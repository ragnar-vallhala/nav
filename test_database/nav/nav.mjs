#!/usr/bin/env node
import fs from 'node:fs/promises';
import { existsSync } from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { spawn } from 'node:child_process';
import http from 'node:http';

const ROOT = path.dirname(fileURLToPath(import.meta.url));
const LEGACY_HOME = path.join(ROOT, '.nav-home');
const HOME = process.env.NAV_HOME || (existsSync(LEGACY_HOME) ? LEGACY_HOME : path.join(os.homedir(), '.nav'));
const CONFIG = path.join(HOME, 'config.json');
const CACHE = path.join(HOME, 'cache');
const TOOLCHAIN_HOME = path.join(HOME, 'toolchains');
const API = process.env.NAV_REGISTRY_URL || process.env.NAV_TEST_REGISTRY || 'http://localhost:14000';

const args = process.argv.slice(2);

const STM32_LINKER_LD = `ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 512K
  RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 96K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
  .isr_vector :
  {
    KEEP(*(.isr_vector))
  } > FLASH

  .text :
  {
    *(.text*)
    *(.rodata*)
    KEEP(*(.init))
    KEEP(*(.fini))
    _etext = .;
  } > FLASH

  .data : AT(_etext)
  {
    _sdata = .;
    *(.data*)
    _edata = .;
  } > RAM

  .bss :
  {
    _sbss = .;
    *(.bss*)
    *(COMMON)
    _ebss = .;
  } > RAM
}
`;

const STM32_STARTUP_C = `#include <stdint.h>

extern uint32_t _estack;
extern uint32_t _etext;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;

extern int main(void);

void Reset_Handler(void);
void Default_Handler(void);

__attribute__((section(".isr_vector")))
void (*const vector_table[])(void) = {
    (void (*)(void))(&_estack),
    Reset_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
    Default_Handler,
    0,
    0,
    0,
    0,
    Default_Handler,
    Default_Handler,
    0,
    Default_Handler,
    Default_Handler,
    Default_Handler
};

void Reset_Handler(void) {
    uint32_t *src = &_etext;
    uint32_t *dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }

    dst = &_sbss;
    while (dst < &_ebss) {
        *dst++ = 0;
    }

    (void)main();
    while (1) {}
}

void Default_Handler(void) {
    while (1) {}
}
`;

function usage() {
  console.log(`Nav CLI

Usage:
  nav help
  nav check
  nav signup <name> <email> <password>
  nav login
  nav logout
  nav update-cli
  nav whoami

  nav search [query]
  nav namespace list
  nav namespace create <name>
  nav namespace members <namespace>
  nav namespace member-add <namespace> <email> [role]

  nav package list [query]
  nav package create <namespace> <slug> <description>
  nav package info <namespace/package>
  nav docs <namespace/package>

  nav create <project> [--board <board>]
  nav setup [project-folder]
  nav add <namespace/package[@version]>
  nav build [project-folder]
  nav run [project-folder] [--port COMx]
  nav clean [project-folder]
  nav upload [project-folder] [--port COMx]
  nav monitor [project-folder] [--port COMx]
  nav update [project-folder]

  nav pack <folder> --out <archive.navpkg>
  nav publish <namespace> <package> <version> <archive.navpkg> [--changelog CHANGELOG.md]
  nav install <namespace/package[@version]>

  nav toolchain list
  nav toolchain publish <metadata.json> <archive.zip>

Examples:
  nav signup "Your Name" you@example.com <password>
  nav login
  nav logout
  nav search esp32
  nav setup
  # empty folders are initialized from nav/navhal@0.1.0 in the registry

  nav create blink_twiceproject --board esp32-devkit-v1
  cd blink_twiceproject
  nav add nav/esp32-blink@0.1.0
  nav setup
  nav build

Registry: ${API}
State: ${HOME}`);
}

function terminalWidth() {
  return Math.max(80, Number(process.stdout.columns || 120));
}

function truncateCell(value, width) {
  const text = String(value ?? '');
  if (text.length <= width) return text;
  if (width <= 3) return text.slice(0, width);
  return `${text.slice(0, width - 3)}...`;
}

function printTable(columns, rows, { maxWidth = terminalWidth() } = {}) {
  if (!rows.length) {
    console.log('(none)');
    return;
  }

  const minWidths = columns.map(column => Math.max(column.header.length, column.min || 8));
  const preferred = columns.map((column, index) => Math.min(
    column.max || 42,
    Math.max(
      minWidths[index],
      ...rows.map(row => String(row[column.key] ?? '').length)
    )
  ));
  let total = preferred.reduce((sum, width) => sum + width, 0) + (columns.length - 1) * 3;
  while (total > maxWidth && preferred.some((width, index) => width > minWidths[index])) {
    let widest = 0;
    for (let index = 1; index < preferred.length; index += 1) {
      if (preferred[index] - minWidths[index] > preferred[widest] - minWidths[widest]) widest = index;
    }
    preferred[widest] -= 1;
    total -= 1;
  }

  const line = row => columns
    .map((column, index) => truncateCell(row[column.key], preferred[index]).padEnd(preferred[index]))
    .join('   ')
    .trimEnd();
  const header = Object.fromEntries(columns.map(column => [column.key, column.header]));
  console.log(line(header));
  console.log(columns.map((_, index) => '-'.repeat(preferred[index])).join('   ').trimEnd());
  rows.forEach(row => console.log(line(row)));
}

async function main() {
  await fs.mkdir(HOME, { recursive: true });
  await fs.mkdir(CACHE, { recursive: true });
  await fs.mkdir(TOOLCHAIN_HOME, { recursive: true });

  const [command, subcommand, ...rest] = args;
  try {
    if (!command || command === 'help' || command === '--help') return usage();
    if (command === 'check') return check(subcommand);
    if (command === 'signup') return signup(subcommand, rest[0], rest[1]);
    if (command === 'login') return login([subcommand, ...rest].filter(Boolean));
    if (command === 'logout') return logout();
    if (command === 'update-cli' || command === 'self-update') return updateCli();
    if (command === 'whoami') return whoami();
    if (command === 'search') return packageList([subcommand, ...rest].filter(Boolean).join(' '));
    if (command === 'namespace') return namespaceCommand(subcommand, rest);
    if (command === 'package') return packageCommand(subcommand, rest);
    if (command === 'info') return packageInfo(subcommand);
    if (command === 'docs') return docs(subcommand);
    if (command === 'create') return createProject(subcommand, rest);
    if (command === 'setup') return setupProject([subcommand, ...rest].filter(Boolean));
    if (command === 'add') return addPackage(subcommand);
    if (command === 'build') return buildProject(subcommand);
    if (command === 'run') return runProject([subcommand, ...rest].filter(Boolean));
    if (command === 'clean') return cleanProject(subcommand);
    if (command === 'upload') return uploadProject([subcommand, ...rest].filter(Boolean));
    if (command === 'monitor') return monitorProject([subcommand, ...rest].filter(Boolean));
    if (command === 'update') return setupProject(subcommand);
    if (command === 'pack') return pack(subcommand, rest);
    if (command === 'publish') return publish(subcommand, rest);
    if (command === 'install' || command === 'download') return install(subcommand);
    if (command === 'toolchain') return toolchainCommand(subcommand, rest);
    throw new Error(`Unknown command: ${command}`);
  } catch (error) {
    console.error(`nav: ${error.message}`);
    process.exitCode = 1;
  }
}

async function readConfig() {
  try {
    return JSON.parse(await fs.readFile(CONFIG, 'utf8'));
  } catch {
    return {};
  }
}

async function writeConfig(config) {
  await fs.writeFile(CONFIG, JSON.stringify(config, null, 2));
}

async function request(route, options = {}) {
  const config = await readConfig();
  const headers = {
    ...(options.body && !(options.body instanceof Buffer) ? { 'Content-Type': 'application/json' } : {}),
    ...(config.token ? { Authorization: `Bearer ${config.token}` } : {}),
    ...(options.headers || {})
  };
  let response;
  try {
    response = await fetch(`${API}${route}`, { ...options, headers });
  } catch (error) {
    throw new Error(`registry is unreachable at ${API} (${error.message})`);
  }
  const type = response.headers.get('content-type') || '';
  if (!response.ok) {
    const body = type.includes('application/json') ? await response.json() : { error: await response.text() };
    throw new Error(body.error || `HTTP ${response.status}`);
  }
  if (type.includes('application/json')) return response.json();
  return response.arrayBuffer();
}

async function smoke() {
  const health = await request('/health');
  const stats = await request('/stats');
  console.log(`backend: ${health.status}`);
  console.log(`packages: ${stats.packages}, versions: ${stats.package_versions}, toolchains: ${stats.toolchains}`);
  console.log(`registry: ${API}`);
}

async function check(projectArg) {
  await smoke();
  const projectDir = projectArg ? path.resolve(projectArg) : process.cwd();
  const manifest = await readProjectManifest(projectDir).catch(() => null);
  if (!manifest) {
    console.log('project: no nav.toml/nav.json in current folder');
    return;
  }
  const lockPath = path.join(projectDir, '.nav', 'lock.json');
  let lock = await readJson(lockPath).catch(() => null);
  if (!lock || !(await lockPathsExist(lock))) {
    console.log('project dependencies/toolchains are not fully installed; running nav setup');
    await setupProject(projectDir);
    lock = await readJson(lockPath).catch(() => null);
  }
  console.log(`project: ${manifest.name || path.basename(projectDir)}`);
  console.log(`board: ${manifest.board || '-'}`);
  console.log(`dependencies: ${(manifest.dependencies || []).length}`);
  console.log(`toolchains requested: ${(manifest.toolchains || []).join(', ') || '-'}`);
  console.log(`lock: ${lock ? `${lock.toolchains.length} toolchains, ${lock.packages.length} packages` : 'missing, run nav setup'}`);
}

async function signup(name, email, password) {
  if (!name || !email || !password) throw new Error('signup requires <name> <email> <password>');
  const data = await request('/auth/register', {
    method: 'POST',
    body: JSON.stringify({ name, email, password })
  });
  console.log(`verification required for ${data.email}`);
  console.log('Check your email for the 5-minute OTP, then verify in the web dashboard.');
}

async function login(rawArgs = []) {
  if (rawArgs.length > 0) {
    throw new Error('nav login no longer accepts passwords in the terminal. Run `nav login` and complete sign-in in your browser.');
  }

  const state = crypto.randomBytes(18).toString('hex');
  const result = await waitForBrowserLogin(state);
  await writeConfig({ token: result.token, registry: API });
  const data = await request('/me');
  await writeConfig({ token: result.token, user: data.user, registry: API });
  console.log(`logged in as ${data.user.email}`);
}

async function logout() {
  const config = await readConfig();
  if (!config.token) {
    console.log('not logged in');
    return;
  }
  delete config.token;
  delete config.user;
  await writeConfig(config);
  console.log('logged out');
}

async function updateCli() {
  const currentPath = fileURLToPath(import.meta.url);
  const tempPath = path.join(os.tmpdir(), `nav-cli-${Date.now()}-${crypto.randomBytes(4).toString('hex')}.mjs`);
  const backupPath = `${currentPath}.bak`;

  console.log(`checking latest CLI from ${API}`);
  const bytes = Buffer.from(await request('/downloads/nav/nav.mjs'));
  await fs.writeFile(tempPath, bytes);

  await runCapture(process.execPath, ['--check', tempPath]);

  const currentHash = await sha256File(currentPath).catch(() => null);
  const nextHash = await sha256File(tempPath);
  if (currentHash === nextHash) {
    await fs.rm(tempPath, { force: true });
    console.log('Nav CLI is already up to date.');
    return;
  }

  await fs.copyFile(currentPath, backupPath).catch(() => null);
  await fs.copyFile(tempPath, currentPath);
  if (process.platform !== 'win32') await fs.chmod(currentPath, 0o755).catch(() => null);
  await fs.rm(tempPath, { force: true });

  console.log('Nav CLI updated successfully.');
  console.log(`backup: ${backupPath}`);
}

async function waitForBrowserLogin(state) {
  const server = http.createServer();
  let settled = false;
  let timeout;

  const resultPromise = new Promise((resolve, reject) => {
    server.on('request', (req, res) => {
      const callbackUrl = new URL(req.url || '/', 'http://127.0.0.1');
      if (callbackUrl.pathname !== '/callback') {
        res.writeHead(404, { 'Content-Type': 'text/plain' });
        res.end('Not found');
        return;
      }

      const error = callbackUrl.searchParams.get('error');
      const token = callbackUrl.searchParams.get('token');
      const returnedState = callbackUrl.searchParams.get('state');

      if (returnedState !== state) {
        res.writeHead(400, { 'Content-Type': 'text/html; charset=utf-8' });
        res.end(cliLoginPage({
          title: 'Login failed',
          message: 'State mismatch. Close this tab and run nav login again.',
          tone: 'error'
        }));
        if (!settled) {
          settled = true;
          reject(new Error('login state mismatch'));
        }
        return;
      }

      if (error || !token) {
        res.writeHead(400, { 'Content-Type': 'text/html; charset=utf-8' });
        res.end(cliLoginPage({
          title: 'Login failed',
          message: error || 'Missing token',
          tone: 'error'
        }));
        if (!settled) {
          settled = true;
          reject(new Error(error || 'login did not return a token'));
        }
        return;
      }

      res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
      res.end(cliLoginPage({
        title: 'Nav login complete',
        message: 'You can close this browser tab and return to your terminal.',
        tone: 'success'
      }));
      if (!settled) {
        settled = true;
        resolve({ token });
      }
    });

    server.on('error', reject);
    timeout = setTimeout(() => {
      if (!settled) {
        settled = true;
        reject(new Error('login timed out after 10 minutes'));
      }
    }, 10 * 60 * 1000);
  });

  await new Promise((resolve, reject) => {
    server.listen(0, '127.0.0.1', () => resolve());
    server.once('error', reject);
  });

  const { port } = server.address();
  const redirectUri = `http://127.0.0.1:${port}/callback`;
  const loginUrl = `${API}/auth/cli/start?redirect_uri=${encodeURIComponent(redirectUri)}&state=${encodeURIComponent(state)}`;
  console.log('Opening Nav login in your browser.');
  console.log(`If it does not open, paste this link into your browser:\n${yellow(loginUrl)}`);
  openBrowser(loginUrl);

  try {
    return await resultPromise;
  } finally {
    clearTimeout(timeout);
    await new Promise(resolve => server.close(() => resolve()));
  }
}

function openBrowser(url) {
  const options = { detached: true, stdio: 'ignore' };
  let child;
  if (process.platform === 'win32') {
    child = spawn('rundll32', ['url.dll,FileProtocolHandler', url], options);
  } else if (process.platform === 'darwin') {
    child = spawn('open', [url], options);
  } else {
    child = spawn('xdg-open', [url], options);
  }
  child.on('error', () => {});
  child.unref();
}

function cliLoginPage({ title, message, tone }) {
  const isSuccess = tone === 'success';
  const accent = isSuccess ? '#22c55e' : '#ef4444';
  const badgeText = isSuccess ? 'Authenticated' : 'Action needed';
  return `<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>${escapeHtml(title)}</title>
  <style>
    :root {
      color-scheme: dark;
      font-family: Inter, ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      background: #08090b;
      color: #f8fafc;
    }
    * { box-sizing: border-box; }
    body {
      min-height: 100vh;
      margin: 0;
      display: grid;
      place-items: center;
      padding: 24px;
      background:
        radial-gradient(circle at 50% 0%, rgba(255,255,255,0.08), transparent 34rem),
        #08090b;
    }
    .card {
      width: min(460px, 100%);
      border: 1px solid #27272a;
      border-radius: 14px;
      background: #0d0f12;
      box-shadow: 0 24px 80px rgba(0,0,0,0.45);
      padding: 28px;
    }
    .brand {
      display: flex;
      align-items: center;
      gap: 12px;
      margin-bottom: 28px;
      font-weight: 700;
    }
    .mark {
      width: 34px;
      height: 34px;
      border-radius: 10px;
      display: grid;
      place-items: center;
      background: #f8fafc;
      color: #09090b;
      font-weight: 800;
    }
    .status {
      display: inline-flex;
      align-items: center;
      gap: 8px;
      border: 1px solid #27272a;
      border-radius: 999px;
      padding: 6px 10px;
      color: #d4d4d8;
      font-size: 13px;
      margin-bottom: 14px;
    }
    .dot {
      width: 8px;
      height: 8px;
      border-radius: 999px;
      background: ${accent};
      box-shadow: 0 0 0 4px color-mix(in srgb, ${accent} 18%, transparent);
    }
    h1 {
      margin: 0 0 10px;
      font-size: 28px;
      line-height: 1.15;
      letter-spacing: 0;
    }
    p {
      margin: 0;
      color: #a1a1aa;
      font-size: 16px;
      line-height: 1.6;
    }
    .footer {
      margin-top: 24px;
      padding-top: 18px;
      border-top: 1px solid #27272a;
      color: #71717a;
      font-size: 13px;
    }
  </style>
</head>
<body>
  <main class="card">
    <div class="brand">
      <div class="mark">N</div>
      <div>Nav</div>
    </div>
    <div class="status"><span class="dot"></span>${badgeText}</div>
    <h1>${escapeHtml(title)}</h1>
    <p>${escapeHtml(message)}</p>
    <div class="footer">This local callback was opened by the Nav CLI.</div>
  </main>
</body>
</html>`;
}

function yellow(value) {
  return process.stdout.isTTY ? `\x1b[33m${value}\x1b[0m` : value;
}

function escapeHtml(value) {
  return String(value)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

async function whoami() {
  const data = await request('/me');
  console.log(`${data.user.name} <${data.user.email}>`);
}

async function namespaceCommand(subcommand, rest) {
  if (subcommand === 'list') {
    const data = await request('/namespaces');
    printTable(
      [
        { key: 'name', header: 'Namespace', min: 14, max: 28 },
        { key: 'role', header: 'Role', min: 10, max: 14 },
        { key: 'packages', header: 'Packages', min: 8, max: 10 },
        { key: 'members', header: 'Members', min: 7, max: 10 }
      ],
      data.namespaces.map(ns => ({
        name: ns.name,
        role: ns.role,
        packages: ns.packages,
        members: ns.members
      }))
    );
    return;
  }
  if (subcommand === 'create') {
    const data = await request('/namespaces', {
      method: 'POST',
      body: JSON.stringify({ name: rest[0] })
    });
    console.log(`created namespace ${data.namespace.name}`);
    return;
  }
  if (subcommand === 'members') {
    const data = await request(`/namespaces/${rest[0]}/members`);
    printTable(
      [
        { key: 'email', header: 'Email', min: 24, max: 42 },
        { key: 'role', header: 'Role', min: 10, max: 14 },
        { key: 'status', header: 'Status', min: 8, max: 10 }
      ],
      data.members.map(member => ({
        email: member.email,
        role: member.role,
        status: member.accepted_at ? 'accepted' : 'invited'
      }))
    );
    return;
  }
  if (subcommand === 'member-add') {
    const [namespace, email, role = 'maintainer'] = rest;
    const data = await request(`/namespaces/${namespace}/members`, {
      method: 'POST',
      body: JSON.stringify({ email, role })
    });
    console.log(`added ${data.member.email} as ${data.member.role}`);
    return;
  }
  throw new Error('namespace command must be list, create, members, or member-add');
}

async function packageCommand(subcommand, rest) {
  if (subcommand === 'list') return packageList(rest[0]);
  if (subcommand === 'create') {
    const [namespace, slug, ...descriptionParts] = rest;
    const description = descriptionParts.join(' ') || `${slug} package`;
    const data = await request('/packages', {
      method: 'POST',
      body: JSON.stringify({ namespace, slug, name: slug, description })
    });
    console.log(`created package ${data.package.namespace}/${data.package.slug}`);
    return;
  }
  if (subcommand === 'info') return packageInfo(rest[0]);
  throw new Error('package command must be list, create, or info');
}

async function packageList(query = '') {
  const q = query ? `?q=${encodeURIComponent(query)}` : '';
  const data = await request(`/packages${q}`);
  printTable(
    [
      { key: 'package', header: 'Package', min: 24, max: 40 },
      { key: 'version', header: 'Version', min: 10, max: 18 },
      { key: 'description', header: 'Description', min: 24, max: 64 }
    ],
    data.packages.map(pkg => ({
      package: `${pkg.namespace}/${pkg.slug}`,
      version: pkg.latest_version || '-',
      description: pkg.description || ''
    }))
  );
}

async function packageInfo(spec) {
  const { namespace, name } = parseSpec(spec);
  const data = await request(`/packages/${namespace}/${name}`);
  console.log(`${data.package.namespace}/${data.package.slug}`);
  console.log(data.package.description || '');
  console.log(`license: ${data.package.license || '-'}`);
  console.log('versions:');
  printTable(
    [
      { key: 'version', header: 'Version', min: 10, max: 18 },
      { key: 'sha256', header: 'SHA256', min: 18, max: 24 },
      { key: 'changelog', header: 'Changelog', min: 24, max: 70 }
    ],
    data.versions.map(version => ({
      version: version.version,
      sha256: version.sha256 ? `${version.sha256.slice(0, 16)}...` : '-',
      changelog: (version.changelog || version.manifest?.changelog || '').split(/\r?\n/)[0] || '-'
    }))
  );
}

async function docs(spec) {
  const { namespace, name } = parseSpec(spec);
  const data = await request(`/packages/${namespace}/${name}/docs`);
  console.log(`${data.package.namespace}/${data.package.name}`);
  console.log(data.package.description || '');
  console.log('\nCommands:');
  Object.entries(data.commands).forEach(([key, value]) => console.log(`  ${key}: ${value}`));
  console.log('\nManifest:');
  console.log(JSON.stringify(data.manifest, null, 2));
}

async function createProject(projectName, rest) {
  if (!projectName) throw new Error('create requires <project> [--board <board>]');
  const { board } = parseSetupArgs(rest, 'esp32-devkit-v1');
  const projectDir = path.resolve(projectName);
  await initializeProjectScaffold(projectDir, projectName, board, { overwriteMain: false });
  console.log(`created ${projectDir}`);
  console.log('next: cd into the project and run nav setup');
}

async function initializeProjectScaffold(projectDir, projectName, board, options = {}) {
  await fs.mkdir(path.join(projectDir, 'src'), { recursive: true });
  const projectSlug = slug(projectName || path.basename(projectDir) || 'nav-project');
  const manifest = {
    name: projectSlug,
    version: '0.1.0',
    board,
    target: board.startsWith('esp32') ? 'esp32' : board,
    toolchains: board.startsWith('esp32') ? ['arduino-cli'] : board.startsWith('stm32') ? ['arm-none-eabi', 'stlink'] : [],
    dependencies: [`nav/board-${board}@0.1.0`],
    build_system: board.startsWith('esp32') ? 'arduino-cli' : board.startsWith('stm32') ? 'stm32-gcc' : 'native-cpp',
    cpu: board.startsWith('stm32') ? 'cortex-m4' : undefined,
    linker_script: board.startsWith('stm32') ? 'linker.ld' : undefined,
    flash_address: board.startsWith('stm32') ? '0x08000000' : undefined,
    arduino_core: board.startsWith('esp32') ? 'esp32:esp32' : undefined,
    fqbn: board.startsWith('esp32') ? 'esp32:esp32:esp32' : undefined,
    board_manager_urls: board.startsWith('esp32') ? ['https://espressif.github.io/arduino-esp32/package_esp32_index.json'] : undefined,
    build_output: board.startsWith('esp32') ? `build/${projectSlug}` : board.startsWith('stm32') ? `build/${projectSlug}` : `build/${projectSlug}${process.platform === 'win32' ? '.exe' : ''}`
  };
  await writeIfMissing(path.join(projectDir, 'nav.toml'), writeToml(manifest));
  if (board.startsWith('esp32')) {
    await writeIfMissing(path.join(projectDir, `${projectSlug}.ino`), `#ifndef LED_BUILTIN\n#define LED_BUILTIN 2\n#endif\n\nvoid setup() {\n    pinMode(LED_BUILTIN, OUTPUT);\n}\n\nvoid loop() {\n    digitalWrite(LED_BUILTIN, HIGH);\n    delay(500);\n    digitalWrite(LED_BUILTIN, LOW);\n    delay(500);\n}\n`);
  } else {
    const mainPath = path.join(projectDir, 'src', 'main.c');
    const mainExists = await exists(mainPath);
    if (options.overwriteMain || !mainExists) {
      await fs.writeFile(mainPath, `#include <stdint.h>\n\nint main(void) {\n    volatile uint32_t ticks = 0;\n    while (1) {\n        ticks++;\n    }\n}\n`);
    }
    if (board.startsWith('stm32')) {
      await writeStm32RuntimeScaffold(projectDir);
    }
  }
}

function parseSetupArgs(values = [], defaultBoard = 'stm32f401') {
  const args = Array.isArray(values) ? values.filter(Boolean) : [values].filter(Boolean);
  const boardIndex = args.indexOf('--board');
  const board = boardIndex >= 0 ? args[boardIndex + 1] : defaultBoard;
  const projectArg = args.find((value, index) => value !== '--board' && index !== boardIndex + 1);
  return { projectArg, board };
}

async function setupProject(projectInput) {
  const { projectArg } = parseSetupArgs(projectInput);
  const projectDir = projectArg ? path.resolve(projectArg) : process.cwd();
  let manifest = await readProjectManifest(projectDir).catch(() => null);
  if (!manifest) {
    console.log(`no nav.toml/nav.json found; initializing from registry package nav/navhal@0.1.0`);
    await initializeFromRegistryPackage(projectDir, 'nav/navhal@0.1.0');
    manifest = await readProjectManifest(projectDir);
  }
  const resolution = await resolveProject(manifest);
  const installedToolchains = [];
  const installedPackages = [];

  for (const pkg of resolution.packages) {
    console.log(`resolving package ${pkg.namespace}/${pkg.name}@${pkg.version}`);
    const installed = await installPackageToProject(projectDir, pkg.spec, pkg.version);
    installedPackages.push(installed);
  }

  await materializeProjectRuntime(projectDir, manifest, installedPackages);

  for (const toolchain of resolution.toolchains) {
    console.log(`resolving ${toolchain.os}/${toolchain.arch} toolchain ${toolchain.name}@${toolchain.version}`);
    const installed = await installToolchain(toolchain);
    installedToolchains.push(installed);
  }

  const lock = {
    schema_version: 1,
    registry: API,
    project: manifest.name || path.basename(projectDir),
    board: manifest.board || null,
    generated_at: new Date().toISOString(),
    packages: installedPackages,
    toolchains: installedToolchains
  };
  await fs.mkdir(path.join(projectDir, '.nav'), { recursive: true });
  await fs.writeFile(path.join(projectDir, '.nav', 'lock.json'), JSON.stringify(lock, null, 2));
  if (needsArduino(manifest, lock)) {
    await ensureArduinoEsp32(manifest, lock);
  }
  console.log(`setup complete for ${lock.project}`);
  console.log(`packages: ${installedPackages.length}`);
  console.log(`toolchains: ${installedToolchains.map(item => `${item.name}@${item.version}`).join(', ') || '-'}`);
}

async function initializeFromRegistryPackage(projectDir, spec) {
  await fs.mkdir(projectDir, { recursive: true });
  const normalized = await resolvePackageSpec(spec);
  const installed = await installPackageToProject(projectDir, `${normalized.namespace}/${normalized.name}`, normalized.version);
  const filesDir = path.join(installed.path, 'files');
  await copyDirectoryOverlay(filesDir, projectDir, new Set(['.git', '.nav', 'build', 'node_modules']));
  const baseManifest = {
    ...(installed.manifest || {}),
    name: installed.manifest?.name || normalized.name,
    namespace: installed.manifest?.namespace || normalized.namespace,
    version: installed.manifest?.version || normalized.version,
    build_system: installed.manifest?.build_system || 'cmake',
    cmake_generator: installed.manifest?.cmake_generator || 'Ninja',
    cmake_sample: installed.manifest?.cmake_sample || 'hal_blink',
    build_output: installed.manifest?.build_output || 'build/navhal/samples/portable/01_hal_blink/hal_blink',
    board: installed.manifest?.board || 'nucleo_f401re',
    target: installed.manifest?.target || 'stm32f401',
    uploader: installed.manifest?.uploader || 'stlink',
    flash_address: installed.manifest?.flash_address || '0x08000000',
    toolchains: installed.manifest?.toolchains || ['nav-packager', 'arm-none-eabi', 'stlink']
  };
  await writeProjectManifest(projectDir, baseManifest);
  console.log(`base project: ${normalized.namespace}/${normalized.name}@${normalized.version}`);
}

async function addPackage(spec) {
  if (!spec) throw new Error('add requires <namespace/package[@version]>');
  const projectDir = process.cwd();
  const manifest = await readProjectManifest(projectDir);
  const normalized = await resolvePackageSpec(spec);
  const versionData = await getPackageVersion(normalized.namespace, normalized.name, normalized.version);
  mergeProjectHints(manifest, versionData.manifest || {});
  const dependencySpec = `${normalized.namespace}/${normalized.name}@${normalized.version}`;
  manifest.dependencies = unique([...(manifest.dependencies || []), dependencySpec]);
  await writeProjectManifest(projectDir, manifest);
  await installPackageToProject(projectDir, `${normalized.namespace}/${normalized.name}`, normalized.version);
  console.log(`added ${dependencySpec}`);
  await setupProject(projectDir);
}

async function buildProject(projectArg) {
  const projectDir = projectArg ? path.resolve(projectArg) : process.cwd();
  const manifest = await readProjectManifest(projectDir);
  const lockPath = path.join(projectDir, '.nav', 'lock.json');
  let lock = await readJson(lockPath).catch(() => null);
  if (!lock) {
    console.log('no lockfile found; running nav setup first');
    await setupProject(projectDir);
    lock = await readJson(lockPath);
  }
  const requestedToolchains = manifest.toolchains || [];
  const missingToolchains = requestedToolchains.filter(name => !lock.toolchains.some(toolchain => toolchain.name === name));
  if (missingToolchains.length > 0) {
    console.log(`lockfile is missing toolchains (${missingToolchains.join(', ')}); running nav setup first`);
    await setupProject(projectDir);
    lock = await readJson(lockPath);
  }
  const buildDir = path.join(projectDir, 'build');
  await fs.mkdir(buildDir, { recursive: true });
  const buildSystem = manifest.build_system || (await exists(path.join(projectDir, 'CMakeLists.txt')) ? 'cmake' : lock.toolchains.some(toolchain => toolchain.name === 'arduino-cli') ? 'arduino-cli' : lock.toolchains.some(toolchain => toolchain.name === 'arm-none-eabi') ? 'stm32-gcc' : lock.toolchains.some(toolchain => toolchain.name === 'mingw-gcc') ? 'native-cpp' : 'registry-script');
  const outputPath = path.resolve(projectDir, manifest.build_output || path.join('build', `${manifest.name || path.basename(projectDir)}${process.platform === 'win32' ? '.exe' : ''}`));
  await fs.mkdir(path.dirname(outputPath), { recursive: true });

  if (buildSystem === 'arduino-cli') {
    await buildArduino(projectDir, manifest, lock, outputPath);
  } else if (buildSystem === 'stm32-gcc') {
    await buildStm32Gcc(projectDir, manifest, lock, outputPath);
  } else if (buildSystem === 'native-cpp') {
    await buildNativeCpp(projectDir, manifest, lock, outputPath);
  } else if (buildSystem === 'cmake') {
    await buildCmake(projectDir, manifest, lock, outputPath);
  } else {
    throw new Error(`Unsupported build_system '${buildSystem}'. Use "arduino-cli", "stm32-gcc", "native-cpp", or "cmake".`);
  }

  const report = {
    schema_version: 1,
    project: manifest.name || path.basename(projectDir),
    board: manifest.board || null,
    target: manifest.target || null,
    registry: API,
    mode: buildSystem,
    toolchains: lock.toolchains,
    packages: lock.packages,
    output: outputPath,
    built_at: new Date().toISOString()
  };
  await fs.writeFile(path.join(buildDir, 'nav-build-report.json'), JSON.stringify(report, null, 2));
  console.log(`build complete: ${outputPath}`);
}

async function cleanProject(projectArg) {
  const projectDir = projectArg ? path.resolve(projectArg) : process.cwd();
  await fs.rm(path.join(projectDir, 'build'), { recursive: true, force: true });
  await fs.rm(path.join(projectDir, '.nav', 'lock.json'), { force: true });
  console.log('cleaned build outputs and lockfile');
}

async function runProject(rawArgs = []) {
  const { projectDir, options } = parseProjectOptions(rawArgs);
  await buildProject(projectDir);
  await uploadProject(['--project', projectDir, ...(options.port ? ['--port', options.port] : [])]);
}

async function uploadProject(rawArgs = []) {
  const { projectDir, options } = parseProjectOptions(rawArgs);
  const manifest = await readProjectManifest(projectDir);
  const lock = await readJson(path.join(projectDir, '.nav', 'lock.json')).catch(() => null);
  if (!lock) throw new Error('run nav setup/build before upload');
  const buildSystem = resolveBuildSystem(manifest, lock, projectDir);
  const uploader = resolveUploader(manifest, lock, buildSystem);
  console.log(`upload plan: ${manifest.name || path.basename(projectDir)} -> ${manifest.board || 'unknown board'}`);
  console.log(`registry-managed uploader: ${uploader}`);
  if (buildSystem === 'arduino-cli') {
    const ports = options.port ? [options.port] : await detectSerialPorts();
    console.log(`detected serial ports: ${ports.length ? ports.join(', ') : 'none'}`);
    const port = options.port || choosePort(ports);
    if (!port) throw new Error('No serial port detected. Connect the ESP32 or pass --port COMx.');
    const arduino = await getArduinoContext(lock);
    const fqbn = manifest.fqbn || 'esp32:esp32:esp32';
    await runCommand(arduino.cli, ['--config-file', arduino.config, 'upload', '-p', port, '--fqbn', fqbn, projectDir], projectDir);
    return;
  }
  if (buildSystem === 'stm32-gcc') {
    await uploadStm32(projectDir, manifest, lock);
    return;
  }
  if (buildSystem === 'cmake') {
    await uploadCmake(projectDir, manifest, lock);
    return;
  }
  throw new Error(`Upload is not implemented for build_system '${buildSystem}'.`);
}

async function monitorProject(rawArgs = []) {
  const { projectDir, options } = parseProjectOptions(rawArgs);
  const manifest = await readProjectManifest(projectDir).catch(() => ({}));
  const lock = await readJson(path.join(projectDir, '.nav', 'lock.json')).catch(() => null);
  if (!lock) throw new Error('run nav setup before monitor');
  console.log(`monitor plan: ${manifest.board || 'unknown board'}`);
  if (manifest.build_system === 'stm32-gcc') {
    const port = options.port;
    if (!port) throw new Error('STM32 monitor needs a serial VCP port. Pass --port COMx if your board exposes one.');
    await monitorSerialPort(port, Number(manifest.monitor_baud || 9600));
    return;
  }
  const ports = options.port ? [options.port] : await detectSerialPorts();
  const port = options.port || choosePort(ports);
  console.log(`detected serial ports: ${ports.length ? ports.join(', ') : 'none'}`);
  if (!port) throw new Error('No serial port detected. Connect the ESP32 or pass --port COMx.');
  const arduino = await getArduinoContext(lock);
  await runCommand(arduino.cli, ['--config-file', arduino.config, 'monitor', '-p', port, '-c', 'baudrate=115200'], projectDir);
}

async function pack(folder, rest) {
  if (!folder) throw new Error('pack requires <folder> --out <archive.navpkg>');
  const outIndex = rest.indexOf('--out');
  const out = outIndex >= 0 ? rest[outIndex + 1] : null;
  if (!out) throw new Error('pack requires --out <archive.navpkg>');
  const absoluteFolder = path.resolve(folder);
  const files = await collectFiles(absoluteFolder);
  const { manifest, manifestFile } = await readPackageManifest(absoluteFolder);
  const bundle = {
    schema_version: 1,
    packed_at: new Date().toISOString(),
    manifest_file: manifestFile,
    manifest,
    files: []
  };
  for (const file of files) {
    const rel = path.relative(absoluteFolder, file).replace(/\\/g, '/');
    bundle.files.push({
      path: rel,
      content_base64: (await fs.readFile(file)).toString('base64')
    });
  }
  const bytes = Buffer.from(JSON.stringify(bundle, null, 2));
  await fs.mkdir(path.dirname(path.resolve(out)), { recursive: true });
  await fs.writeFile(out, bytes);
  console.log(`packed ${files.length} files -> ${out}`);
  console.log(`manifest ${manifestFile}`);
  console.log(`sha256 ${hash(bytes)}`);
}

async function publish(namespace, rest) {
  const [name, version, archivePath, ...options] = rest;
  if (!namespace || !name || !version || !archivePath) {
    throw new Error('publish requires <namespace> <package> <version> <archive>');
  }
  const archive = await fs.readFile(archivePath);
  let manifest = {};
  try {
    const parsed = JSON.parse(archive.toString('utf8'));
    manifest = parsed.manifest || {};
  } catch {
    manifest = { name, version };
  }
  const changelog = await resolveChangelog(options, archivePath, manifest);
  const expected_sha256 = hash(archive);
  const init = await request('/publish/init', {
    method: 'POST',
    body: JSON.stringify({
      namespace,
      package: name,
      version,
      file_name: path.basename(archivePath),
      content_type: 'application/vnd.nav.package+json',
      size_bytes: archive.length,
      expected_sha256
    })
  });
  await request(`/publish/${init.session.id}/upload`, {
    method: 'PUT',
    headers: { 'Content-Type': 'application/octet-stream' },
    body: archive
  });
  const complete = await request(`/publish/${init.session.id}/complete`, {
    method: 'POST',
    body: JSON.stringify({
      manifest: { ...manifest, name, namespace, version, ...(changelog ? { changelog } : {}) },
      readme: manifest.description || `${namespace}/${name}@${version}`,
      changelog
    })
  });
  console.log(`published ${namespace}/${name}@${complete.version.version}`);
}

async function resolveChangelog(options, archivePath, manifest) {
  const fileIndex = options.indexOf('--changelog');
  if (fileIndex >= 0) {
    const changelogPath = options[fileIndex + 1];
    if (!changelogPath) throw new Error('--changelog requires a markdown file path');
    return (await fs.readFile(path.resolve(changelogPath), 'utf8')).trim();
  }
  const textIndex = options.indexOf('--changelog-text');
  if (textIndex >= 0) {
    return String(options[textIndex + 1] || '').trim();
  }
  if (manifest.changelog) {
    return String(manifest.changelog).trim();
  }
  const candidates = [
    path.resolve('CHANGELOG.md'),
    path.resolve('changelog.md'),
    path.join(path.dirname(path.resolve(archivePath)), 'CHANGELOG.md'),
    path.join(path.dirname(path.resolve(archivePath)), 'changelog.md')
  ];
  for (const candidate of candidates) {
    try {
      return (await fs.readFile(candidate, 'utf8')).trim();
    } catch {}
  }
  return '';
}

async function install(spec) {
  const normalized = await resolvePackageSpec(spec);
  const bytes = Buffer.from(await request(`/packages/${normalized.namespace}/${normalized.name}/versions/${normalized.version}/download`));
  const targetDir = path.join(CACHE, normalized.namespace, normalized.name, normalized.version);
  await fs.mkdir(targetDir, { recursive: true });
  const out = path.join(targetDir, `${normalized.name}-${normalized.version}.navpkg`);
  await fs.writeFile(out, bytes);
  console.log(`installed ${normalized.namespace}/${normalized.name}@${normalized.version}`);
  console.log(out);
  console.log(`sha256 ${hash(bytes)}`);
  return { ...normalized, archive: out, sha256: hash(bytes) };
}

async function toolchainCommand(subcommand, rest) {
  if (subcommand === 'list') {
    const data = await request('/toolchains');
    printTable(
      [
        { key: 'toolchain', header: 'Toolchain', min: 34, max: 54 },
        { key: 'vendor', header: 'Vendor', min: 10, max: 18 },
        { key: 'platform', header: 'Platform', min: 12, max: 14 },
        { key: 'official', header: 'Official source', min: 24, max: 70 }
      ],
      data.toolchains.map(tc => ({
        toolchain: `${tc.name}@${tc.version}`,
        vendor: `${tc.vendor_kind}/${tc.vendor}`,
        platform: tc.os && tc.arch ? `${tc.os}/${tc.arch}` : 'metadata',
        official: tc.homepage_url || tc.upstream_url || ''
      }))
    );
    return;
  }
  if (subcommand === 'publish') {
    const [metadataPath, archivePath] = rest;
    if (!metadataPath || !archivePath) throw new Error('toolchain publish requires <metadata.json> <archive.zip>');
    const metadata = JSON.parse(await fs.readFile(metadataPath, 'utf8'));
    const archive = await fs.readFile(archivePath);
    const data = await request('/toolchains/publish', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/octet-stream',
        'X-Nav-Toolchain-Metadata': JSON.stringify(metadata)
      },
      body: archive
    });
    console.log(`published toolchain ${data.vendor.kind}/${data.vendor.name}/${data.toolchain.name}@${data.version.version}`);
    return;
  }
  throw new Error('toolchain command must be list or publish');
}

async function resolveProject(manifest) {
  const packageMap = new Map();
  const toolchainSet = new Set(manifest.toolchains || []);
  const queue = [...(manifest.dependencies || [])];
  const seen = new Set();

  while (queue.length > 0) {
    const rawSpec = queue.shift();
    const normalized = await resolvePackageSpec(rawSpec);
    const key = `${normalized.namespace}/${normalized.name}@${normalized.version}`;
    if (seen.has(key)) continue;
    seen.add(key);
    packageMap.set(key, { ...normalized, spec: `${normalized.namespace}/${normalized.name}` });
    const versionData = await getPackageVersion(normalized.namespace, normalized.name, normalized.version);
    const pkgManifest = versionData.manifest || {};
    for (const tc of pkgManifest.toolchains || []) toolchainSet.add(tc);
    for (const dep of pkgManifest.dependencies || []) queue.push(dep);
  }

  const allToolchains = (await request('/toolchains')).toolchains;
  const platform = currentPlatform();
  const resolvedToolchains = [];
  for (const name of toolchainSet) {
    const match = allToolchains.find(tc => tc.name === name && tc.os === platform.os && tc.arch === platform.arch)
      || allToolchains.find(tc => tc.name === name && tc.os === platform.os)
      || allToolchains.find(tc => tc.name === name);
    if (!match) throw new Error(`No registry toolchain found for ${name}`);
    resolvedToolchains.push(match);
  }

  return {
    packages: [...packageMap.values()],
    toolchains: resolvedToolchains
  };
}

async function resolvePackageSpec(spec) {
  const parsed = parseSpec(spec);
  if (parsed.version) return parsed;
  const versions = await request(`/packages/${parsed.namespace}/${parsed.name}/versions`);
  const version = versions.versions[0]?.version;
  if (!version) throw new Error(`No package version found for ${parsed.namespace}/${parsed.name}`);
  return { ...parsed, version };
}

async function getPackageVersion(namespace, name, version) {
  const data = await request(`/packages/${namespace}/${name}`);
  const match = data.versions.find(item => item.version === version);
  if (!match) throw new Error(`Version ${namespace}/${name}@${version} was not found`);
  return match;
}

async function installPackageToProject(projectDir, spec, versionOverride = null) {
  const normalized = versionOverride ? { ...parseSpec(spec), version: versionOverride } : await resolvePackageSpec(spec);
  const registryVersion = await getPackageVersion(normalized.namespace, normalized.name, normalized.version).catch(() => null);
  const registrySha = registryVersion?.sha256 || registryVersion?.checksum_sha256;
  const packageDir = path.join(projectDir, '.nav', 'packages', normalized.namespace, normalized.name, normalized.version);
  const installPath = path.join(packageDir, 'install.json');
  const existing = await readJson(installPath).catch(() => null);
  if (
    existing?.namespace === normalized.namespace
    && existing?.name === normalized.name
    && existing?.version === normalized.version
    && (!registrySha || existing.sha256 === registrySha)
  ) {
    const filesDir = path.join(packageDir, 'files');
    try {
      await fs.access(filesDir);
      console.log(`  using cached package ${normalized.namespace}/${normalized.name}@${normalized.version}`);
      return {
        namespace: normalized.namespace,
        name: normalized.name,
        version: normalized.version,
        path: packageDir,
        sha256: existing.sha256,
        manifest: existing.manifest || registryVersion?.manifest || null
      };
    } catch {
      // Re-download if the cache metadata exists but extracted files are missing.
    }
  } else if (existing?.namespace === normalized.namespace && existing?.name === normalized.name && existing?.version === normalized.version) {
    console.log(`  refreshing cached package ${normalized.namespace}/${normalized.name}@${normalized.version}`);
  }

  const bytes = Buffer.from(await request(`/packages/${normalized.namespace}/${normalized.name}/versions/${normalized.version}/download`));
  const archiveHash = hash(bytes);
  if (registrySha && archiveHash !== registrySha) {
    throw new Error(`Package checksum mismatch for ${normalized.namespace}/${normalized.name}@${normalized.version}`);
  }
  console.log(`  downloaded package archive (${formatBytes(bytes.length)})`);
  console.log(`  verified sha256 ${archiveHash.slice(0, 24)}...`);
  await fs.rm(path.join(packageDir, 'files'), { recursive: true, force: true });
  await fs.mkdir(packageDir, { recursive: true });
  await fs.writeFile(path.join(packageDir, `${normalized.name}-${normalized.version}.navpkg`), bytes);
  let bundle = null;
  try {
    bundle = JSON.parse(bytes.toString('utf8'));
  } catch {
    // Non-navpkg archives are still cached and checksum-verified by the prototype.
  }
  await fs.writeFile(installPath, JSON.stringify({
    namespace: normalized.namespace,
    name: normalized.name,
    version: normalized.version,
    sha256: archiveHash,
    manifest: bundle?.manifest || registryVersion?.manifest || null,
    installed_at: new Date().toISOString()
  }, null, 2));

  if (Array.isArray(bundle?.files)) {
    const extractDir = path.join(packageDir, 'files');
    for (const file of bundle.files) {
      const out = path.join(extractDir, file.path);
      await fs.mkdir(path.dirname(out), { recursive: true });
      await fs.writeFile(out, Buffer.from(file.content_base64, 'base64'));
    }
  }

  return {
    namespace: normalized.namespace,
    name: normalized.name,
    version: normalized.version,
    path: packageDir,
    sha256: archiveHash,
    manifest: bundle?.manifest || registryVersion?.manifest || null
  };
}

async function installToolchain(toolchain) {
  const installDir = path.join(TOOLCHAIN_HOME, toolchain.name, toolchain.version, `${toolchain.os}-${toolchain.arch}`);
  const archiveFormat = toolchain.archive_format || 'zip';
  const archivePath = path.join(installDir, `archive.${archiveFormat}`);
  const cached = await readToolchainInstall(installDir, toolchain);
  if (cached) {
    console.log(`  using cached ${toolchain.name}@${toolchain.version} (${installDir})`);
    return cached;
  }

  await fs.mkdir(path.join(installDir, 'bin'), { recursive: true });
  let bytes = await readCachedArchive(archivePath, toolchain.sha256);
  if (bytes) {
    console.log(`  repairing ${toolchain.name}@${toolchain.version} from cached archive`);
  } else {
    console.log(`  downloading ${toolchain.name}@${toolchain.version} from registry blob`);
    bytes = Buffer.from(await request(`/toolchains/${toolchain.name}/versions/${toolchain.version}/${toolchain.os}/${toolchain.arch}/download`));
  }
  const archiveHash = hash(bytes);
  if (toolchain.sha256 && toolchain.sha256 !== archiveHash) {
    throw new Error(`Toolchain checksum mismatch for ${toolchain.name}@${toolchain.version}`);
  }
  console.log(`  verified sha256 ${archiveHash.slice(0, 24)}...`);
  await fs.writeFile(archivePath, bytes);
  if (toolchain.provenance?.real_artifact) {
    await extractToolchainArchive(archivePath, archiveFormat, installDir);
  }
  await fs.writeFile(path.join(installDir, 'manifest.json'), JSON.stringify({
    name: toolchain.name,
    version: toolchain.version,
    vendor: toolchain.vendor,
    vendor_kind: toolchain.vendor_kind,
    os: toolchain.os,
    arch: toolchain.arch,
    sha256: archiveHash,
    signature: toolchain.signature,
    upstream_url: toolchain.upstream_url,
    archive_format: archiveFormat,
    installed_at: new Date().toISOString()
  }, null, 2));
  const shimName = process.platform === 'win32' ? `${toolchain.name}.cmd` : toolchain.name;
  const shimPath = path.join(installDir, 'bin', shimName);
  const shimBody = process.platform === 'win32'
    ? `@echo off\r\necho ${toolchain.name} ${toolchain.version} from Nav test cache\r\n`
    : `#!/usr/bin/env sh\necho "${toolchain.name} ${toolchain.version} from Nav test cache"\n`;
  await fs.writeFile(shimPath, shimBody);
  console.log(`  installed into ${installDir}`);
  return {
    name: toolchain.name,
    version: toolchain.version,
    vendor: toolchain.vendor,
    vendor_kind: toolchain.vendor_kind,
    os: toolchain.os,
    arch: toolchain.arch,
    path: installDir,
    sha256: archiveHash
  };
}

async function readCachedArchive(archivePath, expectedSha256) {
  try {
    const bytes = await fs.readFile(archivePath);
    if (!expectedSha256 || hash(bytes) === expectedSha256) return bytes;
  } catch {
    // Missing archive; download below.
  }
  return null;
}

async function readToolchainInstall(installDir, toolchain) {
  const manifestPath = path.join(installDir, 'manifest.json');
  const manifest = await readJson(manifestPath).catch(() => null);
  if (!manifest) return null;
  if (manifest.sha256 !== toolchain.sha256) return null;
  if (manifest.name !== toolchain.name || manifest.version !== toolchain.version) return null;
  const archiveFormat = toolchain.archive_format || manifest.archive_format || 'zip';
  const archivePath = path.join(installDir, `archive.${archiveFormat}`);
  try {
    await fs.access(archivePath);
  } catch {
    return null;
  }
  if (toolchain.provenance?.real_artifact) {
    const extractDir = path.join(installDir, 'extract');
    const expectedTool = await expectedToolchainExecutable(toolchain);
    if (expectedTool && !(await findFile(extractDir, expectedTool))) return null;
    if (toolchain.name === 'arm-none-eabi' && !(await findFile(extractDir, process.platform === 'win32' ? /^cc1\.exe$/i : /^cc1$/))) return null;
  }
  return {
    name: toolchain.name,
    version: toolchain.version,
    vendor: toolchain.vendor,
    vendor_kind: toolchain.vendor_kind,
    os: toolchain.os,
    arch: toolchain.arch,
    path: installDir,
    sha256: manifest.sha256
  };
}

async function expectedToolchainExecutable(toolchain) {
  const win = process.platform === 'win32';
  if (toolchain.name === 'arm-none-eabi') return win ? /^arm-none-eabi-gcc\.exe$/i : /^arm-none-eabi-gcc$/;
  if (toolchain.name === 'stlink') return win ? /^st-flash\.exe$/i : /^st-flash$/;
  if (toolchain.name === 'arduino-cli') return win ? /^arduino-cli\.exe$/i : /^arduino-cli$/;
  return null;
}

async function extractToolchainArchive(archivePath, archiveFormat, installDir) {
  const extractDir = path.join(installDir, 'extract');
  await fs.rm(extractDir, { recursive: true, force: true });
  await fs.mkdir(extractDir, { recursive: true });
  if (archiveFormat === 'zip') {
    await runCommand('powershell', ['-NoProfile', '-Command', `Expand-Archive -LiteralPath '${archivePath.replace(/'/g, "''")}' -DestinationPath '${extractDir.replace(/'/g, "''")}' -Force`], process.cwd());
    return;
  }
  if (archiveFormat === 'tar.gz') {
    await runCommand('tar', ['-xzf', archivePath, '-C', extractDir], process.cwd());
    return;
  }
  throw new Error(`Unsupported toolchain archive format: ${archiveFormat}`);
}

async function ensureArduinoEsp32(manifest, lock) {
  const arduino = await getArduinoContext(lock);
  await writeArduinoConfig(arduino.config, manifest);
  console.log('checking Arduino package index');
  await runCommand(arduino.cli, ['--config-file', arduino.config, 'core', 'update-index'], process.cwd());
  const core = manifest.arduino_core || 'esp32:esp32';
  const installed = await runCapture(arduino.cli, ['--config-file', arduino.config, 'core', 'list']);
  if (installed.includes(core)) {
    console.log(`Arduino core already installed: ${core}`);
    return;
  }
  console.log(`installing Arduino core ${core}`);
  await runCommand(arduino.cli, ['--config-file', arduino.config, 'core', 'install', core], process.cwd());
}

async function buildArduino(projectDir, manifest, lock, outputPath) {
  await ensureArduinoEsp32(manifest, lock);
  const arduino = await getArduinoContext(lock);
  const fqbn = manifest.fqbn || 'esp32:esp32:esp32';
  const outputDir = outputPath;
  await fs.mkdir(outputDir, { recursive: true });
  console.log('build system: arduino-cli');
  console.log(`fqbn: ${fqbn}`);
  console.log(`output: ${outputDir}`);
  await runCommand(arduino.cli, ['--config-file', arduino.config, 'compile', '--fqbn', fqbn, '--output-dir', outputDir, projectDir], projectDir);
}

async function getArduinoContext(lock) {
  const arduinoTool = lock.toolchains.find(toolchain => toolchain.name === 'arduino-cli');
  if (!arduinoTool) throw new Error('Project requires arduino-cli but it is missing from the lockfile. Run nav setup.');
  const cliName = process.platform === 'win32' ? /^arduino-cli\.exe$/i : /^arduino-cli$/;
  const cli = await findFile(arduinoTool.path, cliName);
  if (!cli) throw new Error(`arduino-cli executable was not found under ${arduinoTool.path}`);
  const root = path.join(HOME, 'arduino');
  return {
    cli,
    root,
    config: path.join(root, 'arduino-cli.yaml'),
    data: path.join(root, 'data'),
    downloads: path.join(root, 'downloads'),
    user: path.join(root, 'user'),
    cache: path.join(root, 'cache')
  };
}

async function writeArduinoConfig(configPath, manifest) {
  const root = path.dirname(configPath);
  const dirs = {
    data: path.join(root, 'data'),
    downloads: path.join(root, 'downloads'),
    user: path.join(root, 'user'),
    cache: path.join(root, 'cache')
  };
  await Promise.all(Object.values(dirs).map(dir => fs.mkdir(dir, { recursive: true })));
  const urls = manifest.board_manager_urls?.length
    ? manifest.board_manager_urls
    : [`${API}/mirrors/arduino/esp32/package_esp32_index.json`];
  const yaml = [
    'board_manager:',
    '  additional_urls:',
    ...urls.map(url => `    - ${url}`),
    'directories:',
    `  data: ${toPosixPath(dirs.data)}`,
    `  downloads: ${toPosixPath(dirs.downloads)}`,
    `  user: ${toPosixPath(dirs.user)}`,
    'build_cache:',
    `  path: ${toPosixPath(dirs.cache)}`,
    'metrics:',
    '  enabled: false',
    ''
  ].join('\n');
  await fs.mkdir(path.dirname(configPath), { recursive: true });
  await fs.writeFile(configPath, yaml);
}

function needsArduino(manifest, lock) {
  return manifest.build_system === 'arduino-cli' || lock.toolchains.some(toolchain => toolchain.name === 'arduino-cli');
}

async function writeStm32RuntimeScaffold(projectDir) {
  await fs.mkdir(path.join(projectDir, 'startup'), { recursive: true });
  await writeIfMissing(path.join(projectDir, 'linker.ld'), STM32_LINKER_LD);
  await writeIfMissing(path.join(projectDir, 'startup', 'startup.c'), STM32_STARTUP_C);
}

async function writeIfMissing(file, content) {
  try {
    await fs.access(file);
  } catch {
    await fs.mkdir(path.dirname(file), { recursive: true });
    await fs.writeFile(file, content);
  }
}

async function materializeProjectRuntime(projectDir, manifest, installedPackages) {
  if (manifest.build_system !== 'stm32-gcc' && !String(manifest.board || '').startsWith('stm32')) return;
  await writeStm32RuntimeScaffold(projectDir);

  for (const pkg of installedPackages) {
    const root = path.join(pkg.path, 'files');
    await copyIfMissing(path.join(root, 'linker.ld'), path.join(projectDir, manifest.linker_script || 'linker.ld'));
    await copyDirectoryIfMissing(path.join(root, 'startup'), path.join(projectDir, 'startup'));

    const projectMain = path.join(projectDir, 'src', 'main.c');
    const packageMain = path.join(root, 'src', 'main.c');
    if (await isDefaultGeneratedMain(projectMain)) {
      await fs.copyFile(packageMain, projectMain).catch(() => null);
    }
  }
}

async function copyIfMissing(from, to) {
  try {
    await fs.access(to);
    return;
  } catch {}
  try {
    await fs.mkdir(path.dirname(to), { recursive: true });
    await fs.copyFile(from, to);
  } catch {}
}

async function copyDirectoryIfMissing(from, to) {
  const existing = await fs.readdir(to).catch(() => []);
  if (existing.length > 0) return;
  try {
    await copyDirectory(from, to);
  } catch {}
}

async function copyDirectoryOverlay(from, to, ignoredDirectories = new Set()) {
  const entries = await fs.readdir(from, { withFileTypes: true }).catch(() => []);
  await fs.mkdir(to, { recursive: true });
  for (const entry of entries) {
    if (entry.isDirectory() && ignoredDirectories.has(entry.name)) continue;
    const source = path.join(from, entry.name);
    const target = path.join(to, entry.name);
    if (entry.isDirectory()) {
      await copyDirectoryOverlay(source, target, ignoredDirectories);
    } else if (entry.isFile()) {
      try {
        await fs.access(target);
      } catch {
        await fs.mkdir(path.dirname(target), { recursive: true });
        await fs.copyFile(source, target);
      }
    }
  }
}

async function isDefaultGeneratedMain(file) {
  try {
    const content = await fs.readFile(file, 'utf8');
    return content.includes('volatile uint32_t ticks') && content.includes('ticks++');
  } catch {
    return false;
  }
}

function mergeProjectHints(manifest, dependencyManifest) {
  for (const key of ['build_system', 'arduino_core', 'fqbn', 'build_output']) {
    if (!manifest[key] && dependencyManifest[key]) manifest[key] = dependencyManifest[key];
  }
  if ((!manifest.board_manager_urls || manifest.board_manager_urls.length === 0) && dependencyManifest.board_manager_urls) {
    manifest.board_manager_urls = dependencyManifest.board_manager_urls;
  }
  if (dependencyManifest.toolchains?.length) {
    manifest.toolchains = unique([...(manifest.toolchains || []), ...dependencyManifest.toolchains]);
  }
}

async function buildNativeCpp(projectDir, manifest, lock, outputPath) {
  const compiler = await findExecutable(process.platform === 'win32' ? ['g++.exe', 'clang++.exe'] : ['g++', 'clang++']);
  if (!compiler) {
    throw new Error('No native C++ compiler found. The registry resolved mingw-gcc, but this machine has no g++/clang++ available on PATH.');
  }

  const sources = await collectSourceFiles(path.join(projectDir, 'src'));
  if (sources.length === 0) {
    throw new Error('No C/C++ source files found under src/');
  }

  const includeDirs = unique([
    path.join(projectDir, 'src'),
    ...lock.packages.flatMap(pkg => [
      path.join(pkg.path, 'files'),
      path.join(pkg.path, 'files', 'include'),
      path.join(pkg.path, 'files', 'src')
    ])
  ]);

  console.log(`build system: native-cpp`);
  console.log(`compiler: ${compiler}`);
  console.log(`sources: ${sources.length}`);
  console.log(`output: ${outputPath}`);

  const args = [
    '-std=c++17',
    '-O2',
    '-Wall',
    '-Wextra',
    ...includeDirs.map(dir => `-I${dir}`),
    ...sources,
    '-o',
    outputPath
  ];

  await runCommand(compiler, args, projectDir);
}

async function buildCmake(projectDir, manifest, lock, outputPath) {
  const cmake = await findExecutable(process.platform === 'win32' ? ['cmake.exe'] : ['cmake']);
  if (!cmake) {
    throw new Error('CMake is required to build this registry base project. Install CMake or publish a Nav-managed cmake toolchain.');
  }
  const generator = manifest.cmake_generator || 'Ninja';
  const generatorExe = generator.toLowerCase() === 'ninja'
    ? await findExecutable(process.platform === 'win32' ? ['ninja.exe'] : ['ninja'])
    : null;
  if (generator.toLowerCase() === 'ninja' && !generatorExe) {
    throw new Error('Ninja is required for this CMake project. Install Ninja or change cmake_generator in nav.toml.');
  }

  const armToolchain = lock.toolchains.find(item => item.name === 'arm-none-eabi');
  const gcc = armToolchain ? await findRegistryTool(armToolchain, process.platform === 'win32' ? /^arm-none-eabi-gcc\.exe$/i : /^arm-none-eabi-gcc$/) : null;
  const objcopy = armToolchain ? await findRegistryTool(armToolchain, process.platform === 'win32' ? /^arm-none-eabi-objcopy\.exe$/i : /^arm-none-eabi-objcopy$/) : null;
  const toolchainBin = gcc ? path.dirname(gcc) : null;
  const buildDir = path.resolve(projectDir, manifest.cmake_build_dir || 'build/navhal');
  const output = path.resolve(projectDir, outputPath);
  const env = {
    ...process.env,
    PATH: toolchainBin ? `${toolchainBin}${path.delimiter}${process.env.PATH || ''}` : process.env.PATH
  };
  const configureArgs = [
    '-S',
    projectDir,
    '-B',
    buildDir,
    '-G',
    generator,
    `-DSAMPLE=${manifest.cmake_sample || 'hal_blink'}`
  ];

  console.log('build system: cmake');
  console.log(`cmake: ${cmake}`);
  console.log(`generator: ${generator}`);
  if (toolchainBin) console.log(`toolchain bin: ${toolchainBin}`);
  console.log(`sample: ${manifest.cmake_sample || 'hal_blink'}`);
  console.log(`build dir: ${buildDir}`);
  console.log(`output: ${output}`);

  await runCommand(cmake, configureArgs, projectDir, { env });
  await runCommand(cmake, ['--build', buildDir], projectDir, { env });
  if (objcopy) {
    const binPath = output.endsWith('.bin') ? output : `${output}.bin`;
    await runCommand(objcopy, ['-O', 'binary', output, binPath], projectDir, { env });
  }
}

async function buildStm32Gcc(projectDir, manifest, lock, outputPath) {
  const toolchain = lock.toolchains.find(item => item.name === 'arm-none-eabi');
  if (!toolchain) throw new Error('STM32 build requires arm-none-eabi from the registry lockfile. Run nav setup.');
  const gcc = await findRegistryTool(toolchain, process.platform === 'win32' ? /^arm-none-eabi-gcc\.exe$/i : /^arm-none-eabi-gcc$/);
  const objcopy = await findRegistryTool(toolchain, process.platform === 'win32' ? /^arm-none-eabi-objcopy\.exe$/i : /^arm-none-eabi-objcopy$/);
  const sizeTool = await findFile(toolchain.path, process.platform === 'win32' ? /^arm-none-eabi-size\.exe$/i : /^arm-none-eabi-size$/);
  const packageSourceRoots = lock.packages.map(pkg => path.join(pkg.path, 'files'));
  const sources = unique([
    ...await collectSourceFiles(path.join(projectDir, 'src')),
    ...await collectSourceFiles(path.join(projectDir, 'startup')),
    ...await collectPackageSources(packageSourceRoots)
  ]).filter(file => /\.(c|s|S)$/i.test(file));
  const filteredSources = sources.filter(file => !file.includes(`${path.sep}build${path.sep}`) && !file.endsWith('.navpkg'));
  if (filteredSources.length === 0) throw new Error('No STM32 source files found. Add .c/.s files under src/ or startup/.');
  const linkerScript = path.resolve(projectDir, manifest.linker_script || 'linker.ld');
  await fs.access(linkerScript).catch(() => {
    throw new Error(`Linker script not found: ${linkerScript}`);
  });
  const includeDirs = unique([
    path.join(projectDir, 'src'),
    path.join(projectDir, 'include'),
    ...lock.packages.flatMap(pkg => [
      path.join(pkg.path, 'files', 'include'),
      path.join(pkg.path, 'files', 'src'),
      ...packageIncludeDirs(pkg)
    ])
  ]);
  const cpu = manifest.cpu || 'cortex-m4';
  const cflags = [
    `-mcpu=${cpu}`,
    '-mthumb',
    '-ffreestanding',
    '-fdata-sections',
    '-ffunction-sections',
    '-Wall',
    '-Wextra',
    '-Os',
    ...includeDirs.map(dir => `-I${dir}`),
    ...(manifest.defines || []).map(item => `-D${item}`)
  ];
  const elfPath = outputPath.endsWith('.elf') ? outputPath : `${outputPath}.elf`;
  const binPath = outputPath.endsWith('.bin') ? outputPath : `${outputPath}.bin`;
  console.log('build system: stm32-gcc');
  console.log(`compiler: ${gcc}`);
  console.log(`linker: ${linkerScript}`);
  console.log(`sources: ${filteredSources.length}`);
  console.log(`elf: ${elfPath}`);
  console.log(`bin: ${binPath}`);
  await runCommand(gcc, [
    ...cflags,
    ...filteredSources,
    '-nostdlib',
    '-Wl,--gc-sections',
    `-Wl,-T,${linkerScript}`,
    '-o',
    elfPath
  ], projectDir);
  if (sizeTool) await runCommand(sizeTool, [elfPath], projectDir);
  await runCommand(objcopy, ['-O', 'binary', elfPath, binPath], projectDir);
}

async function uploadStm32(projectDir, manifest, lock) {
  const stlink = lock.toolchains.find(item => item.name === 'stlink');
  if (!stlink) throw new Error('STM32 upload requires stlink from the registry lockfile. Run nav setup.');
  const stFlash = await findRegistryTool(stlink, process.platform === 'win32' ? /^st-flash\.exe$/i : /^st-flash$/);
  await prepareStlinkConfig(stlink);
  const outputPath = path.resolve(projectDir, manifest.build_output || path.join('build', `${manifest.name || path.basename(projectDir)}`));
  const binPath = outputPath.endsWith('.bin') ? outputPath : `${outputPath}.bin`;
  await fs.access(binPath).catch(() => {
    throw new Error(`Build output not found: ${binPath}. Run nav build first.`);
  });
  const address = manifest.flash_address || '0x08000000';
  console.log(`flasher: ${stFlash}`);
  console.log(`binary: ${binPath}`);
  console.log(`flash address: ${address}`);
  await runCommand(stFlash, ['write', binPath, address], projectDir);
}

async function uploadCmake(projectDir, manifest, lock) {
  const uploader = resolveUploader(manifest, lock, 'cmake');
  if (uploader === 'stlink') {
    const stlink = lock.toolchains.find(item => item.name === 'stlink');
    if (!stlink) throw new Error('CMake upload selected stlink, but stlink is not in the registry lockfile. Run nav setup.');
    const stFlash = await findRegistryTool(stlink, process.platform === 'win32' ? /^st-flash\.exe$/i : /^st-flash$/);
    await prepareStlinkConfig(stlink);
    const outputPath = path.resolve(projectDir, manifest.build_output || 'build/navhal/samples/portable/01_hal_blink/hal_blink');
    const binPath = outputPath.endsWith('.bin') ? outputPath : `${outputPath}.bin`;
    await fs.access(binPath).catch(() => {
      throw new Error(`Build output not found: ${binPath}. Run nav build first.`);
    });
    const address = manifest.flash_address || '0x08000000';
    console.log(`flasher: ${stFlash}`);
    console.log(`binary: ${binPath}`);
    console.log(`flash address: ${address}`);
    await runCommand(stFlash, ['write', binPath, address], projectDir);
    return;
  }
  throw new Error('CMake upload needs a registry-managed uploader. Add uploader = "stlink" or another supported uploader to nav.toml.');
}

function resolveBuildSystem(manifest, lock, projectDir) {
  if (manifest.build_system) return manifest.build_system;
  if (existsSync(path.join(projectDir, 'CMakeLists.txt'))) return 'cmake';
  if (lock.toolchains.some(toolchain => toolchain.name === 'arduino-cli')) return 'arduino-cli';
  if (lock.toolchains.some(toolchain => toolchain.name === 'arm-none-eabi')) return 'stm32-gcc';
  if (lock.toolchains.some(toolchain => toolchain.name === 'mingw-gcc')) return 'native-cpp';
  return 'registry-script';
}

function resolveUploader(manifest, lock, buildSystem) {
  if (manifest.uploader) return manifest.uploader;
  if (buildSystem === 'arduino-cli') return 'arduino-cli upload';
  if (manifest.target === 'esp32') return 'esptool';
  if (String(manifest.target || '').startsWith('stm32')) return 'stlink';
  if (lock.toolchains.some(toolchain => toolchain.name === 'stlink')) return 'stlink';
  return 'board uploader';
}

async function prepareStlinkConfig(stlink) {
  if (process.platform !== 'win32') return;
  const sourceConfig = await findDirectory(stlink.path, 'chips');
  if (!sourceConfig) return;
  const expectedConfig = 'C:\\Program Files (x86)\\stlink\\config\\chips';
  try {
    await fs.access(path.join(expectedConfig, 'F401xD_xE.chip'));
    return;
  } catch {
    // Continue below.
  }
  console.log(`preparing ST-Link chip database: ${expectedConfig}`);
  try {
    await copyDirectory(sourceConfig, expectedConfig);
  } catch (error) {
    throw new Error(`ST-Link chip database is missing and could not be prepared at ${expectedConfig}. Run PowerShell as Administrator once or use ST's official STM32CubeProgrammer. Details: ${error.message}`);
  }
}

async function findDirectory(dir, name) {
  const entries = await fs.readdir(dir, { withFileTypes: true }).catch(() => []);
  for (const entry of entries) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory() && entry.name.toLowerCase() === name.toLowerCase()) return full;
    if (entry.isDirectory()) {
      const nested = await findDirectory(full, name);
      if (nested) return nested;
    }
  }
  return null;
}

async function copyDirectory(source, destination) {
  await fs.mkdir(destination, { recursive: true });
  const entries = await fs.readdir(source, { withFileTypes: true });
  for (const entry of entries) {
    const from = path.join(source, entry.name);
    const to = path.join(destination, entry.name);
    if (entry.isDirectory()) {
      await copyDirectory(from, to);
    } else if (entry.isFile()) {
      await fs.copyFile(from, to);
    }
  }
}

async function collectPackageSources(packageSourceRoots) {
  const sources = [];
  for (const root of packageSourceRoots) {
    const install = await readJson(path.join(path.dirname(root), 'install.json')).catch(() => null);
    if (install?.manifest?.auto_compile === false) continue;
    sources.push(...await collectSourceFiles(path.join(root, 'src')));
  }
  return sources.filter(file => path.basename(file).toLowerCase() !== 'main.c');
}

function packageIncludeDirs(pkg) {
  const manifest = pkg.manifest || {};
  const fromManifest = Array.isArray(manifest.include_paths) ? manifest.include_paths : [];
  return fromManifest.map(item => path.join(pkg.path, 'files', item));
}

async function findRegistryTool(toolchain, pattern) {
  const tool = await findFile(toolchain.path, pattern);
  if (!tool) {
    throw new Error(`Expected registry-managed executable ${pattern} under ${toolchain.path}. Run nav clean then nav setup to reinstall from the registry blob.`);
  }
  return tool;
}

async function collectSourceFiles(dir) {
  const entries = await fs.readdir(dir, { withFileTypes: true }).catch(() => []);
  const files = [];
  for (const entry of entries) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) files.push(...await collectSourceFiles(full));
    if (entry.isFile() && /\.(c|cc|cpp|cxx)$/i.test(entry.name)) files.push(full);
  }
  return files;
}

async function findExecutable(names) {
  const pathParts = (process.env.PATH || '').split(path.delimiter).filter(Boolean);
  for (const dir of pathParts) {
    for (const name of names) {
      const candidate = path.join(dir, name);
      try {
        await fs.access(candidate);
        return candidate;
      } catch {
        // keep searching
      }
    }
  }
  return null;
}

async function findFile(dir, pattern) {
  const entries = await fs.readdir(dir, { withFileTypes: true }).catch(() => []);
  for (const entry of entries) {
    const full = path.join(dir, entry.name);
    if (entry.isFile() && pattern.test(entry.name)) return full;
    if (entry.isDirectory()) {
      const nested = await findFile(full, pattern);
      if (nested) return nested;
    }
  }
  return null;
}

async function runCommand(command, args, cwd, options = {}) {
  console.log(`running: ${quoteCommand([command, ...args])}`);
  await new Promise((resolve, reject) => {
    const child = spawn(command, args, { cwd, stdio: 'inherit', shell: false, env: options.env || process.env });
    child.on('error', reject);
    child.on('exit', code => {
      if (code === 0) resolve();
      else reject(new Error(`${path.basename(command)} exited with code ${code}`));
    });
  });
}

async function runCapture(command, args, cwd = process.cwd()) {
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, { cwd, stdio: ['ignore', 'pipe', 'pipe'], shell: false });
    let stdout = '';
    let stderr = '';
    child.stdout.on('data', chunk => {
      stdout += chunk.toString();
    });
    child.stderr.on('data', chunk => {
      stderr += chunk.toString();
    });
    child.on('error', reject);
    child.on('exit', code => {
      if (code === 0) resolve(stdout);
      else reject(new Error(`${path.basename(command)} exited with code ${code}: ${stderr || stdout}`));
    });
  });
}

async function monitorSerialPort(port, baud) {
  if (process.platform !== 'win32') {
    throw new Error('Direct serial monitor is currently implemented for Windows PowerShell only. Use a serial terminal for this board.');
  }
  const command = `
    $portName = '${String(port).replace(/'/g, "''")}'
    $serial = [System.IO.Ports.SerialPort]::new($portName, ${Number(baud)}, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
    $serial.ReadTimeout = 500
    $serial.Open()
    Write-Host "monitoring $portName at ${Number(baud)} baud; press Ctrl+C to stop"
    try {
      while ($true) {
        try {
          $line = $serial.ReadLine()
          Write-Host $line
        } catch [System.TimeoutException] {}
      }
    } finally {
      $serial.Close()
    }
  `;
  await runCommand('powershell', ['-NoProfile', '-Command', command], process.cwd());
}

async function detectSerialPorts() {
  if (process.platform !== 'win32') return [];
  const command = `
    $ports = Get-PnpDevice -PresentOnly -Class Ports -ErrorAction SilentlyContinue |
      Where-Object {
        $_.FriendlyName -match '\\(COM\\d+\\)' -and
        $_.FriendlyName -notmatch 'Bluetooth'
      } |
      ForEach-Object {
        if ($_.FriendlyName -match '(COM\\d+)') {
          $Matches[1]
        }
      }
    if (-not $ports) {
      $ports = Get-CimInstance Win32_SerialPort -ErrorAction SilentlyContinue |
        Where-Object {
          $_.Name -match 'COM\\d+' -and
          $_.Name -notmatch 'Bluetooth'
        } |
        ForEach-Object { $_.DeviceID }
    }
    $ports | Sort-Object -Unique
  `;
  return new Promise(resolve => {
    const child = spawn('powershell', ['-NoProfile', '-Command', command], { stdio: ['ignore', 'pipe', 'ignore'] });
    let output = '';
    child.stdout.on('data', chunk => {
      output += chunk.toString();
    });
    child.on('exit', () => {
      resolve(output.split(/\r?\n/).map(line => line.trim()).filter(Boolean));
    });
    child.on('error', () => resolve([]));
  });
}

function parseProjectOptions(rawArgs = []) {
  const args = [...rawArgs].filter(Boolean);
  const options = {};
  let projectDir = process.cwd();
  for (let index = 0; index < args.length; index += 1) {
    const item = args[index];
    if (item === '--port') {
      options.port = args[index + 1];
      index += 1;
    } else if (item === '--project') {
      projectDir = path.resolve(args[index + 1]);
      index += 1;
    } else if (!item.startsWith('--')) {
      projectDir = path.resolve(item);
    }
  }
  return { projectDir, options };
}

function choosePort(ports) {
  if (!ports.length) return null;
  return ports.find(port => /^COM\d+$/i.test(port)) || ports[0];
}

async function readProjectManifest(projectDir) {
  const tomlPath = path.join(projectDir, 'nav.toml');
  const jsonPath = path.join(projectDir, 'nav.json');
  const tomlExists = await exists(tomlPath);
  const jsonExists = await exists(jsonPath);
  if (tomlExists) return enhanceProjectManifest(parseToml(await fs.readFile(tomlPath, 'utf8')), projectDir);
  if (jsonExists) return enhanceProjectManifest(JSON.parse(await fs.readFile(jsonPath, 'utf8')), projectDir);
  throw new Error(`No Nav project manifest found in ${projectDir}. Run "nav create <name> --board <board>" first, or add nav.toml to this folder.`);
}

function enhanceProjectManifest(manifest, projectDir) {
  if (manifest?.name === 'navhal') {
    manifest.build_system ||= 'cmake';
    manifest.cmake_generator ||= 'Ninja';
    manifest.cmake_sample ||= 'hal_blink';
    manifest.build_output ||= 'build/navhal/samples/portable/01_hal_blink/hal_blink';
    manifest.board ||= 'nucleo_f401re';
    manifest.target ||= 'stm32f401';
    manifest.uploader ||= 'stlink';
    manifest.flash_address ||= '0x08000000';
    manifest.toolchains = unique([...(manifest.toolchains || []), 'arm-none-eabi', 'stlink']);
  }
  return manifest;
}


async function readPackageManifest(projectDir) {
  const tomlPath = path.join(projectDir, 'nav.toml');
  const jsonPath = path.join(projectDir, 'nav.json');
  try {
    return {
      manifest: parseToml(await fs.readFile(tomlPath, 'utf8')),
      manifestFile: 'nav.toml'
    };
  } catch {}
  try {
    return {
      manifest: JSON.parse(await fs.readFile(jsonPath, 'utf8')),
      manifestFile: 'nav.json'
    };
  } catch {
    throw new Error('pack requires the folder to contain nav.toml or nav.json');
  }
}

async function writeProjectManifest(projectDir, manifest) {
  await fs.writeFile(path.join(projectDir, 'nav.toml'), writeToml(manifest));
}

async function exists(filePath) {
  try {
    await fs.access(filePath);
    return true;
  } catch {
    return false;
  }
}

function parseToml(source) {
  const result = {};
  for (const rawLine of source.split(/\r?\n/)) {
    const line = rawLine.replace(/#.*$/, '').trim();
    if (!line || line.startsWith('[')) continue;
    const match = line.match(/^([A-Za-z0-9_-]+)\s*=\s*(.+)$/);
    if (!match) continue;
    const [, key, rawValue] = match;
    result[key] = parseTomlValue(rawValue.trim());
  }
  return result;
}

function parseTomlValue(rawValue) {
  if (rawValue.startsWith('[')) {
    const inner = rawValue.replace(/^\[/, '').replace(/\]$/, '').trim();
    if (!inner) return [];
    return inner.split(',').map(item => parseTomlValue(item.trim()));
  }
  if ((rawValue.startsWith('"') && rawValue.endsWith('"')) || (rawValue.startsWith("'") && rawValue.endsWith("'"))) {
    return rawValue.slice(1, -1);
  }
  if (rawValue === 'true') return true;
  if (rawValue === 'false') return false;
  return rawValue;
}

function writeToml(manifest) {
  const order = ['name', 'version', 'board', 'target', 'toolchains', 'dependencies'];
  const keys = unique([...order, ...Object.keys(manifest)]);
  return keys
    .filter(key => manifest[key] !== undefined && manifest[key] !== null)
    .map(key => `${key} = ${formatTomlValue(manifest[key])}`)
    .join('\n') + '\n';
}

function formatTomlValue(value) {
  if (Array.isArray(value)) return `[${value.map(formatTomlValue).join(', ')}]`;
  if (typeof value === 'boolean') return String(value);
  return JSON.stringify(String(value));
}

async function collectFiles(dir) {
  const entries = await fs.readdir(dir, { withFileTypes: true });
  const files = [];
  for (const entry of entries) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) {
      if (['.git', '.nav', 'node_modules', 'build'].includes(entry.name)) continue;
      files.push(...await collectFiles(full));
    }
    if (entry.isFile()) files.push(full);
  }
  return files;
}

async function readJson(file) {
  return JSON.parse(await fs.readFile(file, 'utf8'));
}

async function lockPathsExist(lock) {
  const paths = [
    ...(lock.packages || []).map(item => item.path),
    ...(lock.toolchains || []).map(item => item.path)
  ];
  for (const item of paths) {
    try {
      await fs.access(item);
    } catch {
      return false;
    }
  }
  return paths.length > 0;
}

function parseSpec(spec) {
  if (!spec || !spec.includes('/')) throw new Error('Package spec must be namespace/package or namespace/package@version');
  const [namespace, packageAndVersion] = spec.split('/');
  const [name, version] = packageAndVersion.split('@');
  return { namespace, name, version };
}

function currentPlatform() {
  const osMap = { win32: 'windows', linux: 'linux', darwin: 'darwin' };
  const archMap = { x64: 'x64', arm64: 'arm64' };
  return {
    os: osMap[process.platform] || process.platform,
    arch: archMap[process.arch] || process.arch
  };
}

function unique(values) {
  return [...new Set(values.filter(value => value !== undefined && value !== null && value !== ''))];
}

function slug(value) {
  return String(value || '')
    .trim()
    .toLowerCase()
    .replace(/[^a-z0-9._-]+/g, '-')
    .replace(/^-+|-+$/g, '');
}

function hash(buffer) {
  return crypto.createHash('sha256').update(buffer).digest('hex');
}

async function sha256File(file) {
  return hash(await fs.readFile(file));
}

function quoteCommand(parts) {
  return parts.map(part => /\s/.test(part) ? `"${part}"` : part).join(' ');
}

function formatBytes(value) {
  const bytes = Number(value || 0);
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}

function toPosixPath(value) {
  return path.resolve(value).replace(/\\/g, '/');
}

main().catch(error => {
  console.error(`nav: ${error.message}`);
  process.exitCode = 1;
});
