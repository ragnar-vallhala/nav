#!/usr/bin/env node
import fs from 'node:fs/promises';
import { existsSync, readdirSync, statSync as fsSyncStat } from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { spawn } from 'node:child_process';
import http from 'node:http';
import readline from 'node:readline/promises';

const ROOT = path.dirname(fileURLToPath(import.meta.url));
const LEGACY_HOME = path.join(ROOT, '.nav-home');
const HOME = process.env.NAV_HOME || (existsSync(LEGACY_HOME) ? LEGACY_HOME : path.join(os.homedir(), '.nav'));
const CONFIG = path.join(HOME, 'config.json');
const CACHE = path.join(HOME, 'cache');
const TOOLCHAIN_HOME = path.join(HOME, 'toolchains');
const API = normalizeRegistryUrl(process.env.NAV_REGISTRY_URL || process.env.NAV_TEST_REGISTRY || 'https://navdev.navrobotec.com/api');
const PROJECT_SEARCH_MAX_DEPTH = Number(process.env.NAV_PROJECT_SEARCH_MAX_DEPTH || 8);
const PROJECT_SEARCH_MAX_DIRS = Number(process.env.NAV_PROJECT_SEARCH_MAX_DIRS || 4000);
const PROJECT_SEARCH_SKIP_DIRS = new Set([
  '.git',
  '.hg',
  '.svn',
  '.nav',
  '.next',
  '.turbo',
  '.cache',
  '.venv',
  'node_modules',
  'build',
  'dist',
  'out',
  'target',
  'CMakeFiles'
]);

const ABI_VERSION = 1;
const HAL_API_VERSION = 1;
const KNOWN_HAL_CAPS = new Set([
  'GPIO',
  'I2C',
  'I2C_DMA',
  'SPI',
  'UART',
  'TIMER',
  'PWM',
  'ADC',
  'DMA',
  'INTERRUPT'
]);
const TARGET_CAPABILITIES = {
  'cortex-m4/stm32/nucleo_f401re/nucleo_f401re': ['GPIO', 'I2C', 'I2C_DMA', 'SPI', 'UART', 'TIMER', 'PWM', 'ADC', 'DMA', 'INTERRUPT'],
  'cortex-m4/stm32/nucleo_f401re': ['GPIO', 'I2C', 'I2C_DMA', 'SPI', 'UART', 'TIMER', 'PWM', 'ADC', 'DMA', 'INTERRUPT'],
  'cortex-m4/stm32/stm32f401/stm32f401': ['GPIO', 'I2C', 'I2C_DMA', 'SPI', 'UART', 'TIMER', 'PWM', 'ADC', 'DMA', 'INTERRUPT'],
  'cortex-m4/stm32/stm32f401': ['GPIO', 'I2C', 'I2C_DMA', 'SPI', 'UART', 'TIMER', 'PWM', 'ADC', 'DMA', 'INTERRUPT'],
  'xtensa/espressif/esp32/esp32-devkit-v1': ['GPIO', 'I2C', 'SPI', 'UART', 'TIMER', 'PWM', 'ADC', 'INTERRUPT'],
  'xtensa/espressif/esp32-devkit-v1': ['GPIO', 'I2C', 'SPI', 'UART', 'TIMER', 'PWM', 'ADC', 'INTERRUPT'],
  'host/native/local/local': ['GPIO', 'I2C', 'SPI', 'UART', 'TIMER', 'PWM', 'ADC', 'INTERRUPT'],
  'host/native/local': ['GPIO', 'I2C', 'SPI', 'UART', 'TIMER', 'PWM', 'ADC', 'INTERRUPT']
};
const SOURCE_ONLY_REJECT_EXTENSIONS = new Set([
  '.a', '.o', '.obj', '.elf', '.bin', '.hex', '.exe', '.dll', '.so', '.dylib', '.lib', '.class', '.jar'
]);

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
  nav 1
  nav check
  nav signup <name...> <email> <password>
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
  nav validate [project-folder]
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

const COLORS = {
  reset: '\x1b[0m',
  bold: '\x1b[1m',
  dim: '\x1b[2m',
  red: '\x1b[31m',
  green: '\x1b[32m',
  yellow: '\x1b[33m',
  blue: '\x1b[34m',
  magenta: '\x1b[35m',
  cyan: '\x1b[36m'
};

function supportsColor() {
  return process.stdout.isTTY && process.env.NO_COLOR === undefined;
}

function color(value, name) {
  if (!supportsColor() || !COLORS[name]) return String(value);
  return `${COLORS[name]}${value}${COLORS.reset}`;
}

function bold(value) {
  return color(value, 'bold');
}

function dim(value) {
  return color(value, 'dim');
}

function green(value) {
  return color(value, 'green');
}

function red(value) {
  return color(value, 'red');
}

function yellow(value) {
  return color(value, 'yellow');
}

function cyan(value) {
  return color(value, 'cyan');
}

function blue(value) {
  return color(value, 'blue');
}

function printInfo(message) {
  console.log(blue(message));
}

function printSuccess(message) {
  console.log(green(message));
}

function printWarn(message) {
  console.log(yellow(message));
}

function printStep(message) {
  console.log(cyan(message));
}

function formatPath(value) {
  return dim(value);
}

function formatCommand(value) {
  return blue(value);
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
  console.log(cyan(line(header)));
  console.log(dim(columns.map((_, index) => '-'.repeat(preferred[index])).join('   ').trimEnd()));
  rows.forEach(row => console.log(line(row)));
}

async function main() {
  await fs.mkdir(HOME, { recursive: true });
  await fs.mkdir(CACHE, { recursive: true });
  await fs.mkdir(TOOLCHAIN_HOME, { recursive: true });

  const [command, subcommand, ...rest] = args;
  try {
    if (!command || command === 'help' || command === '--help') return usage();
    if (command === '1') return temporaryUpdateTestCommand();
    if (command === 'check') return check(subcommand);
    if (command === 'signup') return signup([subcommand, ...rest].filter(Boolean));
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
    if (command === 'validate') return validateProjectCommand(subcommand);
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
    console.error(red(`nav: ${error.message}`));
    process.exitCode = 1;
  }
}

function temporaryUpdateTestCommand() {
  console.log('Nav CLI update test command is installed.');
}

function normalizeRegistryUrl(value) {
  const raw = String(value || '').replace(/\/+$/, '');
  if (raw === 'https://nav.navrobotec.online/api' || raw === 'https://nav.navrobotec.online') {
    return 'https://navdev.navrobotec.com/api';
  }
  return raw;
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

async function promptHiddenOrPlain(prompt) {
  const rl = readline.createInterface({ input: process.stdin, output: process.stdout });
  try {
    return await rl.question(prompt);
  } finally {
    rl.close();
  }
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
  console.log(`${bold('backend:')} ${health.status === 'healthy' ? green(health.status) : yellow(health.status)}`);
  console.log(`${bold('packages:')} ${stats.packages}, ${bold('versions:')} ${stats.package_versions}, ${bold('toolchains:')} ${stats.toolchains}`);
  console.log(`${bold('registry:')} ${cyan(API)}`);
}

async function check(projectArg) {
  await smoke();
  const projectDir = resolveExistingProjectDir(projectArg);
  const manifest = await readProjectManifest(projectDir).catch(() => null);
  if (!manifest) {
    printWarn('project: no nav.toml/nav.json found from current folder upward or below');
    return;
  }
  const lockPath = path.join(projectDir, '.nav', 'lock.json');
  let lock = await readJson(lockPath).catch(() => null);
  if (!lock || !(await lockPathsExist(lock))) {
    printInfo('project dependencies/toolchains are not fully installed; running nav setup');
    await setupProject(projectDir);
    lock = await readJson(lockPath).catch(() => null);
  }
  console.log(`${bold('project:')} ${manifest.name || path.basename(projectDir)}`);
  console.log(`${bold('board:')} ${manifest.board || '-'}`);
  console.log(`${bold('dependencies:')} ${(manifest.dependencies || []).length}`);
  console.log(`${bold('toolchains requested:')} ${(manifest.toolchains || []).join(', ') || '-'}`);
  console.log(`${bold('lock:')} ${lock ? green(`${lock.toolchains.length} toolchains, ${lock.packages.length} packages`) : yellow('missing, run nav setup')}`);
}

function parseSignupArgs(values = []) {
  const args = [...values].filter(Boolean);
  if (args.length < 3) throw new Error('signup requires <name...> <email> <password>');
  const password = args.at(-1);
  const email = args.at(-2);
  const name = args.slice(0, -2).join(' ').trim();
  return { name, email, password };
}

async function signup(values = []) {
  const { name, email, password } = parseSignupArgs(values);
  if (!name || !email || !password) throw new Error('signup requires <name> <email> <password>');
  const data = await request('/auth/register', {
    method: 'POST',
    body: JSON.stringify({ name, email, password })
  });
  printSuccess(`OTP sent to ${data.email}.`);
  if (!process.stdin.isTTY) {
    throw new Error('signup verification needs an interactive terminal so you can enter the OTP');
  }
  const otp = await promptHiddenOrPlain('Enter OTP: ');
  if (!otp.trim()) {
    throw new Error('OTP is required to finish signup');
  }
  await verifyEmail(data.email, otp.trim());
}

async function verifyEmail(email, otp) {
  if (!email || !otp) throw new Error('verify requires <email> <otp>');
  const result = await request('/auth/verify-email', {
    method: 'POST',
    body: JSON.stringify({ email, otp })
  });
  await writeConfig({ token: result.token, user: result.user, registry: API });
  printSuccess(`account created successfully and logged in as ${result.user.email}`);
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
  printSuccess(`logged in as ${data.user.email}`);
}

async function logout() {
  const config = await readConfig();
  if (!config.token) {
    printWarn('not logged in');
    return;
  }
  delete config.token;
  delete config.user;
  await writeConfig(config);
  printSuccess('logged out');
}

async function updateCli() {
  const currentPath = fileURLToPath(import.meta.url);
  const tempPath = path.join(os.tmpdir(), `nav-cli-${Date.now()}-${crypto.randomBytes(4).toString('hex')}.mjs`);
  const backupPath = `${currentPath}.bak`;

  printStep(`checking latest CLI from ${cyan(API)}`);
  const bytes = Buffer.from(await request('/downloads/nav/nav.mjs'));
  await fs.writeFile(tempPath, bytes);

  await runCapture(process.execPath, ['--check', tempPath]);

  const currentHash = await sha256File(currentPath).catch(() => null);
  const nextHash = await sha256File(tempPath);
  if (currentHash === nextHash) {
    await fs.rm(tempPath, { force: true });
    printSuccess('Nav CLI is already up to date.');
    return;
  }

  await fs.copyFile(currentPath, backupPath).catch(() => null);
  await fs.copyFile(tempPath, currentPath);
  if (process.platform !== 'win32') await fs.chmod(currentPath, 0o755).catch(() => null);
  await fs.rm(tempPath, { force: true });

  printSuccess('Nav CLI updated successfully.');
  console.log(`${bold('backup:')} ${formatPath(backupPath)}`);
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
  printStep('Opening Nav login in your browser.');
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

function escapeHtml(value) {
  return String(value)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

async function whoami() {
  const data = await request('/me');
  console.log(`${bold(data.user.name)} ${dim(`<${data.user.email}>`)}`);
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
    printSuccess(`created namespace ${data.namespace.name}`);
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
    printSuccess(`added ${data.member.email} as ${data.member.role}`);
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
    printSuccess(`created package ${data.package.namespace}/${data.package.slug}`);
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
  console.log(bold(`${data.package.namespace}/${data.package.slug}`));
  console.log(data.package.description || '');
  console.log(`${bold('license:')} ${data.package.license || '-'}`);
  console.log(bold('versions:'));
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
  console.log(bold(`${data.package.namespace}/${data.package.name}`));
  console.log(data.package.description || '');
  console.log(`\n${bold('Commands:')}`);
  Object.entries(data.commands).forEach(([key, value]) => console.log(`  ${cyan(key)}: ${formatCommand(value)}`));
  console.log(`\n${bold('Manifest:')}`);
  console.log(JSON.stringify(data.manifest, null, 2));
}

async function createProject(projectName, rest) {
  if (!projectName) throw new Error('create requires <project> [--board <board>]');
  const { board } = parseSetupArgs(rest, 'esp32-devkit-v1');
  const projectDir = path.resolve(projectName);
  await initializeProjectScaffold(projectDir, path.basename(projectDir), board, { overwriteMain: false });
  printSuccess(`created ${formatPath(projectDir)}`);
  console.log(`${bold('next:')} ${formatCommand('cd into the project and run nav setup')}`);
}

async function initializeProjectScaffold(projectDir, projectName, board, options = {}) {
  await fs.mkdir(path.join(projectDir, 'src'), { recursive: true });
  const projectSlug = slug(projectName || path.basename(projectDir) || 'nav-project');
  if (board.startsWith('stm32')) {
    const manifest = {
      package: {
        kind: 'app',
        name: projectSlug,
        version: '0.1.0',
        license: 'MIT',
        description: `${projectSlug} firmware app`
      },
      abi: {
        abi_version: ABI_VERSION,
        hal_api_version: HAL_API_VERSION
      },
      target: {
        arch: 'cortex-m4',
        vendor: 'stm32',
        family: board,
        board
      },
      requires: {
        caps: ['GPIO', 'TIMER']
      },
      dependencies: {
        'nav/board-stm32f401': '0.1.0'
      },
      build: {
        sources: ['src/**/*.c'],
        include_dirs: ['include'],
        entry: 'src/main.c'
      }
    };
    await writeIfMissing(path.join(projectDir, 'navmod.toml'), writeToml(manifest));
    const mainPath = path.join(projectDir, 'src', 'main.c');
    const mainExists = await exists(mainPath);
    if (options.overwriteMain || !mainExists) {
      await fs.writeFile(mainPath, `#include <stdint.h>\n#include "hal.h"\n\nint main(void) {\n    volatile uint32_t ticks = 0;\n    while (1) {\n        ticks++;\n    }\n}\n`);
    }
    return;
  }
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
  const projectArg = args.find((value, index) => value !== '--board' && (boardIndex < 0 || index !== boardIndex + 1));
  return { projectArg, board };
}

async function setupProject(projectInput) {
  const { projectArg, board } = parseSetupArgs(projectInput);
  const projectDir = resolveSetupProjectDir(projectArg);
  let manifest = await readProjectManifest(projectDir).catch(() => null);
  if (!manifest) {
    printStep(`no navmod.toml/nav.toml/nav.json found; initializing ABI v1 app for ${board}`);
    await initializeProjectScaffold(projectDir, path.basename(projectDir), board, { overwriteMain: false });
    manifest = await readProjectManifest(projectDir);
  }
  const resolution = await resolveProject(manifest);
  const installedToolchains = [];
  const installedPackages = [];

  for (const pkg of resolution.packages) {
    printStep(`resolving package ${pkg.namespace}/${pkg.name}@${pkg.version}`);
    const installed = await installPackageToProject(projectDir, pkg.spec, pkg.version);
    installedPackages.push(installed);
  }

  await materializeProjectRuntime(projectDir, manifest, installedPackages);

  for (const toolchain of resolution.toolchains) {
    if (toolchain.source === 'system') {
      printStep(`using system ${toolchain.name} (${toolchain.executables?.[0] || toolchain.path})`);
      installedToolchains.push(toolchain);
      continue;
    }
    if (toolchain.source === 'metadata-only') {
      printStep(`using registry metadata for ${toolchain.name}@${toolchain.version}`);
      installedToolchains.push(toolchain);
      continue;
    }
    printStep(`resolving ${toolchain.os}/${toolchain.arch} toolchain ${toolchain.name}@${toolchain.version}`);
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
  printSuccess(`setup complete for ${lock.project}`);
  console.log(`${bold('packages:')} ${installedPackages.length}`);
  console.log(`${bold('toolchains:')} ${installedToolchains.map(item => `${item.name}@${item.version}`).join(', ') || '-'}`);
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
    board: installed.manifest?.board || null,
    target: installed.manifest?.target || 'host',
    uploader: installed.manifest?.uploader || null,
    run: installed.manifest?.run || 'none',
    flash_address: installed.manifest?.flash_address || null,
    toolchains: installed.manifest?.toolchains || ['cmake', 'ninja', 'arm-none-eabi']
  };
  await writeProjectManifest(projectDir, baseManifest);
  console.log(`${bold('base project:')} ${normalized.namespace}/${normalized.name}@${normalized.version}`);
}

async function addPackage(spec) {
  if (!spec) throw new Error('add requires <namespace/package[@version]>');
  const projectDir = requireProjectDir();
  const manifest = await readProjectManifest(projectDir);
  const normalized = await resolvePackageSpec(spec);
  const versionData = await getPackageVersion(normalized.namespace, normalized.name, normalized.version);
  const dependencySpec = `${normalized.namespace}/${normalized.name}@${normalized.version}`;
  if (isAbiManifest(manifest)) {
    await addDependencyToNavmod(projectDir, normalized, normalized.version);
  } else {
    mergeProjectHints(manifest, versionData.manifest || {});
    manifest.dependencies = unique([...(manifest.dependencies || []), dependencySpec]);
    await writeProjectManifest(projectDir, manifest);
  }
  await installPackageToProject(projectDir, `${normalized.namespace}/${normalized.name}`, normalized.version);
  printSuccess(`added ${dependencySpec}`);
  await setupProject(projectDir);
}

async function addDependencyToNavmod(projectDir, normalized, version) {
  const navmodPath = path.join(projectDir, 'navmod.toml');
  let content = await fs.readFile(navmodPath, 'utf8');
  const depName = normalized.namespace === 'nav' ? normalized.name : `${normalized.namespace}/${normalized.name}`;
  const depKey = formatTomlKey(depName);
  const line = `${depKey} = "${version}"`;
  const depRegex = new RegExp(`^\\s*(?:${escapeRegExp(depName)}|${escapeRegExp(depKey)})\\s*=`, 'm');
  if (depRegex.test(content)) {
    content = content.replace(new RegExp(`^\\s*(?:${escapeRegExp(depName)}|${escapeRegExp(depKey)})\\s*=.*$`, 'm'), line);
  } else if (/^\[dependencies\]\s*$/m.test(content)) {
    content = content.replace(/^\[dependencies\]\s*$/m, `[dependencies]\n${line}`);
  } else {
    content = `${content.trimEnd()}\n\n[dependencies]\n${line}\n`;
  }
  await fs.writeFile(navmodPath, content);
}

async function buildProject(projectArg) {
  const projectDir = requireProjectDir(projectArg);
  const manifest = await readProjectManifest(projectDir);
  if (isAbiManifest(manifest)) {
    await buildAbiProject(projectDir, manifest);
    return;
  }
  const lockPath = path.join(projectDir, '.nav', 'lock.json');
  let lock = await readJson(lockPath).catch(() => null);
  if (!lock) {
    printInfo('no lockfile found; running nav setup first');
    await setupProject(projectDir);
    lock = await readJson(lockPath);
  }
  const requestedToolchains = manifest.toolchains || [];
  const buildSystemForRequirements = manifest.build_system || (await exists(path.join(projectDir, 'CMakeLists.txt')) ? 'cmake' : undefined);
  const missingToolchains = unique([
    ...requestedToolchains.filter(name => !lock.toolchains.some(toolchain => toolchain.name === name)),
    ...missingRequiredToolchains(manifest, lock, buildSystemForRequirements)
  ]);
  if (missingToolchains.length > 0) {
    printInfo(`lockfile is missing toolchains (${missingToolchains.join(', ')}); running nav setup first`);
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
  printSuccess(`build complete: ${formatPath(outputPath)}`);
}

async function validateProjectCommand(projectArg) {
  const projectDir = requireProjectDir(projectArg);
  const manifest = await readProjectManifest(projectDir);
  if (!isAbiManifest(manifest)) {
    printSuccess('legacy manifest parsed');
    return;
  }
  await validateAbiProject(projectDir, manifest, { build: false });
  printSuccess(`ABI v1 validation passed for ${manifest.package_kind} ${manifest.name}@${manifest.version}`);
}

async function buildAbiProject(projectDir, manifest) {
  let lock = await readJson(path.join(projectDir, '.nav', 'lock.json')).catch(() => null);
  if (!lock) {
    printInfo('no lockfile found; running nav setup first');
    await setupProject(projectDir);
    lock = await readJson(path.join(projectDir, '.nav', 'lock.json'));
  }

  await validateAbiProject(projectDir, manifest, { build: true, lock });
  const buildRoot = path.join(projectDir, 'build', 'abi-v1');
  const generatedInclude = path.join(buildRoot, 'generated', 'include');
  const objectRoot = path.join(buildRoot, 'obj');
  const target = manifest.package_kind === 'app' ? manifest.target : { arch: 'cortex-m4', vendor: 'stm32', board: 'nucleo_f401re' };
  await fs.rm(buildRoot, { recursive: true, force: true });
  await fs.mkdir(generatedInclude, { recursive: true });
  await fs.mkdir(objectRoot, { recursive: true });
  await writeAbiGeneratedHeaders(generatedInclude, target);
  await writeAbiGeneratedCmake(projectDir, manifest, lock, buildRoot);

  const armToolchain = lock.toolchains.find(item => item.name === 'arm-none-eabi');
  if (!armToolchain) throw new Error('ABI v1 build requires arm-none-eabi. Run nav setup.');
  const gcc = await findRegistryTool(armToolchain, process.platform === 'win32' ? /^arm-none-eabi-gcc\.exe$/i : /^arm-none-eabi-gcc$/);
  const ar = await findRegistryTool(armToolchain, process.platform === 'win32' ? /^arm-none-eabi-ar\.exe$/i : /^arm-none-eabi-ar$/);
  const nm = await findFile(armToolchain.path, process.platform === 'win32' ? /^arm-none-eabi-nm\.exe$/i : /^arm-none-eabi-nm$/);
  const objcopy = await findFile(armToolchain.path, process.platform === 'win32' ? /^arm-none-eabi-objcopy\.exe$/i : /^arm-none-eabi-objcopy$/);

  const moduleBuilds = [];
  const modulePackages = abiModulePackages(lock);
  const allModuleIncludeDirs = modulePackages.flatMap(pkg => abiPublicIncludeDirs(path.join(pkg.path, 'files'), pkg.manifest));
  for (const pkg of modulePackages) {
    const moduleManifest = pkg.manifest;
    await validateAbiProject(path.join(pkg.path, 'files'), moduleManifest, { build: true, lock, appTarget: target, dependencyOnly: true });
    const archive = await buildAbiModuleArchive({
      projectDir: path.join(pkg.path, 'files'),
      manifest: moduleManifest,
      buildRoot: path.join(buildRoot, 'deps', moduleManifest.name),
      generatedInclude,
      gcc,
      ar,
      nm,
      target,
      extraIncludeDirs: allModuleIncludeDirs
    });
    moduleBuilds.push({ manifest: moduleManifest, archive, includeDirs: abiPublicIncludeDirs(path.join(pkg.path, 'files'), moduleManifest) });
  }

  let outputPath;
  if (manifest.package_kind === 'module') {
    outputPath = await buildAbiModuleArchive({
      projectDir,
      manifest,
      buildRoot: path.join(buildRoot, manifest.name),
      generatedInclude,
      gcc,
      ar,
      nm,
      target,
      extraIncludeDirs: moduleBuilds.flatMap(item => item.includeDirs)
    });
  } else {
    outputPath = await buildAbiAppExecutable({
      projectDir,
      manifest,
      buildRoot,
      generatedInclude,
      gcc,
      objcopy,
      target,
      moduleBuilds
    });
  }

  const report = {
    schema_version: 1,
    abi_version: ABI_VERSION,
    package: `${manifest.name}@${manifest.version}`,
    kind: manifest.package_kind,
    target_triple: manifest.package_kind === 'app' ? targetTripleFromManifest(manifest.target) : targetTripleFromManifest(target),
    output: outputPath,
    modules: moduleBuilds.map(item => `${item.manifest.name}@${item.manifest.version}`),
    built_at: new Date().toISOString()
  };
  await fs.writeFile(path.join(buildRoot, 'nav-abi-build-report.json'), JSON.stringify(report, null, 2));
  printSuccess(`ABI v1 build complete: ${formatPath(outputPath)}`);
}

async function validateAbiProject(projectDir, manifest, { build = false, lock = null, appTarget = null, dependencyOnly = false } = {}) {
  validateAbiManifest(manifest, projectDir, { phase: build ? 'build' : 'validate', appTarget });
  const root = path.resolve(projectDir);
  const sourceFiles = await resolveSourceGlobs(root, manifest.sources);
  if (sourceFiles.length === 0) throw new Error(`ABI v1 validation failed for ${manifest.name}: no files match [build].sources`);
  await validateSourceOnlyFiles(root);
  await validateAbiSourceRules(root, manifest, sourceFiles);
  if (manifest.kconfig) await validateAbiKconfig(path.join(root, manifest.kconfig), manifest);
  if (manifest.cmake_extras) await validateAbiCmakeExtras(path.join(root, manifest.cmake_extras));
  if (manifest.package_kind === 'app' && !dependencyOnly) {
    const mainCount = await countMainDefinitions(sourceFiles);
    if (mainCount !== 1) throw new Error(`ABI v1 apps must define exactly one main(); found ${mainCount}`);
  }
  if (manifest.package_kind === 'module') {
    const mainCount = await countMainDefinitions(sourceFiles);
    if (mainCount > 0) throw new Error(`ABI v1 modules must not define main(); found ${mainCount}`);
  }
  if (lock && manifest.package_kind === 'app') {
    for (const pkg of abiModulePackages(lock)) {
      validateAbiManifest(pkg.manifest, path.join(pkg.path, 'files'), { phase: 'build', appTarget: manifest.target });
    }
  }
}

async function buildAbiModuleArchive({ projectDir, manifest, buildRoot, generatedInclude, gcc, ar, nm, target, extraIncludeDirs = [] }) {
  await fs.mkdir(buildRoot, { recursive: true });
  const sourceFiles = await resolveSourceGlobs(projectDir, manifest.sources);
  const includeDirs = unique([
    generatedInclude,
    ...abiPublicIncludeDirs(projectDir, manifest),
    ...extraIncludeDirs
  ]);
  const objects = [];
  for (const source of sourceFiles) {
    const object = path.join(buildRoot, `${path.relative(projectDir, source).replace(/[\\/.:]/g, '_')}.o`);
    await fs.mkdir(path.dirname(object), { recursive: true });
    await runCommand(gcc, [
      ...abiCompileFlags(target),
      ...abiCapabilityDefines(target),
      `-DNAV_MODULE_NAME="${manifest.name}"`,
      `-DNAV_MODULE_VERSION="${manifest.version}"`,
      ...includeDirs.map(dir => `-I${dir}`),
      '-c',
      source,
      '-o',
      object
    ], projectDir);
    objects.push(object);
  }
  const archive = path.join(buildRoot, `lib${manifest.name}.a`);
  await runCommand(ar, ['rcs', archive, ...objects], projectDir);
  if (nm) await validateAbiArchiveSymbols(nm, archive, manifest);
  return archive;
}

async function buildAbiAppExecutable({ projectDir, manifest, buildRoot, generatedInclude, gcc, objcopy, target, moduleBuilds }) {
  const sourceFiles = await resolveSourceGlobs(projectDir, manifest.sources);
  const includeDirs = unique([
    generatedInclude,
    ...abiPublicIncludeDirs(projectDir, manifest),
    ...moduleBuilds.flatMap(item => item.includeDirs)
  ]);
  const objects = [];
  for (const source of sourceFiles) {
    const object = path.join(buildRoot, 'app-obj', `${path.relative(projectDir, source).replace(/[\\/.:]/g, '_')}.o`);
    await fs.mkdir(path.dirname(object), { recursive: true });
    await runCommand(gcc, [
      ...abiCompileFlags(target),
      ...abiCapabilityDefines(target),
      ...includeDirs.map(dir => `-I${dir}`),
      '-c',
      source,
      '-o',
      object
    ], projectDir);
    objects.push(object);
  }
  const startup = path.join(buildRoot, 'startup.c');
  const linker = path.join(buildRoot, 'linker.ld');
  await fs.writeFile(startup, STM32_STARTUP_C);
  await fs.writeFile(linker, STM32_LINKER_LD);
  const startupObject = path.join(buildRoot, 'startup.o');
  await runCommand(gcc, [...abiCompileFlags(target), '-c', startup, '-o', startupObject], projectDir);
  objects.push(startupObject);
  const output = path.resolve(projectDir, manifest.build_output || path.join('build', 'abi-v1', manifest.name));
  await fs.mkdir(path.dirname(output), { recursive: true });
  const elf = output.endsWith('.elf') ? output : `${output}.elf`;
  await runCommand(gcc, [
    ...abiCompileFlags(target),
    ...objects,
    ...moduleBuilds.map(item => item.archive),
    '-nostdlib',
    '-Wl,--gc-sections',
    `-Wl,-T,${linker}`,
    '-o',
    elf
  ], projectDir);
  if (objcopy) {
    await runCommand(objcopy, ['-O', 'binary', elf, output.endsWith('.bin') ? output : `${output}.bin`], projectDir);
  }
  return elf;
}

function abiCompileFlags(target = {}) {
  const cpu = target.arch && target.arch.startsWith('cortex-') ? target.arch : 'cortex-m4';
  return [
    `-mcpu=${cpu}`,
    '-mthumb',
    '-ffreestanding',
    '-fdata-sections',
    '-ffunction-sections',
    '-Wall',
    '-Wextra',
    '-Os'
  ];
}

function abiCapabilityDefines(target = {}) {
  const caps = capsForTarget(target);
  return [...KNOWN_HAL_CAPS].map(cap => `-DNAVHAL_HAS_${cap}=${caps.has(cap) ? 1 : 0}`);
}

function abiModulePackages(lock) {
  return (lock?.packages || []).filter(pkg => pkg.manifest?.abi_v1 && pkg.manifest.package_kind === 'module');
}

function abiPublicIncludeDirs(root, manifest) {
  return normalizeStringArray(manifest.include_dirs).map(item => path.resolve(root, item));
}

async function writeAbiGeneratedHeaders(includeDir, target) {
  const caps = capsForTarget(target || { arch: 'cortex-m4', vendor: 'stm32', board: 'nucleo_f401re' });
  const capLines = [...KNOWN_HAL_CAPS].sort().map(cap => `#define NAVHAL_HAS_${cap} ${caps.has(cap) ? 1 : 0}`).join('\n');
  const header = `#pragma once
#define HAL_API_VERSION ${HAL_API_VERSION}
${capLines}

typedef enum {
    HAL_OK = 0,
    HAL_ERROR = 1,
    HAL_BUSY = 2,
    HAL_TIMEOUT = 3
} hal_status_t;
`;
  await fs.mkdir(includeDir, { recursive: true });
  await fs.writeFile(path.join(includeDir, 'navhal.h'), header);
  await fs.writeFile(path.join(includeDir, 'hal.h'), header);
  await fs.writeFile(path.join(includeDir, 'navhal_abi.h'), header);
}

async function writeAbiGeneratedCmake(projectDir, manifest, lock, buildRoot) {
  const moduleBlocks = [];
  for (const pkg of abiModulePackages(lock || { packages: [] })) {
    moduleBlocks.push(await abiCmakeTargetBlock(pkg.manifest, path.join(pkg.path, 'files'), 'STATIC'));
  }
  moduleBlocks.push(await abiCmakeTargetBlock(manifest, projectDir, manifest.package_kind === 'module' ? 'STATIC' : 'EXECUTABLE'));
  const cmake = `cmake_minimum_required(VERSION 3.20)
project(nav_abi_${manifest.name} C)

add_library(navhal INTERFACE)
target_include_directories(navhal INTERFACE "\${CMAKE_CURRENT_LIST_DIR}/generated/include")

${moduleBlocks.join('\n\n')}
`;
  await fs.writeFile(path.join(buildRoot, 'CMakeLists.generated.txt'), cmake);
}

async function abiCmakeTargetBlock(manifest, root, type) {
  const sourceFiles = await resolveSourceGlobs(root, manifest.sources);
  const sourceList = sourceFiles.map(item => `"${path.resolve(item).replace(/\\/g, '/')}"`).join(' ');
  const includeList = manifest.include_dirs.map(item => `"${path.resolve(root, item).replace(/\\/g, '/')}"`).join(' ');
  const targetName = sanitizeCIdentifier(manifest.name);
  if (type === 'EXECUTABLE') {
    return `add_executable(${targetName} ${sourceList})
target_include_directories(${targetName} PUBLIC ${includeList})
target_link_libraries(${targetName} PUBLIC navhal)`;
  }
  return `add_library(${targetName} STATIC ${sourceList})
target_include_directories(${targetName} PUBLIC ${includeList})
target_link_libraries(${targetName} PUBLIC navhal)
target_compile_definitions(${targetName} PRIVATE NAV_MODULE_NAME="${manifest.name}" NAV_MODULE_VERSION="${manifest.version}")`;
}

async function resolveSourceGlobs(root, patterns) {
  const allFiles = await collectFiles(root);
  const matched = new Set();
  for (const pattern of patterns) {
    const regex = globToRegExp(pattern);
    for (const file of allFiles) {
      const rel = path.relative(root, file).replace(/\\/g, '/');
      if (regex.test(rel)) matched.add(file);
    }
  }
  return [...matched].filter(file => /\.(c|cc|cpp|cxx|s|S)$/i.test(file)).sort();
}

function globToRegExp(pattern) {
  const input = String(pattern).replace(/\\/g, '/');
  const placeholder = '\0';
  const normalized = input.replace(/\*\*\//g, placeholder).replace(/\*\*/g, placeholder);
  let output = '';
  for (const char of normalized) {
    if (char === placeholder) output += '(?:.*/)?';
    else if (char === '*') output += '[^/]*';
    else output += char.replace(/[.+^${}()|[\]\\]/g, '\\$&');
  }
  return new RegExp(`^${output}$`);
}

async function validateSourceOnlyFiles(root) {
  const files = await collectFiles(root);
  for (const file of files) {
    const ext = path.extname(file).toLowerCase();
    if (SOURCE_ONLY_REJECT_EXTENSIONS.has(ext)) {
      throw new Error(`ABI v1 packages are source-only; rejected binary artifact ${path.relative(root, file)}`);
    }
  }
}

async function validateAbiSourceRules(root, manifest, sourceFiles) {
  const modulePrefix = moduleSymbolPrefix(manifest.name);
  if (manifest.package_kind === 'module') {
    const publicHeaderRoot = path.join(root, 'include', modulePrefix);
    if (!existsSync(publicHeaderRoot)) {
      throw new Error(`ABI v1 module headers must live under include/${modulePrefix}/`);
    }
  }
  for (const source of sourceFiles) {
    const text = await fs.readFile(source, 'utf8');
    if (/^\s*#\s*define\s+NAVHAL_/m.test(text)) {
      throw new Error(`module/app must not redefine NAVHAL_* macros: ${path.relative(root, source)}`);
    }
    if (manifest.package_kind === 'module' && /\bhal_[A-Za-z0-9_]*\s*\(/.test(text.replace(/\bhal_status_t\b/g, ''))) {
      // Modules may call hal_* symbols, but they must not define them. The archive symbol pass catches definitions.
    }
  }
}

async function validateAbiArchiveSymbols(nm, archive, manifest) {
  const output = await runCapture(nm, ['-g', '--defined-only', archive]).catch(() => '');
  const prefix = moduleSymbolPrefix(manifest.name);
  const allowed = new Set([]);
  const bad = [];
  for (const line of output.split(/\r?\n/)) {
    const match = line.trim().match(/(?:[0-9a-fA-F]+\s+)?[A-Z]\s+(.+)$/);
    const symbol = match?.[1]?.trim();
    if (!symbol || symbol.startsWith('_') || allowed.has(symbol)) continue;
    if (symbol === 'main') bad.push(symbol);
    else if (!symbol.startsWith(`${prefix}_`) && !symbol.startsWith(prefix.toUpperCase())) bad.push(symbol);
    if (symbol.startsWith('hal_')) bad.push(symbol);
  }
  if (bad.length > 0) {
    throw new Error(`ABI v1 module ${manifest.name} exports invalid symbols: ${unique(bad).join(', ')}`);
  }
}

async function validateAbiKconfig(kconfigPath, manifest) {
  const text = await fs.readFile(kconfigPath, 'utf8').catch(() => {
    throw new Error(`Kconfig file not found: ${kconfigPath}`);
  });
  const prefix = moduleSymbolPrefix(manifest.name).toUpperCase();
  if (/^\s*mainmenu\b/m.test(text)) throw new Error('module Kconfig must not contain mainmenu');
  if (/^\s*source\s+["']?(?:\.\.|\/)/m.test(text)) throw new Error('module Kconfig must not source files outside the module tree');
  if (/^\s*select\s+DRV_/m.test(text)) throw new Error('module Kconfig must not select NavHAL DRV_* caps; use [requires].caps');
  for (const match of text.matchAll(/^\s*(?:config|menuconfig)\s+([A-Za-z0-9_]+)/gm)) {
    if (!match[1].startsWith(`${prefix}_`)) {
      throw new Error(`Kconfig symbol ${match[1]} must start with ${prefix}_`);
    }
  }
}

async function validateAbiCmakeExtras(filePath) {
  const text = await fs.readFile(filePath, 'utf8').catch(() => {
    throw new Error(`cmake_extras file not found: ${filePath}`);
  });
  const forbidden = [/\bPARENT_SCOPE\b/i, /^\s*project\s*\(/im, /^\s*add_executable\s*\(/im, /^\s*add_subdirectory\s*\(/im];
  if (forbidden.some(pattern => pattern.test(text))) {
    throw new Error('cmake/extras.cmake must not modify global build state');
  }
}

async function countMainDefinitions(files) {
  let count = 0;
  for (const file of files) {
    const text = await fs.readFile(file, 'utf8');
    const withoutComments = text.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/.*$/gm, '');
    if (/\b(?:int|void)\s+main\s*\(/.test(withoutComments)) count += 1;
  }
  return count;
}

function moduleSymbolPrefix(name) {
  return String(name || '').replace(/-/g, '_');
}

function sanitizeCIdentifier(name) {
  return moduleSymbolPrefix(name).replace(/[^A-Za-z0-9_]/g, '_');
}

async function cleanProject(projectArg) {
  const projectDir = requireProjectDir(projectArg);
  await fs.rm(path.join(projectDir, 'build'), { recursive: true, force: true });
  await fs.rm(path.join(projectDir, '.nav', 'lock.json'), { force: true });
  printSuccess('cleaned build outputs and lockfile');
}

async function runProject(rawArgs = []) {
  const { projectDir, options } = parseProjectOptions(rawArgs);
  await buildProject(projectDir);
  const manifest = await readProjectManifest(projectDir);
  const lock = await readJson(path.join(projectDir, '.nav', 'lock.json')).catch(() => null);
  const buildSystem = lock ? resolveBuildSystem(manifest, lock, projectDir) : manifest.build_system;
  const localRun = manifest.run === 'local' || buildSystem === 'native-cpp';
  if (localRun) {
    await runLocalBuildOutput(projectDir, manifest);
    return;
  }
  await uploadProject(['--project', projectDir, ...(options.port ? ['--port', options.port] : [])]);
}

async function runLocalBuildOutput(projectDir, manifest) {
  const outputPath = path.resolve(projectDir, manifest.build_output || path.join('build', `${manifest.name || path.basename(projectDir)}${process.platform === 'win32' ? '.exe' : ''}`));
  const candidates = process.platform === 'win32' && !outputPath.endsWith('.exe')
    ? [outputPath, `${outputPath}.exe`]
    : [outputPath];
  const executable = candidates.find(candidate => existsSync(candidate));
  if (!executable) {
    throw new Error(`Build output not found: ${outputPath}. Run nav build and check build_output in nav.toml.`);
  }
  console.log(`${bold('run:')} ${formatPath(executable)}`);
  await runCommand(executable, [], projectDir);
}

async function uploadProject(rawArgs = []) {
  const { projectDir, options } = parseProjectOptions(rawArgs);
  const manifest = await readProjectManifest(projectDir);
  const lockPath = path.join(projectDir, '.nav', 'lock.json');
  let lock = await readJson(lockPath).catch(() => null);
  if (!lock) {
    printInfo('no lockfile found; running nav setup first');
    await setupProject(projectDir);
    lock = await readJson(lockPath);
  }
  let buildSystem = resolveBuildSystem(manifest, lock, projectDir);
  const missingToolchains = missingRequiredToolchains(manifest, lock, buildSystem);
  if (missingToolchains.length > 0) {
    printInfo(`lockfile is missing upload/build toolchains (${missingToolchains.join(', ')}); running nav setup first`);
    await setupProject(projectDir);
    lock = await readJson(lockPath);
    buildSystem = resolveBuildSystem(manifest, lock, projectDir);
  }
  const uploader = resolveUploader(manifest, lock, buildSystem);
  console.log(`${bold('upload plan:')} ${manifest.name || path.basename(projectDir)} ${cyan('->')} ${manifest.board || 'unknown board'}`);
  console.log(`${bold('registry-managed uploader:')} ${uploader}`);
  if (buildSystem === 'arduino-cli') {
    const ports = options.port ? [options.port] : await detectSerialPorts();
    console.log(`${bold('detected serial ports:')} ${ports.length ? green(ports.join(', ')) : yellow('none')}`);
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
  if (buildSystem === 'abi-v1') {
    if (manifest.package_kind !== 'app') throw new Error('ABI v1 modules are libraries and cannot be uploaded directly.');
    if (manifest.target?.vendor === 'stm32') {
      await uploadStm32(projectDir, manifest, lock);
      return;
    }
    throw new Error(`ABI v1 upload is not implemented for target ${targetTripleFromManifest(manifest.target || {})}.`);
  }
  throw new Error(`Upload is not implemented for build_system '${buildSystem}'.`);
}

async function monitorProject(rawArgs = []) {
  const { projectDir, options } = parseProjectOptions(rawArgs);
  const manifest = await readProjectManifest(projectDir).catch(() => ({}));
  const lock = await readJson(path.join(projectDir, '.nav', 'lock.json')).catch(() => null);
  if (!lock) throw new Error('run nav setup before monitor');
  console.log(`${bold('monitor plan:')} ${manifest.board || 'unknown board'}`);
  if (manifest.build_system === 'stm32-gcc' || (manifest.build_system === 'abi-v1' && manifest.target?.vendor === 'stm32')) {
    const port = options.port;
    if (!port) throw new Error('STM32 monitor needs a serial VCP port. Pass --port COMx if your board exposes one.');
    await monitorSerialPort(port, Number(manifest.monitor_baud || 9600));
    return;
  }
  const ports = options.port ? [options.port] : await detectSerialPorts();
  const port = options.port || choosePort(ports);
  console.log(`${bold('detected serial ports:')} ${ports.length ? green(ports.join(', ')) : yellow('none')}`);
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
  const files = (await collectFiles(absoluteFolder)).sort((a, b) => path.relative(absoluteFolder, a).localeCompare(path.relative(absoluteFolder, b)));
  const { manifest, manifestFile } = await readPackageManifest(absoluteFolder);
  if (isAbiManifest(manifest)) {
    await validateAbiProject(absoluteFolder, manifest, { build: false });
  }
  const abiSourceHash = isAbiManifest(manifest) ? await sourceTreeHash(absoluteFolder, files) : null;
  const bundledManifest = abiSourceHash
    ? { ...manifest, abi: { ...(manifest.abi || {}), source_hash: `sha256:${abiSourceHash}` }, source_hash: `sha256:${abiSourceHash}` }
    : manifest;
  const bundle = {
    schema_version: 1,
    packed_at: abiSourceHash ? '1970-01-01T00:00:00.000Z' : new Date().toISOString(),
    manifest_file: manifestFile,
    manifest: bundledManifest,
    ...(abiSourceHash ? { abi_version: ABI_VERSION, source_hash: `sha256:${abiSourceHash}` } : {}),
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
  printSuccess(`packed ${files.length} files ${cyan('->')} ${formatPath(out)}`);
  console.log(`${bold('manifest:')} ${manifestFile}`);
  if (abiSourceHash) console.log(`${bold('source hash:')} ${dim(`sha256:${abiSourceHash}`)}`);
  console.log(`${bold('sha256:')} ${dim(hash(bytes))}`);
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
  if (complete.version?.version) {
    printSuccess(`published ${namespace}/${name}@${complete.version.version}`);
    return;
  }
  printInfo(`queued security scan for ${namespace}/${name}@${version}`);
  const final = await waitForPublish(init.session.id);
  if (final.session.status === 'published') {
    printSuccess(`published ${namespace}/${name}@${version}`);
    return;
  }
  const reason = final.session.scan_error || 'security scan rejected the archive';
  throw new Error(`publish rejected: ${reason}`);
}

async function waitForPublish(sessionId) {
  const deadline = Date.now() + 120000;
  const pending = new Set(['queued', 'running', 'uploaded']);
  while (Date.now() < deadline) {
    const status = await request(`/publish/${sessionId}/status`);
    const state = status.session.scan_status || status.session.status;
    if (!pending.has(state) && !pending.has(status.session.status)) return status;
    process.stdout.write('.');
    await new Promise(resolve => setTimeout(resolve, 1500));
  }
  process.stdout.write('\n');
  throw new Error('publish scan timed out; check the registry worker status');
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
  printSuccess(`installed ${normalized.namespace}/${normalized.name}@${normalized.version}`);
  console.log(`${bold('archive:')} ${formatPath(out)}`);
  console.log(`${bold('sha256:')} ${dim(hash(bytes))}`);
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
    printSuccess(`published toolchain ${data.vendor.kind}/${data.vendor.name}/${data.toolchain.name}@${data.version.version}`);
    return;
  }
  throw new Error('toolchain command must be list or publish');
}

async function resolveProject(manifest) {
  const packageMap = new Map();
  const resolvedByPackage = new Map();
  const toolchainSet = new Set(requiredToolchainsForManifest(manifest));
  const queue = [...(manifest.dependencies || [])].map(spec => ({ spec, via: manifest.name || 'project' }));
  const seen = new Set();

  while (queue.length > 0) {
    const { spec: rawSpec, via } = queue.shift();
    const requested = parseSpec(rawSpec);
    const normalized = await resolvePackageSpec(rawSpec);
    const packageKey = `${normalized.namespace}/${normalized.name}`;
    const existingVersion = resolvedByPackage.get(packageKey);
    if (existingVersion && existingVersion !== normalized.version) {
      throw new Error(`Dependency conflict for ${packageKey}: ${existingVersion} already selected, but ${via} requested ${requested.version || '*'}`);
    }
    resolvedByPackage.set(packageKey, normalized.version);
    const key = `${normalized.namespace}/${normalized.name}@${normalized.version}`;
    if (seen.has(key)) continue;
    seen.add(key);
    packageMap.set(key, { ...normalized, spec: `${normalized.namespace}/${normalized.name}` });
    const versionData = await getPackageVersion(normalized.namespace, normalized.name, normalized.version);
    const pkgManifest = versionData.manifest || {};
    if (pkgManifest.abi_v1 && pkgManifest.package_kind === 'app') {
      throw new Error(`Apps cannot depend on app packages: ${key}`);
    }
    for (const tc of pkgManifest.toolchains || []) toolchainSet.add(tc);
    for (const dep of pkgManifest.dependencies || []) queue.push({ spec: dep, via: key });
  }

  const allToolchains = (await request('/toolchains')).toolchains;
  const platform = currentPlatform();
  const resolvedToolchains = [];
  for (const name of toolchainSet) {
    const systemToolchain = await resolveSystemToolchain(name);
    if (systemToolchain) {
      resolvedToolchains.push(systemToolchain);
      continue;
    }

    const match = allToolchains.find(tc => tc.name === name && tc.os === platform.os && tc.arch === platform.arch)
      || allToolchains.find(tc => tc.name === name && tc.os === platform.os && tc.arch);
    if (match) {
      resolvedToolchains.push(match);
      continue;
    }

    const metadataOnly = allToolchains.find(tc => tc.name === name);
    if (metadataOnly && !metadataOnly.os && !metadataOnly.arch) {
      resolvedToolchains.push({
        name,
        version: metadataOnly.version || 'metadata',
        vendor: metadataOnly.vendor,
        vendor_kind: metadataOnly.vendor_kind,
        os: platform.os,
        arch: platform.arch,
        path: HOME,
        source: 'metadata-only',
        sha256: null
      });
      continue;
    }

    throw new Error(`No ${platform.os}/${platform.arch} registry toolchain found for ${name}, and no compatible system tool was detected`);
  }

  return {
    packages: [...packageMap.values()],
    toolchains: resolvedToolchains
  };
}

function requiredToolchainsForManifest(manifest, buildSystem = manifest?.build_system) {
  const toolchains = new Set(normalizeStringArray(manifest?.toolchains));
  const uploaderToolchain = uploaderToToolchainName(manifest?.uploader);
  if (uploaderToolchain) toolchains.add(uploaderToolchain);

  if (manifest?.name === 'navhal' || buildSystem === 'cmake') {
    toolchains.add('cmake');
    toolchains.add('ninja');
    toolchains.add('arm-none-eabi');
  }

  if (buildSystem === 'abi-v1') {
    toolchains.add('cmake');
    toolchains.add('ninja');
    toolchains.add('arm-none-eabi');
    if (manifest?.target?.vendor === 'stm32') toolchains.add('stlink');
  }

  if (buildSystem === 'stm32-gcc' || String(manifest?.board || '').startsWith('stm32') || String(manifest?.target || '').startsWith('stm32')) {
    toolchains.add('arm-none-eabi');
    if (manifest?.uploader === 'stlink') toolchains.add('stlink');
  }

  if (buildSystem === 'arduino-cli') toolchains.add('arduino-cli');
  return [...toolchains].filter(Boolean);
}

function uploaderToToolchainName(uploader) {
  const value = String(uploader || '').trim();
  if (!value || value === 'none') return null;
  if (value === 'arduino-cli upload') return 'arduino-cli';
  if (['stlink', 'esptool', 'openocd', 'arduino-cli'].includes(value)) return value;
  return null;
}

function missingRequiredToolchains(manifest, lock, buildSystem = manifest?.build_system) {
  const locked = new Set((lock?.toolchains || []).map(toolchain => toolchain.name));
  return requiredToolchainsForManifest(manifest, buildSystem).filter(name => !locked.has(name));
}

async function resolveSystemToolchain(name) {
  const tools = systemToolchainExecutables(name);
  if (tools.length === 0) return null;
  const found = [];
  for (const candidates of tools) {
    const executable = await findExecutable(candidates);
    if (!executable) return null;
    found.push(executable);
  }
  const platform = currentPlatform();
  return {
    name,
    version: 'system',
    vendor: 'system',
    vendor_kind: 'system',
    os: platform.os,
    arch: platform.arch,
    path: path.dirname(found[0]),
    source: 'system',
    executables: found,
    sha256: null
  };
}

function systemToolchainExecutables(name) {
  const win = process.platform === 'win32';
  const map = {
    'arm-none-eabi': [
      [win ? 'arm-none-eabi-gcc.exe' : 'arm-none-eabi-gcc'],
      [win ? 'arm-none-eabi-objcopy.exe' : 'arm-none-eabi-objcopy']
    ],
    stlink: [[win ? 'st-flash.exe' : 'st-flash']],
    'arduino-cli': [[win ? 'arduino-cli.exe' : 'arduino-cli']],
    cmake: [[win ? 'cmake.exe' : 'cmake']],
    ninja: [[win ? 'ninja.exe' : 'ninja']]
  };
  return map[name] || [];
}

async function resolvePackageSpec(spec) {
  const parsed = parseSpec(spec);
  if (parsed.version && isExactSemver(parsed.version)) return parsed;
  const versions = await request(`/packages/${parsed.namespace}/${parsed.name}/versions`);
  const range = parsed.version || '*';
  const candidates = (versions.versions || [])
    .map(item => item.version)
    .filter(version => semverSatisfies(version, range))
    .sort(compareSemver)
    .reverse();
  const version = candidates[0];
  if (!version) throw new Error(`No package version found for ${parsed.namespace}/${parsed.name} satisfying ${range}`);
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
      console.log(`  ${green('cached')} package ${normalized.namespace}/${normalized.name}@${normalized.version}`);
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
    console.log(`  ${yellow('refreshing')} cached package ${normalized.namespace}/${normalized.name}@${normalized.version}`);
  }

  const bytes = Buffer.from(await request(`/packages/${normalized.namespace}/${normalized.name}/versions/${normalized.version}/download`));
  const archiveHash = hash(bytes);
  if (registrySha && archiveHash !== registrySha) {
    throw new Error(`Package checksum mismatch for ${normalized.namespace}/${normalized.name}@${normalized.version}`);
  }
  console.log(`  ${green('downloaded')} package archive (${formatBytes(bytes.length)})`);
  console.log(`  ${green('verified')} sha256 ${dim(`${archiveHash.slice(0, 24)}...`)}`);
  await fs.rm(path.join(packageDir, 'files'), { recursive: true, force: true });
  await fs.mkdir(packageDir, { recursive: true });
  await fs.writeFile(path.join(packageDir, `${normalized.name}-${normalized.version}.navpkg`), bytes);
  let bundle = null;
  try {
    bundle = JSON.parse(bytes.toString('utf8'));
  } catch {
    // Non-navpkg archives are still cached and checksum-verified by the prototype.
  }
  const installedManifest = bundle?.manifest || registryVersion?.manifest || null;

  if (Array.isArray(bundle?.files)) {
    const extractDir = path.join(packageDir, 'files');
    for (const file of bundle.files) {
      const out = path.join(extractDir, file.path);
      await fs.mkdir(path.dirname(out), { recursive: true });
      await fs.writeFile(out, Buffer.from(file.content_base64, 'base64'));
    }
    if (isAbiManifest(installedManifest)) {
      validateAbiManifest(installedManifest, extractDir, { phase: 'add' });
      await validateAbiProject(extractDir, installedManifest, { build: false, dependencyOnly: true });
    }
  }

  await fs.writeFile(installPath, JSON.stringify({
    namespace: normalized.namespace,
    name: normalized.name,
    version: normalized.version,
    sha256: archiveHash,
    manifest: installedManifest,
    installed_at: new Date().toISOString()
  }, null, 2));

  return {
    namespace: normalized.namespace,
    name: normalized.name,
    version: normalized.version,
    path: packageDir,
    sha256: archiveHash,
    manifest: installedManifest
  };
}

async function installToolchain(toolchain) {
  const installDir = path.join(TOOLCHAIN_HOME, toolchain.name, toolchain.version, `${toolchain.os}-${toolchain.arch}`);
  const archiveFormat = toolchain.archive_format || 'zip';
  const archivePath = path.join(installDir, `archive.${archiveFormat}`);
  const cached = await readToolchainInstall(installDir, toolchain);
  if (cached) {
    console.log(`  ${green('cached')} ${toolchain.name}@${toolchain.version} ${formatPath(`(${installDir})`)}`);
    return cached;
  }

  await fs.mkdir(path.join(installDir, 'bin'), { recursive: true });
  let bytes = await readCachedArchive(archivePath, toolchain.sha256);
  if (bytes) {
    console.log(`  ${yellow('repairing')} ${toolchain.name}@${toolchain.version} from cached archive`);
  } else {
    console.log(`  ${cyan('downloading')} ${toolchain.name}@${toolchain.version} from registry blob`);
    bytes = Buffer.from(await request(`/toolchains/${toolchain.name}/versions/${toolchain.version}/${toolchain.os}/${toolchain.arch}/download`));
  }
  const archiveHash = hash(bytes);
  if (toolchain.sha256 && toolchain.sha256 !== archiveHash) {
    throw new Error(`Toolchain checksum mismatch for ${toolchain.name}@${toolchain.version}`);
  }
  console.log(`  ${green('verified')} sha256 ${dim(`${archiveHash.slice(0, 24)}...`)}`);
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
  console.log(`  ${green('installed')} into ${formatPath(installDir)}`);
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
    if (process.platform === 'win32') {
      await runCommand('powershell', ['-NoProfile', '-Command', `Expand-Archive -LiteralPath '${archivePath.replace(/'/g, "''")}' -DestinationPath '${extractDir.replace(/'/g, "''")}' -Force`], process.cwd());
    } else {
      try {
        await runCommand('unzip', ['-q', archivePath, '-d', extractDir], process.cwd());
      } catch {
        await runCommand('python3', ['-m', 'zipfile', '-e', archivePath, extractDir], process.cwd());
      }
    }
    return;
  }
  if (archiveFormat === 'tar.gz') {
    await runCommand('tar', ['-xzf', archivePath, '-C', extractDir], process.cwd());
    return;
  }
  if (archiveFormat === 'tar.xz') {
    await runCommand('tar', ['-xJf', archivePath, '-C', extractDir], process.cwd());
    return;
  }
  if (archiveFormat === 'deb') {
    if (process.platform !== 'linux') {
      throw new Error('Debian package extraction is only supported on Linux');
    }
    await runCommand('dpkg-deb', ['-x', archivePath, extractDir], process.cwd());
    return;
  }
  throw new Error(`Unsupported toolchain archive format: ${archiveFormat}`);
}

async function ensureArduinoEsp32(manifest, lock) {
  const arduino = await getArduinoContext(lock);
  await writeArduinoConfig(arduino.config, manifest);
  printStep('checking Arduino package index');
  await runCommand(arduino.cli, ['--config-file', arduino.config, 'core', 'update-index'], process.cwd());
  const core = manifest.arduino_core || 'esp32:esp32';
  const installed = await runCapture(arduino.cli, ['--config-file', arduino.config, 'core', 'list']);
  if (installed.includes(core)) {
    printSuccess(`Arduino core already installed: ${core}`);
    return;
  }
  printStep(`installing Arduino core ${core}`);
  await runCommand(arduino.cli, ['--config-file', arduino.config, 'core', 'install', core], process.cwd());
}

async function buildArduino(projectDir, manifest, lock, outputPath) {
  await ensureArduinoEsp32(manifest, lock);
  const arduino = await getArduinoContext(lock);
  const fqbn = manifest.fqbn || 'esp32:esp32:esp32';
  const outputDir = outputPath;
  await fs.mkdir(outputDir, { recursive: true });
  console.log(`${bold('build system:')} arduino-cli`);
  console.log(`${bold('fqbn:')} ${fqbn}`);
  console.log(`${bold('output:')} ${formatPath(outputDir)}`);
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

  console.log(`${bold('build system:')} native-cpp`);
  console.log(`${bold('compiler:')} ${formatPath(compiler)}`);
  console.log(`${bold('sources:')} ${sources.length}`);
  console.log(`${bold('output:')} ${formatPath(outputPath)}`);

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
  const cmakeToolchain = lock.toolchains.find(item => item.name === 'cmake');
  const ninjaToolchain = lock.toolchains.find(item => item.name === 'ninja');
  const systemCmake = await findExecutable(process.platform === 'win32' ? ['cmake.exe'] : ['cmake']);
  const cmake = systemCmake || (cmakeToolchain ? await findRegistryTool(cmakeToolchain, process.platform === 'win32' ? /^cmake\.exe$/i : /^cmake$/) : null);
  if (!cmake) {
    throw new Error('CMake is required to build this registry base project. Run nav setup to install the registry-managed cmake toolchain.');
  }
  const generator = manifest.cmake_generator || 'Ninja';
  const systemNinja = generator.toLowerCase() === 'ninja'
    ? await findExecutable(process.platform === 'win32' ? ['ninja.exe'] : ['ninja'])
    : null;
  const generatorExe = generator.toLowerCase() === 'ninja'
    ? systemNinja || (ninjaToolchain ? await findRegistryTool(ninjaToolchain, process.platform === 'win32' ? /^ninja\.exe$/i : /^ninja$/) : null)
    : null;
  if (generator.toLowerCase() === 'ninja' && !generatorExe) {
    throw new Error('Ninja is required for this CMake project. Run nav setup to install the registry-managed ninja toolchain.');
  }

  const armToolchain = lock.toolchains.find(item => item.name === 'arm-none-eabi');
  const gcc = armToolchain ? await findRegistryTool(armToolchain, process.platform === 'win32' ? /^arm-none-eabi-gcc\.exe$/i : /^arm-none-eabi-gcc$/) : null;
  const objcopy = armToolchain ? await findRegistryTool(armToolchain, process.platform === 'win32' ? /^arm-none-eabi-objcopy\.exe$/i : /^arm-none-eabi-objcopy$/) : null;
  const toolchainBin = gcc ? path.dirname(gcc) : null;
  const cmakeBin = cmake ? path.dirname(cmake) : null;
  const ninjaBin = generatorExe ? path.dirname(generatorExe) : null;
  const buildDir = path.resolve(projectDir, manifest.cmake_build_dir || 'build/navhal');
  const output = path.resolve(projectDir, outputPath);
  const pathParts = [toolchainBin, cmakeBin, ninjaBin].filter(Boolean);
  const env = {
    ...process.env,
    PATH: pathParts.length ? `${pathParts.join(path.delimiter)}${path.delimiter}${process.env.PATH || ''}` : process.env.PATH
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

  console.log(`${bold('build system:')} cmake`);
  console.log(`${bold('cmake:')} ${formatPath(cmake)}`);
  console.log(`${bold('generator:')} ${generator}`);
  if (toolchainBin) console.log(`${bold('toolchain bin:')} ${formatPath(toolchainBin)}`);
  console.log(`${bold('sample:')} ${manifest.cmake_sample || 'hal_blink'}`);
  console.log(`${bold('build dir:')} ${formatPath(buildDir)}`);
  console.log(`${bold('output:')} ${formatPath(output)}`);

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
  console.log(`${bold('build system:')} stm32-gcc`);
  console.log(`${bold('compiler:')} ${formatPath(gcc)}`);
  console.log(`${bold('linker:')} ${formatPath(linkerScript)}`);
  console.log(`${bold('sources:')} ${filteredSources.length}`);
  console.log(`${bold('elf:')} ${formatPath(elfPath)}`);
  console.log(`${bold('bin:')} ${formatPath(binPath)}`);
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
  console.log(`${bold('flasher:')} ${formatPath(stFlash)}`);
  console.log(`${bold('binary:')} ${formatPath(binPath)}`);
  console.log(`${bold('flash address:')} ${address}`);
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
    console.log(`${bold('flasher:')} ${formatPath(stFlash)}`);
    console.log(`${bold('binary:')} ${formatPath(binPath)}`);
    console.log(`${bold('flash address:')} ${address}`);
    await runCommand(stFlash, ['write', binPath, address], projectDir);
    return;
  }
  throw new Error('CMake upload needs a registry-managed uploader. Add uploader = "stlink" or another supported uploader to nav.toml/navmod.toml.');
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
  if (buildSystem === 'abi-v1' && manifest.target?.vendor === 'stm32') return 'stlink';
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
  printStep(`preparing ST-Link chip database: ${formatPath(expectedConfig)}`);
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
  console.log(`${dim('running:')} ${formatCommand(quoteCommand([command, ...args]))}`);
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
  let projectDir = null;
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
  return { projectDir: requireProjectDir(projectDir), options };
}

function choosePort(ports) {
  if (!ports.length) return null;
  return ports.find(port => /^COM\d+$/i.test(port)) || ports[0];
}

function findProjectRoot(startDir = process.cwd()) {
  const upward = findProjectRootUpward(startDir);
  if (upward) return upward;
  return findProjectRootDownward(startDir);
}

function findProjectRootUpward(startDir = process.cwd()) {
  let current = path.resolve(startDir || process.cwd());
  try {
    const stat = existsSync(current) ? requireStatSync(current) : null;
    if (stat?.isFile()) current = path.dirname(current);
  } catch {}

  for (;;) {
    if (isNavProjectDirectory(current, { allowLockOnly: true })) {
      return current;
    }
    const parent = path.dirname(current);
    if (parent === current) return null;
    current = parent;
  }
}

function findProjectRootDownward(startDir = process.cwd()) {
  const root = path.resolve(startDir || process.cwd());
  const rootStat = requireStatSync(root);
  if (!rootStat?.isDirectory()) return null;
  const queue = [{ dir: root, depth: 0 }];
  let visited = 0;
  while (queue.length > 0 && visited < PROJECT_SEARCH_MAX_DIRS) {
    const { dir, depth } = queue.shift();
    visited += 1;
    if (isNavProjectDirectory(dir, { allowLockOnly: false })) {
      return dir;
    }
    if (depth >= PROJECT_SEARCH_MAX_DEPTH) continue;
    let entries = [];
    try {
      entries = readdirSync(dir, { withFileTypes: true });
    } catch {
      continue;
    }
    for (const entry of entries) {
      if (!entry.isDirectory()) continue;
      if (PROJECT_SEARCH_SKIP_DIRS.has(entry.name)) continue;
      if (entry.name.startsWith('.') && !['.config'].includes(entry.name)) continue;
      queue.push({ dir: path.join(dir, entry.name), depth: depth + 1 });
    }
  }
  return null;
}

function isNavProjectDirectory(dir, { allowLockOnly = false } = {}) {
  if (existsSync(path.join(dir, 'navmod.toml')) || existsSync(path.join(dir, 'nav.toml')) || existsSync(path.join(dir, 'nav.json'))) return true;
  return allowLockOnly && existsSync(path.join(dir, '.nav', 'lock.json'));
}

function requireStatSync(target) {
  try {
    return fsSyncStat(target);
  } catch {
    return null;
  }
}

function resolveExistingProjectDir(projectArg = null) {
  const start = projectArg ? path.resolve(projectArg) : process.cwd();
  return findProjectRoot(start) || start;
}

function requireProjectDir(projectArg = null) {
  const start = projectArg ? path.resolve(projectArg) : process.cwd();
  const root = findProjectRoot(start);
  if (!root) {
    throw new Error(`No Nav project found from ${start} upward or below. Run "nav setup" in a project folder first.`);
  }
  return root;
}

function resolveSetupProjectDir(projectArg = null) {
  const start = projectArg ? path.resolve(projectArg) : process.cwd();
  return findProjectRoot(start) || start;
}

async function readProjectManifest(projectDir) {
  const root = findProjectRoot(projectDir) || path.resolve(projectDir || process.cwd());
  const navmodPath = path.join(root, 'navmod.toml');
  const tomlPath = path.join(root, 'nav.toml');
  const jsonPath = path.join(root, 'nav.json');
  const navmodExists = await exists(navmodPath);
  const tomlExists = await exists(tomlPath);
  const jsonExists = await exists(jsonPath);
  if (navmodExists) return normalizeAbiManifest(parseToml(await fs.readFile(navmodPath, 'utf8')), root, 'navmod.toml');
  if (tomlExists) return enhanceProjectManifest(parseToml(await fs.readFile(tomlPath, 'utf8')), root);
  if (jsonExists) return enhanceProjectManifest(JSON.parse(await fs.readFile(jsonPath, 'utf8')), root);
  throw new Error(`No Nav project manifest found from ${path.resolve(projectDir || process.cwd())} upward or below. Run "nav setup" in an empty folder, or add navmod.toml/nav.toml to the project root.`);
}

function enhanceProjectManifest(manifest, projectDir) {
  if (manifest?.name === 'navhal') {
    manifest.build_system ||= 'cmake';
    manifest.cmake_generator ||= 'Ninja';
    manifest.cmake_sample ||= 'hal_blink';
    manifest.build_output ||= 'build/navhal/samples/portable/01_hal_blink/hal_blink';
    manifest.board ||= null;
    manifest.target ||= 'host';
    manifest.run ||= 'none';
    manifest.flash_address ||= null;
    if (Array.isArray(manifest.toolchains)) {
      manifest.toolchains = manifest.toolchains.filter(item => item !== 'nav-packager');
      manifest.toolchains = unique(['cmake', 'ninja', 'arm-none-eabi', ...manifest.toolchains]);
    } else {
      manifest.toolchains = ['cmake', 'ninja', 'arm-none-eabi'];
    }
    const uploaderToolchain = uploaderToToolchainName(manifest.uploader);
    if (uploaderToolchain) manifest.toolchains = unique([...manifest.toolchains, uploaderToolchain]);
  }
  return manifest;
}

function isAbiManifest(manifest) {
  return Boolean(manifest?.abi_v1 || manifest?.package?.kind || manifest?.abi?.abi_version);
}

function normalizeAbiManifest(parsed, projectDir, manifestFile = 'navmod.toml') {
  const packageInfo = parsed.package || {};
  const abi = parsed.abi || {};
  const target = parsed.target || {};
  const build = parsed.build || {};
  const requires = parsed.requires || {};
  const optionalRequires = requires.optional || {};
  const dependencies = normalizeDependencyMap(parsed.dependencies || {});
  const packageName = packageInfo.name || parsed.name;
  const kind = packageInfo.kind || parsed.kind;
  const targetToolchains = kind === 'app' && target.vendor === 'stm32' ? ['stlink'] : [];
  const targetTriple = kind === 'app' ? targetTripleFromManifest(target) : null;
  return {
    abi_v1: true,
    manifest_file: manifestFile,
    manifest_root: projectDir,
    package_kind: kind,
    name: packageName,
    version: packageInfo.version || parsed.version,
    license: packageInfo.license || parsed.license,
    authors: packageInfo.authors || [],
    description: packageInfo.description || parsed.description || '',
    namespace: packageInfo.namespace || parsed.namespace || null,
    abi,
    abi_version: Number(abi.abi_version),
    hal_api_version: Number(abi.hal_api_version),
    target,
    target_triple: targetTriple,
    board: target.board || parsed.board || null,
    build,
    build_system: 'abi-v1',
    build_output: build.output || (kind === 'module' ? path.join('build', 'abi-v1', `lib${packageName}.a`) : path.join('build', 'abi-v1', packageName || 'app')),
    sources: normalizeStringArray(build.sources),
    include_dirs: normalizeStringArray(build.include_dirs),
    entry: build.entry || null,
    kconfig: build.kconfig || null,
    cmake_extras: build.cmake_extras || null,
    requires_caps: normalizeStringArray(requires.caps),
    optional_caps: normalizeStringArray(optionalRequires.caps),
    dependencies: dependencies.required,
    optional_dependencies: dependencies.optional,
    toolchains: unique(['cmake', 'ninja', 'arm-none-eabi', ...targetToolchains, ...normalizeStringArray(parsed.toolchains)])
  };
}

function normalizeDependencyMap(dependencies) {
  const optional = dependencies.optional && typeof dependencies.optional === 'object' ? dependencies.optional : {};
  const requiredEntries = Object.entries(dependencies)
    .filter(([key]) => key !== 'optional')
    .map(([name, range]) => dependencyEntryToSpec(name, range));
  const optionalEntries = Object.entries(optional).map(([name, range]) => dependencyEntryToSpec(name, range));
  return { required: requiredEntries, optional: optionalEntries };
}

function dependencyEntryToSpec(name, range) {
  const packageName = String(name || '').trim();
  const versionRange = String(range || '*').trim();
  if (packageName.includes('/')) return `${packageName}@${versionRange}`;
  return `nav/${packageName}@${versionRange}`;
}

function targetTripleFromManifest(target = {}) {
  const arch = target.arch || 'host';
  const vendor = target.vendor || 'native';
  const family = target.family || target.board || 'local';
  const board = target.board || family;
  return `${arch}/${vendor}/${family}/${board}`;
}

function normalizeTargetCapabilityKey(target = {}) {
  const arch = target.arch || 'host';
  const vendor = target.vendor || 'native';
  const board = target.board || 'local';
  const family = target.family || board;
  return `${arch}/${vendor}/${family}/${board}`;
}

function capsForTarget(target = {}) {
  const key = normalizeTargetCapabilityKey(target);
  return new Set(TARGET_CAPABILITIES[key] || TARGET_CAPABILITIES[`${target.arch}/${target.vendor}/${target.board}`] || []);
}

function normalizeStringArray(value) {
  if (Array.isArray(value)) return value.map(item => String(item).trim()).filter(Boolean);
  if (value === undefined || value === null || value === '') return [];
  return [String(value).trim()].filter(Boolean);
}

function validateAbiManifest(manifest, projectDir, { phase = 'build', appTarget = null } = {}) {
  const errors = [];
  const pkg = `${manifest.name || '<unnamed>'}@${manifest.version || '<no-version>'}`;
  if (!['module', 'app'].includes(manifest.package_kind)) errors.push('[package].kind must be "module" or "app"');
  if (!/^[a-z][a-z0-9_-]{1,63}$/.test(String(manifest.name || ''))) errors.push('[package].name must match ^[a-z][a-z0-9_-]{1,63}$');
  if (!isSemver(manifest.version)) errors.push('[package].version must be semver MAJOR.MINOR.PATCH');
  if (!manifest.license) errors.push('[package].license is required');
  if (manifest.abi_version !== ABI_VERSION) errors.push(`[abi].abi_version must be ${ABI_VERSION}`);
  if (manifest.hal_api_version !== HAL_API_VERSION) errors.push(`[abi].hal_api_version must equal NavHAL HAL_API_VERSION ${HAL_API_VERSION}`);
  if (manifest.package_kind === 'app' && (!manifest.target?.arch || !manifest.target?.vendor || !manifest.target?.board)) {
    errors.push('apps must pin [target].arch, [target].vendor, and [target].board');
  }
  if (manifest.package_kind === 'module' && (manifest.target?.arch || manifest.target?.vendor || manifest.target?.board)) {
    errors.push('modules must not pin a [target] block; they build against the consuming app target');
  }
  if (manifest.sources.length === 0) errors.push('[build].sources must include at least one source glob');
  for (const cap of [...manifest.requires_caps, ...manifest.optional_caps]) {
    if (!KNOWN_HAL_CAPS.has(cap)) errors.push(`unknown HAL capability ${cap}`);
  }
  if (phase === 'build') {
    const targetCaps = capsForTarget(appTarget || manifest.target || {});
    for (const cap of manifest.requires_caps) {
      if (!targetCaps.has(cap)) errors.push(`target ${targetTripleFromManifest(appTarget || manifest.target || {})} does not provide required cap ${cap}`);
    }
  }
  if (errors.length > 0) {
    throw new Error(`ABI v1 validation failed for ${pkg}: ${errors.join('; ')}`);
  }
  return true;
}


async function readPackageManifest(projectDir) {
  const navmodPath = path.join(projectDir, 'navmod.toml');
  const tomlPath = path.join(projectDir, 'nav.toml');
  const jsonPath = path.join(projectDir, 'nav.json');
  try {
    const parsed = parseToml(await fs.readFile(navmodPath, 'utf8'));
    const manifest = normalizeAbiManifest(parsed, projectDir, 'navmod.toml');
    validateAbiManifest(manifest, projectDir, { phase: 'pack' });
    return {
      manifest,
      manifestFile: 'navmod.toml'
    };
  } catch (error) {
    if (await exists(navmodPath)) throw error;
  }
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
    throw new Error('pack requires the folder to contain navmod.toml, nav.toml, or nav.json');
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
  let current = result;
  for (const rawLine of source.split(/\r?\n/)) {
    const line = stripTomlComment(rawLine).trim();
    if (!line) continue;
    const section = line.match(/^\[([A-Za-z0-9_.-]+)\]$/);
    if (section) {
      current = result;
      for (const part of section[1].split('.')) {
        current[part] ||= {};
        current = current[part];
      }
      continue;
    }
    const equalsIndex = line.indexOf('=');
    if (equalsIndex < 0) continue;
    const key = line.slice(0, equalsIndex).trim();
    const rawValue = line.slice(equalsIndex + 1);
    const parts = parseTomlKey(key);
    let target = current;
    for (const part of parts.slice(0, -1)) {
      target[part] ||= {};
      target = target[part];
    }
    target[parts.at(-1)] = parseTomlValue(rawValue.trim());
  }
  return result;
}

function parseTomlKey(rawKey) {
  const parts = [];
  let current = '';
  let quote = null;
  for (let index = 0; index < rawKey.length; index += 1) {
    const char = rawKey[index];
    if ((char === '"' || char === "'") && rawKey[index - 1] !== '\\') {
      quote = quote === char ? null : quote || char;
      continue;
    }
    if (char === '.' && !quote) {
      parts.push(current.trim());
      current = '';
      continue;
    }
    current += char;
  }
  if (current.trim()) parts.push(current.trim());
  return parts.map(part => part.replace(/\\"/g, '"').replace(/\\'/g, "'"));
}

function stripTomlComment(line) {
  let quote = null;
  for (let index = 0; index < line.length; index += 1) {
    const char = line[index];
    if ((char === '"' || char === "'") && line[index - 1] !== '\\') {
      quote = quote === char ? null : quote || char;
    }
    if (char === '#' && !quote) return line.slice(0, index);
  }
  return line;
}

function parseTomlValue(rawValue) {
  if (rawValue.startsWith('[')) {
    const inner = rawValue.replace(/^\[/, '').replace(/\]$/, '').trim();
    if (!inner) return [];
    return splitTomlArray(inner).map(item => parseTomlValue(item.trim()));
  }
  if ((rawValue.startsWith('"') && rawValue.endsWith('"')) || (rawValue.startsWith("'") && rawValue.endsWith("'"))) {
    return rawValue.slice(1, -1);
  }
  if (rawValue === 'true') return true;
  if (rawValue === 'false') return false;
  if (/^-?\d+$/.test(rawValue)) return Number(rawValue);
  return rawValue;
}

function splitTomlArray(inner) {
  const values = [];
  let current = '';
  let quote = null;
  for (let index = 0; index < inner.length; index += 1) {
    const char = inner[index];
    if ((char === '"' || char === "'") && inner[index - 1] !== '\\') {
      quote = quote === char ? null : quote || char;
    }
    if (char === ',' && !quote) {
      values.push(current);
      current = '';
      continue;
    }
    current += char;
  }
  if (current.trim()) values.push(current);
  return values;
}

function writeToml(manifest) {
  const order = ['name', 'version', 'board', 'target', 'toolchains', 'dependencies'];
  const keys = unique([...order, ...Object.keys(manifest)]);
  const lines = [];
  const tableKeys = [];
  for (const key of keys) {
    const value = manifest[key];
    if (value === undefined || value === null) continue;
    if (isTomlTable(value)) {
      tableKeys.push(key);
      continue;
    }
    lines.push(`${formatTomlKey(key)} = ${formatTomlValue(value)}`);
  }
  for (const key of tableKeys) emitTomlTable(lines, key, manifest[key]);
  return lines.join('\n') + '\n';
}

function emitTomlTable(lines, name, value) {
  if (lines.length && lines.at(-1) !== '') lines.push('');
  lines.push(`[${name}]`);
  const nested = [];
  for (const [key, entry] of Object.entries(value)) {
    if (entry === undefined || entry === null) continue;
    if (isTomlTable(entry)) {
      nested.push([key, entry]);
      continue;
    }
    lines.push(`${formatTomlKey(key)} = ${formatTomlValue(entry)}`);
  }
  for (const [key, entry] of nested) emitTomlTable(lines, `${name}.${key}`, entry);
}

function isTomlTable(value) {
  return value && typeof value === 'object' && !Array.isArray(value);
}

function formatTomlValue(value) {
  if (Array.isArray(value)) return `[${value.map(formatTomlValue).join(', ')}]`;
  if (typeof value === 'boolean') return String(value);
  if (typeof value === 'number') return String(value);
  return JSON.stringify(String(value));
}

function formatTomlKey(key) {
  const value = String(key || '');
  return /^[A-Za-z0-9_.-]+$/.test(value) ? value : JSON.stringify(value);
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

async function sourceTreeHash(root, files) {
  const digest = crypto.createHash('sha256');
  for (const file of files) {
    const rel = path.relative(root, file).replace(/\\/g, '/');
    const bytes = await fs.readFile(file);
    digest.update(rel);
    digest.update('\0');
    digest.update(bytes);
    digest.update('\0');
  }
  return digest.digest('hex');
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
  return true;
}

function parseSpec(spec) {
  if (!spec || !spec.includes('/')) throw new Error('Package spec must be namespace/package or namespace/package@version');
  const [namespace, packageAndVersion] = spec.split('/');
  const [name, version] = packageAndVersion.split('@');
  return { namespace, name, version };
}

function isSemver(value) {
  return /^\d+\.\d+\.\d+(?:-(?:alpha|rc)\.\d+)?$/.test(String(value || ''));
}

function isExactSemver(value) {
  return isSemver(value) && !String(value).includes('x');
}

function parseSemver(value) {
  const match = String(value || '').match(/^(\d+)\.(\d+)\.(\d+)(?:-([A-Za-z]+)\.(\d+))?$/);
  if (!match) return null;
  return {
    major: Number(match[1]),
    minor: Number(match[2]),
    patch: Number(match[3]),
    prerelease: match[4] ? `${match[4]}.${match[5]}` : ''
  };
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

function semverSatisfies(version, range = '*') {
  const normalizedRange = String(range || '*').trim();
  if (normalizedRange === '*' || normalizedRange === 'latest') return isSemver(version);
  if (normalizedRange.startsWith('=')) return version === normalizedRange.slice(1);
  if (normalizedRange.startsWith('>=')) return compareSemver(version, normalizedRange.slice(2)) >= 0;
  if (normalizedRange.startsWith('^')) {
    const base = coercePartialSemver(normalizedRange.slice(1));
    const parsed = parseSemver(version);
    if (!base || !parsed) return false;
    return parsed.major === base.major && compareSemver(version, formatSemver(base)) >= 0;
  }
  if (normalizedRange.startsWith('~')) {
    const base = coercePartialSemver(normalizedRange.slice(1));
    const parsed = parseSemver(version);
    if (!base || !parsed) return false;
    return parsed.major === base.major && parsed.minor === base.minor && compareSemver(version, formatSemver(base)) >= 0;
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

function coercePartialSemver(value) {
  const parts = String(value || '').split('.').map(part => Number(part.replace(/x/i, '0')));
  if (!Number.isFinite(parts[0])) return null;
  return {
    major: parts[0],
    minor: Number.isFinite(parts[1]) ? parts[1] : 0,
    patch: Number.isFinite(parts[2]) ? parts[2] : 0
  };
}

function formatSemver(value) {
  return `${value.major}.${value.minor}.${value.patch}`;
}

function escapeRegExp(value) {
  return String(value).replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
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
  console.error(red(`nav: ${error.message}`));
  process.exitCode = 1;
});
