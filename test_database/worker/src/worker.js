import 'dotenv/config';
import pg from 'pg';
import * as Minio from 'minio';
import crypto from 'crypto';
import os from 'os';
import fs from 'node:fs/promises';
import { existsSync } from 'node:fs';
import path from 'node:path';
import { spawn } from 'node:child_process';

const { Pool } = pg;

const config = {
  workerId: process.env.WORKER_ID || `nav-worker-${os.hostname()}-${process.pid}`,
  pollMs: Number(process.env.WORKER_POLL_MS || 3000),
  maxArchiveBytes: Number(process.env.NAV_SCAN_MAX_ARCHIVE_BYTES || 100 * 1024 * 1024),
  maxExpandedBytes: Number(process.env.NAV_SCAN_MAX_EXPANDED_BYTES || 250 * 1024 * 1024),
  maxFileBytes: Number(process.env.NAV_SCAN_MAX_FILE_BYTES || 25 * 1024 * 1024),
  maxFiles: Number(process.env.NAV_SCAN_MAX_FILES || 5000),
  maxCompressionRatio: Number(process.env.NAV_SCAN_MAX_COMPRESSION_RATIO || 100),
  rebuildTimeoutMs: Number(process.env.NAV_SCAN_REBUILD_TIMEOUT_MS || 45000),
  canonicalTargets: (process.env.NAV_SCAN_CANONICAL_TARGETS || 'cortex-m4/stm32/stm32f401,cortex-m4/stm32/nucleo_f401re')
    .split(',')
    .map(item => item.trim())
    .filter(Boolean),
  db: {
    host: process.env.DB_HOST || 'localhost',
    port: Number(process.env.DB_PORT || 5432),
    user: process.env.DB_USER || 'nav_sample',
    password: process.env.DB_PASSWORD || 'nav_sample_password',
    database: process.env.DB_NAME || 'nav_sample_registry'
  },
  minio: {
    endPoint: process.env.MINIO_ENDPOINT || 'localhost',
    port: Number(process.env.MINIO_PORT || 9000),
    useSSL: String(process.env.MINIO_USE_SSL || 'false') === 'true',
    accessKey: process.env.MINIO_ACCESS_KEY || 'navsample',
    secretKey: process.env.MINIO_SECRET_KEY || 'navsamplepassword'
  }
};

const pool = new Pool(config.db);
const minio = new Minio.Client(config.minio);
const PACKAGE_BUCKET = 'nav-packages';
const QUARANTINE_BUCKET = 'nav-quarantine';
const ABI_VERSION = 1;
const HAL_API_VERSION = 1;
const ABI_SOURCE_REJECT_EXTENSIONS = new Set([
  'a', 'o', 'obj', 'elf', 'bin', 'hex', 'exe', 'dll', 'so', 'dylib', 'lib', 'class', 'jar'
]);
const KNOWN_HAL_CAPS = new Set(['GPIO', 'I2C', 'I2C_DMA', 'SPI', 'UART', 'TIMER', 'PWM', 'ADC', 'DMA', 'INTERRUPT']);
const TARGET_CAPABILITIES = {
  'cortex-m4/stm32/stm32f401': ['GPIO', 'I2C', 'I2C_DMA', 'SPI', 'UART', 'TIMER', 'PWM', 'ADC', 'DMA', 'INTERRUPT'],
  'cortex-m4/stm32/nucleo_f401re': ['GPIO', 'I2C', 'I2C_DMA', 'SPI', 'UART', 'TIMER', 'PWM', 'ADC', 'DMA', 'INTERRUPT']
};
const STM32_LINKER_LD = `ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 512K
  RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 96K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
  .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
  .text : { *(.text*) *(.rodata*) _etext = .; } > FLASH
  .data : AT(_etext) { _sdata = .; *(.data*) _edata = .; } > RAM
  .bss : { _sbss = .; *(.bss*) *(COMMON) _ebss = .; } > RAM
}
`;
const STM32_STARTUP_C = `#include <stdint.h>
extern uint32_t _estack, _etext, _sdata, _edata, _sbss, _ebss;
extern int main(void);
void Reset_Handler(void);
void Default_Handler(void);
__attribute__((section(".isr_vector")))
void (*const vector_table[])(void) = {
  (void (*)(void))(&_estack), Reset_Handler, Default_Handler, Default_Handler
};
void Reset_Handler(void) {
  uint32_t *src = &_etext;
  for (uint32_t *dst = &_sdata; dst < &_edata;) *dst++ = *src++;
  for (uint32_t *dst = &_sbss; dst < &_ebss;) *dst++ = 0;
  (void)main();
  while (1) {}
}
void Default_Handler(void) { while (1) {} }
`;

function sha256(value) {
  return crypto.createHash('sha256').update(value).digest('hex');
}

function decodedBase64Size(value) {
  const input = String(value || '');
  if (!input) return 0;
  const padding = input.endsWith('==') ? 2 : input.endsWith('=') ? 1 : 0;
  return Math.floor((input.length * 3) / 4) - padding;
}

function safeRelativePath(filePath) {
  const value = String(filePath || '').replace(/\\/g, '/');
  if (!value || value.startsWith('/') || value.includes('\0')) return false;
  const parts = value.split('/');
  return parts.every(part => part && part !== '.' && part !== '..') && value.length <= 240;
}

function hasSecretLikeContent(text) {
  const patterns = [
    /-----BEGIN (RSA |DSA |EC |OPENSSH |)PRIVATE KEY-----/i,
    /\bAKIA[0-9A-Z]{16}\b/,
    /\bghp_[A-Za-z0-9_]{30,}\b/,
    /\bgithub_pat_[A-Za-z0-9_]{30,}\b/,
    /\bAIza[0-9A-Za-z_-]{30,}\b/,
    /(?:password|secret|api[_-]?key|token)\s*[:=]\s*['"][^'"]{12,}['"]/i
  ];
  return patterns.some(pattern => pattern.test(text));
}

function sourceTreeHash(files) {
  const digest = crypto.createHash('sha256');
  const sorted = [...files].sort((left, right) => String(left.path || '').localeCompare(String(right.path || '')));
  for (const file of sorted) {
    digest.update(String(file.path || '').replace(/\\/g, '/'));
    digest.update('\0');
    digest.update(Buffer.from(String(file.content_base64 || ''), 'base64'));
    digest.update('\0');
  }
  return digest.digest('hex');
}

function countMainDefinitions(files) {
  let count = 0;
  for (const file of files) {
    const rel = String(file.path || '');
    const extension = rel.split('.').pop()?.toLowerCase();
    if (!['c', 'cc', 'cpp', 'cxx'].includes(extension)) continue;
    const text = Buffer.from(String(file.content_base64 || ''), 'base64').toString('utf8');
    const withoutComments = text.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/.*$/gm, '');
    if (/\b(?:int|void)\s+main\s*\(/.test(withoutComments)) count += 1;
  }
  return count;
}

function isSemver(value) {
  return /^\d+\.\d+\.\d+(?:-(?:alpha|rc)\.\d+)?$/.test(String(value || ''));
}

function parseSemver(value) {
  const match = String(value || '').match(/^(\d+)\.(\d+)\.(\d+)(?:-([A-Za-z]+)\.(\d+))?$/);
  if (!match) return null;
  return { major: Number(match[1]), minor: Number(match[2]), patch: Number(match[3]), prerelease: match[4] ? `${match[4]}.${match[5]}` : '' };
}

function compareSemver(a, b) {
  const left = parseSemver(a);
  const right = parseSemver(b);
  if (!left || !right) return String(a).localeCompare(String(b));
  for (const key of ['major', 'minor', 'patch']) {
    if (left[key] !== right[key]) return left[key] - right[key];
  }
  if (left.prerelease && !right.prerelease) return -1;
  if (!left.prerelease && right.prerelease) return 1;
  return left.prerelease.localeCompare(right.prerelease);
}

function coercePartialSemver(value) {
  const parts = String(value || '').split('.').map(part => Number(part.replace(/x/i, '0')));
  if (!Number.isFinite(parts[0])) return null;
  return { major: parts[0], minor: Number.isFinite(parts[1]) ? parts[1] : 0, patch: Number.isFinite(parts[2]) ? parts[2] : 0 };
}

function formatSemver(value) {
  return `${value.major}.${value.minor}.${value.patch}`;
}

function semverSatisfies(version, range = '*') {
  const normalizedRange = String(range || '*').trim();
  if (normalizedRange === '*' || normalizedRange === 'latest') return isSemver(version);
  if (normalizedRange.startsWith('=')) return version === normalizedRange.slice(1);
  if (normalizedRange.startsWith('>=')) return compareSemver(version, normalizedRange.slice(2)) >= 0;
  if (normalizedRange.startsWith('^')) {
    const base = coercePartialSemver(normalizedRange.slice(1));
    const parsed = parseSemver(version);
    return Boolean(base && parsed && parsed.major === base.major && compareSemver(version, formatSemver(base)) >= 0);
  }
  if (normalizedRange.startsWith('~')) {
    const base = coercePartialSemver(normalizedRange.slice(1));
    const parsed = parseSemver(version);
    return Boolean(base && parsed && parsed.major === base.major && parsed.minor === base.minor && compareSemver(version, formatSemver(base)) >= 0);
  }
  if (/^\d+\.\d+\.x$/.test(normalizedRange)) {
    const [major, minor] = normalizedRange.split('.').map(Number);
    const parsed = parseSemver(version);
    return parsed?.major === major && parsed?.minor === minor;
  }
  if (/^\d+\.\d+$/.test(normalizedRange)) {
    const [major, minor] = normalizedRange.split('.').map(Number);
    const parsed = parseSemver(version);
    return parsed?.major === major && parsed?.minor === minor;
  }
  return version === normalizedRange;
}

function parsePackageSpec(spec) {
  const raw = String(spec || '');
  const slash = raw.indexOf('/');
  if (slash < 1) throw new Error(`invalid dependency spec ${raw}`);
  const namespace = raw.slice(0, slash);
  const tail = raw.slice(slash + 1);
  const at = tail.lastIndexOf('@');
  return at >= 0
    ? { namespace, name: tail.slice(0, at), range: tail.slice(at + 1) || '*' }
    : { namespace, name: tail, range: '*' };
}

function targetFromTriple(triple) {
  const [arch, vendor, family] = String(triple || '').split('/');
  return { arch, vendor, family: family || '', board: family || '' };
}

function capsForTarget(target) {
  return new Set(TARGET_CAPABILITIES[`${target.arch}/${target.vendor}/${target.family}`] || []);
}

function modulePrefix(name) {
  return String(name || '').replace(/-/g, '_');
}

function sourcePatterns(manifest) {
  return Array.isArray(manifest.sources) ? manifest.sources : Array.isArray(manifest.build?.sources) ? manifest.build.sources : [];
}

function includeDirs(manifest) {
  return Array.isArray(manifest.include_dirs) ? manifest.include_dirs : Array.isArray(manifest.build?.include_dirs) ? manifest.build.include_dirs : [];
}

function packageKind(manifest) {
  return manifest.package_kind || manifest.package?.kind;
}

function packageName(manifest) {
  return manifest.name || manifest.package?.name;
}

function packageVersion(manifest) {
  return manifest.version || manifest.package?.version;
}

function requiredCaps(manifest) {
  return Array.isArray(manifest.requires_caps) ? manifest.requires_caps : Array.isArray(manifest.requires?.caps) ? manifest.requires.caps : [];
}

function optionalCaps(manifest) {
  return Array.isArray(manifest.optional_caps) ? manifest.optional_caps : Array.isArray(manifest.requires?.optional?.caps) ? manifest.requires.optional.caps : [];
}

function dependencySpecs(manifest) {
  if (Array.isArray(manifest.dependencies)) return manifest.dependencies;
  if (!manifest.dependencies || typeof manifest.dependencies !== 'object') return [];
  return Object.entries(manifest.dependencies)
    .filter(([key]) => key !== 'optional')
    .map(([name, range]) => name.includes('/') ? `${name}@${range}` : `nav/${name}@${range}`);
}

function globToRegExp(pattern) {
  const placeholder = '\0';
  const normalized = String(pattern || '').replace(/\\/g, '/').replace(/\*\*\//g, placeholder).replace(/\*\*/g, placeholder);
  let output = '';
  for (const char of normalized) {
    if (char === placeholder) output += '(?:.*/)?';
    else if (char === '*') output += '[^/]*';
    else output += char.replace(/[.+^${}()|[\]\\]/g, '\\$&');
  }
  return new RegExp(`^${output}$`);
}

async function collectFiles(root) {
  const entries = await fs.readdir(root, { withFileTypes: true });
  const files = [];
  for (const entry of entries) {
    const full = path.join(root, entry.name);
    if (entry.isDirectory()) {
      if (['.git', '.nav', 'build', 'node_modules'].includes(entry.name)) continue;
      files.push(...await collectFiles(full));
    } else if (entry.isFile()) {
      files.push(full);
    }
  }
  return files;
}

async function resolveSources(root, patterns) {
  const files = await collectFiles(root);
  const matched = new Set();
  for (const pattern of patterns) {
    const regex = globToRegExp(pattern);
    for (const file of files) {
      const rel = path.relative(root, file).replace(/\\/g, '/');
      if (regex.test(rel) && /\.(c|cc|cpp|cxx|s|S)$/i.test(rel)) matched.add(file);
    }
  }
  return [...matched].sort();
}

async function writeGeneratedHeaders(includeDir, target) {
  const caps = capsForTarget(target);
  const capLines = [...KNOWN_HAL_CAPS].sort().map(cap => `#define NAVHAL_HAS_${cap} ${caps.has(cap) ? 1 : 0}`).join('\n');
  const header = `#pragma once
#define HAL_API_VERSION ${HAL_API_VERSION}
${capLines}
typedef enum { HAL_OK = 0, HAL_ERROR = 1, HAL_BUSY = 2, HAL_TIMEOUT = 3 } hal_status_t;
`;
  await fs.mkdir(includeDir, { recursive: true });
  await fs.writeFile(path.join(includeDir, 'hal.h'), header);
  await fs.writeFile(path.join(includeDir, 'navhal.h'), header);
  await fs.writeFile(path.join(includeDir, 'navhal_abi.h'), header);
}

async function materializeFiles(files, destination) {
  await fs.mkdir(destination, { recursive: true });
  for (const file of files) {
    const out = path.join(destination, String(file.path || '').replace(/\\/g, '/'));
    await fs.mkdir(path.dirname(out), { recursive: true });
    await fs.writeFile(out, Buffer.from(String(file.content_base64 || ''), 'base64'));
  }
}

function runTool(command, args, cwd) {
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, { cwd, stdio: ['ignore', 'pipe', 'pipe'] });
    let output = '';
    const timer = setTimeout(() => {
      child.kill('SIGKILL');
      reject(new Error(`${path.basename(command)} timed out after ${config.rebuildTimeoutMs}ms`));
    }, config.rebuildTimeoutMs);
    child.stdout.on('data', chunk => { output += chunk.toString(); });
    child.stderr.on('data', chunk => { output += chunk.toString(); });
    child.on('error', error => {
      clearTimeout(timer);
      reject(error);
    });
    child.on('exit', code => {
      clearTimeout(timer);
      if (code === 0) resolve(output);
      else reject(new Error(`${path.basename(command)} exited with ${code}: ${output.slice(-1200)}`));
    });
  });
}

async function findExecutable(names) {
  const pathParts = (process.env.PATH || '').split(path.delimiter).filter(Boolean);
  const suffixes = process.platform === 'win32' ? ['', '.exe', '.cmd', '.bat'] : [''];
  for (const dir of pathParts) {
    for (const name of names) {
      for (const suffix of suffixes) {
        const candidate = path.join(dir, name.endsWith(suffix) ? name : `${name}${suffix}`);
        if (existsSync(candidate)) return candidate;
      }
    }
  }
  return null;
}

function compileFlags(target) {
  const cpu = target.arch?.startsWith('cortex-') ? target.arch : 'cortex-m4';
  return [`-mcpu=${cpu}`, '-mthumb', '-ffreestanding', '-fdata-sections', '-ffunction-sections', '-Wall', '-Wextra', '-Os'];
}

function capabilityDefines(target) {
  const caps = capsForTarget(target);
  return [...KNOWN_HAL_CAPS].map(cap => `-DNAVHAL_HAS_${cap}=${caps.has(cap) ? 1 : 0}`);
}

async function validateModuleArchiveSymbols(nm, archive, manifest) {
  const output = await runTool(nm, ['-g', '--defined-only', archive], path.dirname(archive)).catch(() => '');
  const prefix = modulePrefix(packageName(manifest));
  const bad = [];
  for (const line of output.split(/\r?\n/)) {
    const match = line.trim().match(/(?:[0-9a-fA-F]+\s+)?[A-Z]\s+(.+)$/);
    const symbol = match?.[1]?.trim();
    if (!symbol || symbol.startsWith('_')) continue;
    if (symbol === 'main' || symbol.startsWith('hal_') || (!symbol.startsWith(`${prefix}_`) && !symbol.startsWith(prefix.toUpperCase()))) {
      bad.push(symbol);
    }
  }
  if (bad.length) throw new Error(`${packageName(manifest)} exports invalid symbols: ${[...new Set(bad)].join(', ')}`);
}

async function buildAbiModule({ root, manifest, target, buildRoot, generatedInclude, dependencyBuilds, tools }) {
  const targetCaps = capsForTarget(target);
  for (const cap of requiredCaps(manifest)) {
    if (!targetCaps.has(cap)) throw new Error(`${packageName(manifest)} requires ${cap}, unavailable on ${target.arch}/${target.vendor}/${target.family}`);
  }
  const sources = await resolveSources(root, sourcePatterns(manifest));
  if (!sources.length) throw new Error(`${packageName(manifest)} has no matching sources`);
  const publicIncludeRoot = path.join(root, 'include', modulePrefix(packageName(manifest)));
  if (!existsSync(publicIncludeRoot)) throw new Error(`${packageName(manifest)} public headers must live under include/${modulePrefix(packageName(manifest))}/`);
  const includeList = [
    generatedInclude,
    ...includeDirs(manifest).map(item => path.resolve(root, item)),
    ...dependencyBuilds.flatMap(item => item.includeDirs)
  ];
  await fs.mkdir(buildRoot, { recursive: true });
  const objects = [];
  for (const source of sources) {
    const object = path.join(buildRoot, `${path.relative(root, source).replace(/[\\/.:]/g, '_')}.o`);
    await runTool(tools.gcc, [
      ...compileFlags(target),
      ...capabilityDefines(target),
      `-DNAV_MODULE_NAME="${packageName(manifest)}"`,
      `-DNAV_MODULE_VERSION="${packageVersion(manifest)}"`,
      ...includeList.map(dir => `-I${dir}`),
      '-c',
      source,
      '-o',
      object
    ], root);
    objects.push(object);
  }
  const archive = path.join(buildRoot, `lib${packageName(manifest)}.a`);
  await runTool(tools.ar, ['rcs', archive, ...objects], root);
  await validateModuleArchiveSymbols(tools.nm, archive, manifest);
  return { manifest, archive, includeDirs: includeDirs(manifest).map(item => path.resolve(root, item)) };
}

async function buildAbiApp({ root, manifest, target, buildRoot, generatedInclude, dependencyBuilds, tools }) {
  const targetCaps = capsForTarget(target);
  for (const cap of requiredCaps(manifest)) {
    if (!targetCaps.has(cap)) throw new Error(`${packageName(manifest)} requires ${cap}, unavailable on ${target.arch}/${target.vendor}/${target.family}`);
  }
  const sources = await resolveSources(root, sourcePatterns(manifest));
  if (!sources.length) throw new Error(`${packageName(manifest)} has no matching sources`);
  const includeList = [
    generatedInclude,
    ...includeDirs(manifest).map(item => path.resolve(root, item)),
    ...dependencyBuilds.flatMap(item => item.includeDirs)
  ];
  const objects = [];
  const objectDir = path.join(buildRoot, 'app-obj');
  await fs.mkdir(objectDir, { recursive: true });
  for (const source of sources) {
    const object = path.join(objectDir, `${path.relative(root, source).replace(/[\\/.:]/g, '_')}.o`);
    await runTool(tools.gcc, [...compileFlags(target), ...capabilityDefines(target), ...includeList.map(dir => `-I${dir}`), '-c', source, '-o', object], root);
    objects.push(object);
  }
  const startup = path.join(buildRoot, 'startup.c');
  const linker = path.join(buildRoot, 'linker.ld');
  const startupObject = path.join(buildRoot, 'startup.o');
  await fs.writeFile(startup, STM32_STARTUP_C);
  await fs.writeFile(linker, STM32_LINKER_LD);
  await runTool(tools.gcc, [...compileFlags(target), '-c', startup, '-o', startupObject], root);
  const elf = path.join(buildRoot, `${packageName(manifest)}.elf`);
  await runTool(tools.gcc, [...compileFlags(target), ...objects, startupObject, ...dependencyBuilds.map(item => item.archive), '-nostdlib', '-Wl,--gc-sections', `-Wl,-T,${linker}`, '-o', elf], root);
  return { manifest, elf };
}

async function fetchPackageArchive(namespace, name, range) {
  const result = await pool.query(
    `SELECT pv.version, pv.manifest, pa.storage_bucket, pa.storage_key, pa.sha256
     FROM namespaces ns
     JOIN packages p ON p.namespace_id = ns.id
     JOIN package_versions pv ON pv.package_id = p.id
     JOIN package_artifacts pa ON pa.package_version_id = pv.id AND pa.artifact_type = 'archive'
     WHERE ns.name = $1 AND p.slug = $2 AND pv.yanked = FALSE
     ORDER BY pv.created_at DESC`,
    [namespace, name]
  );
  const selected = result.rows
    .filter(row => semverSatisfies(row.version, range))
    .sort((left, right) => compareSemver(left.version, right.version))
    .reverse()[0];
  if (!selected) throw new Error(`dependency ${namespace}/${name}@${range} was not found`);
  const bytes = await objectToBuffer(selected.storage_bucket, selected.storage_key);
  const actual = sha256(bytes);
  if (actual !== selected.sha256) throw new Error(`dependency checksum mismatch for ${namespace}/${name}@${selected.version}`);
  const parsed = JSON.parse(bytes.toString('utf8'));
  return { parsed, version: selected.version };
}

async function resolveAbiDependencies(manifest, workspace, dependencyMap = new Map(), stack = []) {
  const resolved = [];
  for (const spec of dependencySpecs(manifest)) {
    const parsedSpec = parsePackageSpec(spec);
    const key = `${parsedSpec.namespace}/${parsedSpec.name}`;
    if (stack.includes(key)) throw new Error(`dependency cycle: ${[...stack, key].join(' -> ')}`);
    let dependency = dependencyMap.get(key);
    if (!dependency) {
      const archive = await fetchPackageArchive(parsedSpec.namespace, parsedSpec.name, parsedSpec.range);
      const depManifest = archive.parsed.manifest || {};
      if (packageKind(depManifest) !== 'module') throw new Error(`dependency ${key}@${archive.version} is not an ABI module`);
      const depRoot = path.join(workspace, 'deps', parsedSpec.namespace, parsedSpec.name, archive.version);
      await materializeFiles(archive.parsed.files || [], depRoot);
      dependency = { key, root: depRoot, manifest: depManifest, dependencies: [] };
      dependencyMap.set(key, dependency);
      dependency.dependencies = await resolveAbiDependencies(depManifest, workspace, dependencyMap, [...stack, key]);
    }
    resolved.push(dependency);
  }
  return resolved;
}

function flattenDependencies(dependencies, out = []) {
  for (const dependency of dependencies) {
    flattenDependencies(dependency.dependencies || [], out);
    if (!out.some(item => item.key === dependency.key)) out.push(dependency);
  }
  return out;
}

async function rebuildAbiPackageInIsolation(parsed, manifest) {
  const kind = packageKind(manifest);
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'nav-abi-scan-'));
  const subjectRoot = path.join(root, 'subject');
  try {
    await materializeFiles(parsed.files || [], subjectRoot);
    const dependencyTree = await resolveAbiDependencies(manifest, root);
    const dependencyList = flattenDependencies(dependencyTree);
    const targets = kind === 'module'
      ? config.canonicalTargets.map(targetFromTriple)
      : [manifest.target || {}];
    const tools = {
      gcc: await findExecutable(['arm-none-eabi-gcc']),
      ar: await findExecutable(['arm-none-eabi-ar']),
      nm: await findExecutable(['arm-none-eabi-nm'])
    };
    if (!tools.gcc || !tools.ar || !tools.nm) {
      throw new Error('worker image is missing arm-none-eabi-gcc/ar/nm for canonical rebuild');
    }

    const rebuilds = [];
    for (const target of targets) {
      const targetName = `${target.arch}/${target.vendor}/${target.family || target.board}`;
      const targetRoot = path.join(root, 'build', targetName.replace(/[\\/]/g, '_'));
      const generatedInclude = path.join(targetRoot, 'generated', 'include');
      await writeGeneratedHeaders(generatedInclude, target);
      const builtDeps = [];
      for (const dependency of dependencyList) {
        builtDeps.push(await buildAbiModule({
          root: dependency.root,
          manifest: dependency.manifest,
          target,
          buildRoot: path.join(targetRoot, 'deps', packageName(dependency.manifest)),
          generatedInclude,
          dependencyBuilds: builtDeps,
          tools
        }));
      }
      if (kind === 'module') {
        await buildAbiModule({
          root: subjectRoot,
          manifest,
          target,
          buildRoot: path.join(targetRoot, 'subject'),
          generatedInclude,
          dependencyBuilds: builtDeps,
          tools
        });
      } else {
        await buildAbiApp({
          root: subjectRoot,
          manifest,
          target,
          buildRoot: path.join(targetRoot, 'subject'),
          generatedInclude,
          dependencyBuilds: builtDeps,
          tools
        });
      }
      rebuilds.push({ target: targetName, dependencies: dependencyList.length, status: 'pass' });
    }
    return { rebuilds };
  } finally {
    await fs.rm(root, { recursive: true, force: true }).catch(() => null);
  }
}

async function scanAbiManifest(parsed, manifest, files) {
  const findings = [];
  const checks = ['abi-v1-manifest'];
  const rebuilds = [];
  const isAbi = Boolean(manifest?.abi_v1 || manifest?.abi?.abi_version || manifest?.package_kind);
  if (!isAbi) return { findings, checks, rebuilds };

  const kind = manifest.package_kind || manifest.package?.kind;
  const name = manifest.name || manifest.package?.name;
  const version = manifest.version || manifest.package?.version;
  const license = manifest.license || manifest.package?.license;
  const abiVersion = Number(manifest.abi_version || manifest.abi?.abi_version);
  const halApiVersion = Number(manifest.hal_api_version || manifest.abi?.hal_api_version);
  const sourceHash = String(manifest.source_hash || manifest.abi?.source_hash || '').replace(/^sha256:/, '');

  if (!parsed.files.some(file => file.path === 'navmod.toml')) findings.push('ABI v1 packages must include navmod.toml at the package root');
  if (!['module', 'app'].includes(kind)) findings.push('[package].kind must be module or app');
  if (!/^[a-z][a-z0-9_-]{1,63}$/.test(String(name || ''))) findings.push('[package].name is missing or invalid');
  if (!/^\d+\.\d+\.\d+(?:-(?:alpha|rc)\.\d+)?$/.test(String(version || ''))) findings.push('[package].version must be semver');
  if (!license) findings.push('[package].license is required');
  if (abiVersion !== ABI_VERSION) findings.push(`[abi].abi_version must be ${ABI_VERSION}`);
  if (halApiVersion !== HAL_API_VERSION) findings.push(`[abi].hal_api_version must be ${HAL_API_VERSION}`);
  if (sourceHash && sourceHash !== sourceTreeHash(files)) findings.push('[abi].source_hash does not match packaged source tree');

  const caps = [
    ...(Array.isArray(manifest.requires_caps) ? manifest.requires_caps : []),
    ...(Array.isArray(manifest.optional_caps) ? manifest.optional_caps : [])
  ];
  for (const cap of caps) {
    if (!KNOWN_HAL_CAPS.has(cap)) findings.push(`unknown HAL capability ${cap}`);
  }

  for (const file of files) {
    const rel = String(file.path || '');
    const extension = rel.split('.').pop()?.toLowerCase();
    if (ABI_SOURCE_REJECT_EXTENSIONS.has(extension)) findings.push(`ABI v1 source-only package contains binary artifact ${rel}`);
    if (['c', 'cc', 'cpp', 'cxx', 'h', 'hpp'].includes(extension)) {
      const text = Buffer.from(String(file.content_base64 || ''), 'base64').toString('utf8');
      if (/^\s*#\s*define\s+NAVHAL_/m.test(text)) findings.push(`source redefines NAVHAL_* macro in ${rel}`);
    }
    if (/^Kconfig$/i.test(rel)) {
      const text = Buffer.from(String(file.content_base64 || ''), 'base64').toString('utf8');
      if (/^\s*mainmenu\b/m.test(text)) findings.push('module Kconfig must not contain mainmenu');
      if (/^\s*source\s+["']?(?:\.\.|\/)/m.test(text)) findings.push('module Kconfig must not source outside the module tree');
      if (/^\s*select\s+DRV_/m.test(text)) findings.push('module Kconfig must not select NavHAL DRV_* caps');
    }
  }

  const mainCount = countMainDefinitions(files);
  if (kind === 'module' && mainCount > 0) findings.push(`ABI v1 module must not define main(); found ${mainCount}`);
  if (kind === 'app' && mainCount !== 1) findings.push(`ABI v1 app must define exactly one main(); found ${mainCount}`);
  checks.push('abi-source-only', 'abi-source-hash', 'abi-main-policy', 'abi-hal-cap-policy');

  if (findings.length === 0) {
    try {
      const rebuild = await rebuildAbiPackageInIsolation(parsed, manifest);
      checks.push('abi-canonical-rebuild');
      rebuilds.push(...rebuild.rebuilds);
    } catch (error) {
      findings.push(`ABI canonical rebuild failed: ${error.message}`);
      checks.push('abi-canonical-rebuild');
    }
  }
  return { findings, checks, rebuilds };
}

async function scanNavPackage(buffer) {
  const findings = [];
  const checks = [];
  if (buffer.length > config.maxArchiveBytes) {
    findings.push(`archive exceeds ${config.maxArchiveBytes} byte upload limit`);
  }

  let parsed;
  try {
    parsed = JSON.parse(buffer.toString('utf8'));
    checks.push('navpkg-json-parse');
  } catch {
    findings.push('unsupported archive format: package uploads must be .navpkg JSON archives for this scanner');
    return { passed: false, checks, findings, expandedBytes: 0, fileCount: 0 };
  }

  if (!parsed || parsed.schema_version !== 1 || !Array.isArray(parsed.files)) {
    findings.push('invalid navpkg schema');
  }
  if (!parsed.manifest || typeof parsed.manifest !== 'object') {
    findings.push('missing package manifest');
  } else {
    checks.push('manifest-present');
  }
  if (parsed.files?.length > config.maxFiles) {
    findings.push(`file count ${parsed.files.length} exceeds ${config.maxFiles}`);
  }

  let expandedBytes = 0;
  const seen = new Set();
  for (const file of parsed.files || []) {
    const rel = String(file?.path || '');
    if (!safeRelativePath(rel)) {
      findings.push(`unsafe file path: ${rel || '<empty>'}`);
      continue;
    }
    if (seen.has(rel)) findings.push(`duplicate file path: ${rel}`);
    seen.add(rel);

    const size = decodedBase64Size(file?.content_base64);
    expandedBytes += size;
    if (size > config.maxFileBytes) findings.push(`file ${rel} exceeds per-file size limit`);
    if (expandedBytes > config.maxExpandedBytes) findings.push(`expanded package exceeds ${config.maxExpandedBytes} bytes`);

    const extension = rel.split('.').pop()?.toLowerCase();
    if (size > 0 && size <= 512 * 1024 && ['c', 'cc', 'cpp', 'h', 'hpp', 'txt', 'md', 'toml', 'json', 'yaml', 'yml', 'ini'].includes(extension)) {
      const text = Buffer.from(String(file.content_base64 || ''), 'base64').toString('utf8');
      if (hasSecretLikeContent(text)) findings.push(`possible secret in ${rel}`);
    }
  }

  const compressionRatio = buffer.length ? expandedBytes / buffer.length : 0;
  if (compressionRatio > config.maxCompressionRatio) {
    findings.push(`expanded-to-archive ratio ${compressionRatio.toFixed(1)} exceeds ${config.maxCompressionRatio}`);
  }

  const abiScan = await scanAbiManifest(parsed, parsed.manifest || {}, parsed.files || []);
  findings.push(...abiScan.findings);
  checks.push(...abiScan.checks);

  checks.push('path-traversal-policy', 'file-count-limit', 'expanded-size-limit', 'secret-patterns');
  return {
    passed: findings.length === 0,
    checks,
    findings,
    expandedBytes,
    fileCount: parsed.files?.length || 0,
    manifest: parsed.manifest || {},
    rebuilds: abiScan.rebuilds || []
  };
}

async function ensureBucket(bucket) {
  const exists = await minio.bucketExists(bucket).catch(() => false);
  if (!exists) await minio.makeBucket(bucket);
}

async function objectToBuffer(bucket, key) {
  const stream = await minio.getObject(bucket, key);
  const chunks = [];
  for await (const chunk of stream) chunks.push(chunk);
  return Buffer.concat(chunks);
}

async function claimJob() {
  const client = await pool.connect();
  try {
    await client.query('BEGIN');
    const result = await client.query(
      `UPDATE scan_jobs
       SET status = 'running',
           attempts = attempts + 1,
           locked_by = $1,
           locked_at = now(),
           started_at = COALESCE(started_at, now()),
           updated_at = now()
       WHERE id = (
         SELECT id
         FROM scan_jobs
         WHERE status = 'queued'
           AND subject_type = 'publish_session'
         ORDER BY priority ASC, created_at ASC
         FOR UPDATE SKIP LOCKED
         LIMIT 1
       )
       RETURNING *`,
      [config.workerId]
    );
    await client.query('COMMIT');
    return result.rows[0] || null;
  } catch (error) {
    await client.query('ROLLBACK').catch(() => null);
    throw error;
  } finally {
    client.release();
  }
}

async function promotePackage(job, session, buffer, scan) {
  await ensureBucket(PACKAGE_BUCKET);
  await minio.putObject(PACKAGE_BUCKET, session.upload_storage_key, buffer, buffer.length, {
    'Content-Type': session.content_type || 'application/octet-stream',
    'x-amz-meta-sha256': session.uploaded_sha256
  });

  const client = await pool.connect();
  try {
    await client.query('BEGIN');
    const versionRow = await client.query(
      `INSERT INTO package_versions (package_id, version, manifest, readme, changelog, checksum_sha256, integrity_signature, published_by)
       VALUES ($1, $2, $3, $4, $5, $6, $7, $8)
       RETURNING *`,
      [
        session.package_id,
        session.version,
        session.manifest || {},
        session.readme || '',
        session.changelog || null,
        session.uploaded_sha256,
        session.integrity_signature || null,
        session.user_id
      ]
    );
    await client.query(
      `INSERT INTO package_artifacts (package_version_id, artifact_type, storage_bucket, storage_key, content_type, size_bytes, sha256)
       VALUES ($1, 'archive', $2, $3, $4, $5, $6)`,
      [versionRow.rows[0].id, PACKAGE_BUCKET, session.upload_storage_key, session.content_type || 'application/octet-stream', session.size_bytes, session.uploaded_sha256]
    );
    await client.query(
      `INSERT INTO security_scans (subject_type, subject_id, scanner, status, result)
       VALUES ('package_version', $1, 'nav-archive-policy-v1', 'pass', $2)`,
      [versionRow.rows[0].id, {
        checks: scan.checks,
        expanded_bytes: scan.expandedBytes,
        file_count: scan.fileCount,
        canonical_rebuilds: scan.rebuilds || [],
        validation_level: 'archive-policy'
      }]
    );
    await client.query(`UPDATE publish_sessions SET status = 'published' WHERE id = $1`, [session.id]);
    await client.query(
      `UPDATE scan_jobs SET status = 'passed', finished_at = now(), updated_at = now(), last_error = NULL WHERE id = $1`,
      [job.id]
    );
    await client.query('COMMIT');
  } catch (error) {
    await client.query('ROLLBACK').catch(() => null);
    throw error;
  } finally {
    client.release();
  }
}

async function rejectPackage(job, session, scan) {
  await pool.query(
    `INSERT INTO security_scans (subject_type, subject_id, scanner, status, result)
     VALUES ('publish_session', $1, 'nav-archive-policy-v1', 'fail', $2)`,
    [session.id, {
      checks: scan.checks,
      findings: scan.findings,
      expanded_bytes: scan.expandedBytes,
      file_count: scan.fileCount,
      canonical_rebuilds: scan.rebuilds || [],
      validation_level: 'archive-policy'
    }]
  );
  await pool.query(`UPDATE publish_sessions SET status = 'rejected' WHERE id = $1`, [session.id]);
  await pool.query(
    `UPDATE scan_jobs SET status = 'failed', last_error = $1, finished_at = now(), updated_at = now() WHERE id = $2`,
    [scan.findings.join('; ').slice(0, 2000), job.id]
  );
}

async function failJob(job, error) {
  const nextStatus = job.attempts >= job.max_attempts ? 'failed' : 'queued';
  await pool.query(
    `UPDATE scan_jobs
     SET status = $1, last_error = $2, locked_by = NULL, locked_at = NULL, updated_at = now()
     WHERE id = $3`,
    [nextStatus, String(error.message || error).slice(0, 2000), job.id]
  );
}

async function processJob(job) {
  const sessionResult = await pool.query(
    `SELECT ps.*, p.slug, ns.name AS namespace
     FROM publish_sessions ps
     JOIN packages p ON p.id = ps.package_id
     JOIN namespaces ns ON ns.id = p.namespace_id
     WHERE ps.id = $1`,
    [job.subject_id]
  );
  if (sessionResult.rowCount === 0) {
    throw new Error(`publish session not found: ${job.subject_id}`);
  }
  const session = sessionResult.rows[0];
  if (!['queued', 'uploaded'].includes(session.status)) {
    await pool.query(`UPDATE scan_jobs SET status = 'skipped', finished_at = now(), updated_at = now() WHERE id = $1`, [job.id]);
    return;
  }
  const buffer = await objectToBuffer(QUARANTINE_BUCKET, session.quarantine_storage_key);
  const actualSha = sha256(buffer);
  if (actualSha !== session.uploaded_sha256) {
    await rejectPackage(job, session, {
      passed: false,
      checks: ['sha256'],
      findings: ['quarantine object checksum changed after upload'],
      expandedBytes: 0,
      fileCount: 0
    });
    return;
  }
  const scan = await scanNavPackage(buffer);
  if (!scan.passed) {
    await rejectPackage(job, session, scan);
    console.log(`[worker] rejected ${session.namespace}/${session.slug}@${session.version}: ${scan.findings.join('; ')}`);
    return;
  }
  await promotePackage(job, session, buffer, scan);
  console.log(`[worker] published ${session.namespace}/${session.slug}@${session.version}`);
}

async function loop() {
  console.log(`[worker] ${config.workerId} online`);
  for (;;) {
    const job = await claimJob();
    if (!job) {
      await new Promise(resolve => setTimeout(resolve, config.pollMs));
      continue;
    }
    try {
      await processJob(job);
    } catch (error) {
      console.error(`[worker] job ${job.id} failed`, error);
      await failJob(job, error);
    }
  }
}

export { scanNavPackage, rebuildAbiPackageInIsolation };

if (process.env.NODE_ENV !== 'test') {
  loop().catch(error => {
    console.error('[worker] fatal', error);
    process.exit(1);
  });
}
