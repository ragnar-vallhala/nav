import 'dotenv/config';
import 'express-async-errors';
import express from 'express';
import cors from 'cors';
import cookieParser from 'cookie-parser';
import session from 'express-session';
import connectPgSimple from 'connect-pg-simple';
import passport from 'passport';
import { Strategy as GoogleStrategy } from 'passport-google-oauth20';
import { Strategy as GitHubStrategy } from 'passport-github2';
import { Strategy as FacebookStrategy } from 'passport-facebook';
import bcrypt from 'bcryptjs';
import jwt from 'jsonwebtoken';
import nodemailer from 'nodemailer';
import pg from 'pg';
import * as Minio from 'minio';
import crypto from 'crypto';
import fs from 'fs';
import path from 'path';

const { Pool } = pg;

const isProduction = process.env.NODE_ENV === 'production';
const config = {
  isProduction,
  port: Number(process.env.PORT || 14000),
  frontendUrl: process.env.FRONTEND_URL || 'http://localhost:13000',
  backendUrl: process.env.BACKEND_URL || 'http://localhost:14000',
  jwtSecret: process.env.JWT_SECRET || 'nav_sample_dev_secret_change_me',
  cookieName: process.env.AUTH_COOKIE_NAME || 'nav_token',
  seedCatalog: String(process.env.SEED_CATALOG || 'true') === 'true',
  seedDemoData: String(process.env.SEED_DEMO_DATA || (!isProduction ? 'true' : 'false')) === 'true',
  rootAccount: {
    name: process.env.NAV_ROOT_NAME || process.env.NAV_OWNER_NAME || (!isProduction ? 'Nav Root Administrator' : ''),
    email: process.env.NAV_ROOT_EMAIL || process.env.NAV_OWNER_EMAIL || (!isProduction ? 'root@nav.local' : ''),
    password: process.env.NAV_ROOT_PASSWORD || process.env.NAV_OWNER_PASSWORD || (!isProduction ? 'navrootpass123' : '')
  },
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
const PgSessionStore = connectPgSimple(session);
const PACKAGE_BUCKET = 'nav-packages';
const QUARANTINE_BUCKET = 'nav-quarantine';

function sha256(value) {
  return crypto.createHash('sha256').update(value).digest('hex');
}

function resultSafeId() {
  return crypto.randomUUID();
}

function cliArtifactPath() {
  const candidates = [
    path.join(process.cwd(), 'cli', 'nav.mjs'),
    path.resolve(process.cwd(), '..', 'nav', 'nav.mjs')
  ];
  return candidates.find(candidate => fs.existsSync(candidate)) || candidates[0];
}

function slugify(value) {
  return String(value || '')
    .trim()
    .toLowerCase()
    .replace(/[^a-z0-9._-]+/g, '-')
    .replace(/^-+|-+$/g, '');
}

function publicUser(row) {
  if (!row) return null;
  return {
    id: row.id,
    name: row.name,
    email: row.email,
    avatar_url: row.avatar_url
  };
}

function signToken(user) {
  return jwt.sign(
    { sub: user.id, email: user.email, name: user.name },
    config.jwtSecret,
    { expiresIn: '7d' }
  );
}

function authRequired(req, res, next) {
  const header = req.headers.authorization || '';
  const token = header.startsWith('Bearer ') ? header.slice(7) : req.cookies[config.cookieName];
  if (!token) {
    return res.status(401).json({ error: 'Authentication required' });
  }
  try {
    req.user = jwt.verify(token, config.jwtSecret);
    return next();
  } catch {
    return res.status(401).json({ error: 'Invalid or expired session' });
  }
}

function isAllowedCliRedirectUri(value) {
  try {
    const url = new URL(String(value || ''));
    return url.protocol === 'http:'
      && ['127.0.0.1', 'localhost'].includes(url.hostname)
      && url.pathname === '/callback'
      && Number(url.port) > 0;
  } catch {
    return false;
  }
}

function cliRedirectParams(req) {
  const redirectUri = req.session?.cliRedirect || req.query.cli_redirect;
  if (!isAllowedCliRedirectUri(redirectUri)) return {};
  return {
    cli_redirect: redirectUri,
    state: String(req.session?.cliState || req.query.state || '')
  };
}

function frontendAuthUrl(pathname, params = {}) {
  const url = new URL(pathname, config.frontendUrl);
  Object.entries(params).forEach(([key, value]) => {
    if (value) url.searchParams.set(key, value);
  });
  return url.toString();
}

function loadGoogleSecret() {
  if (process.env.GOOGLE_CLIENT_SECRET) {
    return {
      clientID: process.env.GOOGLE_CLIENT_ID,
      clientSecret: process.env.GOOGLE_CLIENT_SECRET
    };
  }

  const secretFile = process.env.GOOGLE_CLIENT_SECRET_FILE;
  if (!secretFile || !fs.existsSync(secretFile)) {
    return null;
  }

  const raw = JSON.parse(fs.readFileSync(secretFile, 'utf8'));
  const source = raw.web || raw.installed || raw;
  return {
    clientID: process.env.GOOGLE_CLIENT_ID || source.client_id,
    clientSecret: source.client_secret
  };
}

function createMailer() {
  const user = process.env.SMTP_USER;
  const pass = process.env.GOOGLE_APP_PASSWORD || process.env.SMTP_PASSWORD;
  const host = process.env.SMTP_HOST || 'smtp.gmail.com';
  const port = Number(process.env.SMTP_PORT || 587);

  if (!user || !pass) {
    return null;
  }

  return nodemailer.createTransport({
    host,
    port,
    secure: port === 465,
    auth: { user, pass }
  });
}

const mailer = createMailer();

function mailStatus() {
  return {
    configured: Boolean(mailer),
    host: process.env.SMTP_HOST || 'smtp.gmail.com',
    port: Number(process.env.SMTP_PORT || 587),
    user_configured: Boolean(process.env.SMTP_USER),
    password_configured: Boolean(process.env.GOOGLE_APP_PASSWORD || process.env.SMTP_PASSWORD),
    from_name: process.env.EMAIL_FROM_NAME || 'Nav Registry'
  };
}

async function sendWelcomeEmail(user) {
  if (!mailer) {
    return { sent: false, reason: 'SMTP not configured' };
  }

  const fromName = process.env.EMAIL_FROM_NAME || 'Nav Registry';
  const from = `"${fromName}" <${process.env.SMTP_USER}>`;
  await mailer.sendMail({
    from,
    to: user.email,
    subject: 'Welcome to Nav Registry',
    html: `<p>Hello ${user.name},</p><p>Your Nav Registry account is ready.</p>`
  });
  return { sent: true };
}

async function sendOtpEmail({ to, name, otp, purpose }) {
  if (!mailer) {
    return { sent: false, reason: 'SMTP not configured' };
  }

  const fromName = process.env.EMAIL_FROM_NAME || 'Nav Registry';
  const subject = purpose === 'password_reset' ? 'Reset your Nav password' : 'Verify your Nav account';
  const label = purpose === 'password_reset' ? 'password reset' : 'account verification';
  await mailer.sendMail({
    from: `"${fromName}" <${process.env.SMTP_USER}>`,
    to,
    subject,
    html: `
      <p>Hello ${name || to},</p>
      <p>Your Nav ${label} code is:</p>
      <p style="font-size: 24px; font-weight: 700; letter-spacing: 4px;">${otp}</p>
      <p>This code expires in 5 minutes.</p>
    `
  });
  return { sent: true };
}

async function sendNamespaceInviteEmail({ user, namespace, role, invitedBy }) {
  if (!mailer) {
    return { sent: false, reason: 'SMTP not configured' };
  }

  const fromName = process.env.EMAIL_FROM_NAME || 'Nav Registry';
  const from = `"${fromName}" <${process.env.SMTP_USER}>`;
  await mailer.sendMail({
    from,
    to: user.email,
    subject: `You were added to ${namespace} on Nav`,
    html: `
      <p>Hello ${user.name || user.email},</p>
      <p>${invitedBy.email} added you to the <strong>${namespace}</strong> namespace as <strong>${role}</strong>.</p>
      <p>Open ${config.frontendUrl}/invite?namespace=${encodeURIComponent(namespace)}&email=${encodeURIComponent(user.email)} to create an account or sign in before viewing this namespace.</p>
    `
  });
  return { sent: true };
}

function generateOtp() {
  return String(crypto.randomInt(100000, 1000000));
}

async function createOtp({ userId, email, purpose, metadata = {} }) {
  const otp = generateOtp();
  await pool.query(
    `INSERT INTO auth_otps (user_id, email, purpose, otp_hash, metadata, expires_at)
     VALUES ($1, $2, $3, $4, $5, now() + interval '5 minutes')`,
    [userId || null, String(email).toLowerCase(), purpose, sha256(otp), metadata]
  );
  return otp;
}

async function consumeOtp({ email, purpose, otp }) {
  const result = await pool.query(
    `SELECT * FROM auth_otps
     WHERE email = $1 AND purpose = $2 AND otp_hash = $3 AND consumed_at IS NULL AND expires_at > now()
     ORDER BY created_at DESC
     LIMIT 1`,
    [String(email).toLowerCase(), purpose, sha256(String(otp || '').trim())]
  );
  if (result.rowCount === 0) return null;
  await pool.query(`UPDATE auth_otps SET consumed_at = now() WHERE id = $1`, [result.rows[0].id]);
  return result.rows[0];
}

async function waitForDatabase() {
  for (let attempt = 1; attempt <= 30; attempt += 1) {
    try {
      await pool.query('SELECT 1');
      return;
    } catch (error) {
      console.log(`[db] waiting for postgres (${attempt}/30)`);
      await new Promise(resolve => setTimeout(resolve, 1000));
    }
  }
  throw new Error('Postgres did not become ready');
}

async function waitForObjectStorage() {
  for (let attempt = 1; attempt <= 30; attempt += 1) {
    try {
      await minio.listBuckets();
      return;
    } catch (error) {
      console.log(`[storage] waiting for MinIO (${attempt}/30)`);
      await new Promise(resolve => setTimeout(resolve, 1000));
    }
  }
  throw new Error('MinIO did not become ready');
}

async function ensureBucket(bucket) {
  const exists = await minio.bucketExists(bucket).catch(() => false);
  if (!exists) {
    await minio.makeBucket(bucket);
  }
}

async function putSampleObject(bucket, key, body, contentType = 'application/octet-stream') {
  await ensureBucket(bucket);
  const buffer = Buffer.from(body);
  await minio.putObject(bucket, key, buffer, buffer.length, {
    'Content-Type': contentType,
    'x-amz-meta-sha256': sha256(buffer)
  });
  return {
    bucket,
    key,
    size: buffer.length,
    hash: sha256(buffer)
  };
}

async function putRemoteObject(bucket, key, url, contentType = 'application/octet-stream') {
  await ensureBucket(bucket);
  const response = await fetch(url, {
    headers: { 'User-Agent': 'nav-registry/1.0' }
  });
  if (!response.ok) {
    throw new Error(`Failed downloading ${url}: HTTP ${response.status}`);
  }
  const arrayBuffer = await response.arrayBuffer();
  const buffer = Buffer.from(arrayBuffer);
  await minio.putObject(bucket, key, buffer, buffer.length, {
    'Content-Type': response.headers.get('content-type') || contentType,
    'x-amz-meta-sha256': sha256(buffer),
    'x-amz-meta-upstream-url': url
  });
  return {
    bucket,
    key,
    size: buffer.length,
    hash: sha256(buffer)
  };
}

async function seedToolchain({ vendors, vendorSlug, name, description, version, sourceKind, homepageUrl, upstreamUrl, manifest = {}, platforms = ['windows/x64', 'linux/x64', 'darwin/arm64'], platformDownloads = {} }) {
  const vendor = vendors.get(vendorSlug);
  if (!vendor) {
    throw new Error(`Missing toolchain vendor seed: ${vendorSlug}`);
  }

  const toolchainRow = await pool.query(
    `INSERT INTO toolchains (vendor_id, name, description, source_kind, homepage_url)
     VALUES ($1, $2, $3, $4, $5)
     ON CONFLICT (name) DO UPDATE SET
       vendor_id = EXCLUDED.vendor_id,
       description = EXCLUDED.description,
       source_kind = EXCLUDED.source_kind,
       homepage_url = EXCLUDED.homepage_url
     RETURNING *`,
    [vendor.id, name, description, sourceKind, homepageUrl || vendor.homepage_url]
  );

  const versionRow = await pool.query(
    `INSERT INTO toolchain_versions (toolchain_id, version, manifest, release_notes)
     VALUES ($1, $2, $3, $4)
     ON CONFLICT (toolchain_id, version) DO UPDATE SET manifest = EXCLUDED.manifest
     RETURNING *`,
    [
      toolchainRow.rows[0].id,
      version,
      {
        executables: [name],
        env: { PATH: ['bin'] },
        install_mode: 'nav-managed-cache',
        ...manifest
      },
      `${description} (${version})`
    ]
  );

  for (const platform of platforms) {
    const [os, arch] = platform.split('/');
    const downloadUrl = platformDownloads[platform];
    const archiveFormat = downloadUrl?.endsWith('.tar.gz') ? 'tar.gz' : 'zip';
    const archiveKey = `${sourceKind}/${vendor.slug}/${name}/${version}/${os}/${arch}/${name}-${version}.${archiveFormat}`;
    let archive;
    const existing = await pool.query(
      `SELECT size_bytes, sha256, provenance FROM toolchain_artifacts
       WHERE toolchain_version_id = $1 AND os = $2 AND arch = $3`,
      [versionRow.rows[0].id, os, arch]
    );
    const objectStat = await minio.statObject('nav-toolchains', archiveKey).catch(() => null);
    const objectExists = Boolean(objectStat);
    const existingIsReal = Boolean(existing.rows[0]?.provenance?.real_artifact);
    const existingLooksComplete = Number(existing.rows[0]?.size_bytes || 0) > 1024;
    if (downloadUrl && existing.rowCount > 0 && objectExists && existingIsReal && existingLooksComplete) {
      const objectSha = objectStat?.metaData?.sha256 || objectStat?.metaData?.['x-amz-meta-sha256'];
      archive = {
        bucket: 'nav-toolchains',
        key: archiveKey,
        size: Number(objectStat?.size || existing.rows[0].size_bytes),
        hash: objectSha || existing.rows[0].sha256
      };
    } else if (downloadUrl) {
      console.log(`[seed] downloading real toolchain ${name}@${version} ${platform}`);
      archive = await putRemoteObject('nav-toolchains', archiveKey, downloadUrl, archiveFormat === 'zip' ? 'application/zip' : 'application/gzip');
    } else if (config.isProduction) {
      console.warn(`[seed] skipping ${name}@${version} ${platform}; no real upstream artifact has been configured`);
      continue;
    } else {
      const archiveBody = JSON.stringify({
        schema_version: 1,
        type: 'nav-development-toolchain-placeholder',
        name,
        version,
        vendor: vendor.name,
        os,
        arch,
        executables: [name],
        upstream_url: upstreamUrl || homepageUrl || vendor.homepage_url,
        note: 'Development placeholder only. Production does not expose artifacts without a configured upstream archive.'
      }, null, 2);
      archive = await putSampleObject('nav-toolchains', archiveKey, archiveBody, 'application/vnd.nav.toolchain+json');
    }

    await pool.query(
      `INSERT INTO toolchain_artifacts
       (toolchain_version_id, os, arch, archive_format, storage_bucket, storage_key, size_bytes, sha256, signature, upstream_url, provenance)
       VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11)
       ON CONFLICT (toolchain_version_id, os, arch) DO UPDATE SET
         storage_bucket = EXCLUDED.storage_bucket,
         storage_key = EXCLUDED.storage_key,
         archive_format = EXCLUDED.archive_format,
         size_bytes = EXCLUDED.size_bytes,
         sha256 = EXCLUDED.sha256,
         signature = EXCLUDED.signature,
         upstream_url = EXCLUDED.upstream_url,
         provenance = EXCLUDED.provenance`,
      [
        versionRow.rows[0].id,
        os,
        arch,
        archiveFormat,
        archive.bucket,
        archive.key,
        archive.size,
        archive.hash,
        downloadUrl ? null : `development-placeholder-${archive.hash.slice(0, 24)}`,
        downloadUrl || upstreamUrl || homepageUrl || vendor.homepage_url,
        {
          mirrored_by: 'nav-registry',
          source_kind: sourceKind,
          vendor_kind: vendor.kind,
          real_artifact: Boolean(downloadUrl),
          prototype_artifact: !downloadUrl
        }
      ]
    );
  }
}

async function seedPackageVersion({ namespaceId, maintainerId, slug, name, description, version, manifest, files = null, license = 'Apache-2.0', repositoryUrl = null, homepageUrl = null, contentType = 'application/vnd.nav.package+json' }) {
  const packageRow = await pool.query(
    `INSERT INTO packages (namespace_id, name, slug, description, readme, license, repository_url, homepage_url)
     VALUES ($1, $2, $3, $4, $5, $6, $7, $8)
     ON CONFLICT (namespace_id, slug) DO UPDATE SET
       name = EXCLUDED.name,
       description = EXCLUDED.description,
       readme = EXCLUDED.readme,
       license = EXCLUDED.license,
       repository_url = EXCLUDED.repository_url,
       homepage_url = EXCLUDED.homepage_url
     RETURNING *`,
    [
      namespaceId,
      name,
      slug,
      description,
      `# ${name}\n\n${description}`,
      license,
      repositoryUrl,
      homepageUrl
    ]
  );
  const packageId = packageRow.rows[0].id;

  await pool.query(
    `INSERT INTO package_maintainers (package_id, user_id, role)
     VALUES ($1, $2, 'owner')
     ON CONFLICT (package_id, user_id) DO NOTHING`,
    [packageId, maintainerId]
  );

  const archiveKey = `public/nav/${slug}/${version}/${slug}-${version}.navpkg`;
  const archiveBody = JSON.stringify({
    schema_version: 1,
    packed_at: '2026-05-24T00:00:00.000Z',
    manifest: { namespace: 'nav', name: slug, version, ...manifest },
    files: files || [
      {
        path: 'README.md',
        content_base64: Buffer.from(`# ${name}\n\n${description}\n`).toString('base64')
      }
    ]
  }, null, 2);
  const archive = await putSampleObject('nav-packages', archiveKey, archiveBody, contentType);

  const versionRow = await pool.query(
    `INSERT INTO package_versions (package_id, version, manifest, readme, checksum_sha256, published_by)
     VALUES ($1, $2, $3, $4, $5, $6)
     ON CONFLICT (package_id, version) DO UPDATE SET
       manifest = EXCLUDED.manifest,
       readme = EXCLUDED.readme,
       checksum_sha256 = EXCLUDED.checksum_sha256
     RETURNING *`,
    [
      packageId,
      version,
      { namespace: 'nav', name: slug, version, ...manifest },
      `# ${name}\n\n${description}`,
      archive.hash,
      maintainerId
    ]
  );

  await pool.query(
    `INSERT INTO package_artifacts (package_version_id, artifact_type, storage_bucket, storage_key, content_type, size_bytes, sha256)
     VALUES ($1, 'archive', $2, $3, $4, $5, $6)
     ON CONFLICT (storage_bucket, storage_key) DO UPDATE SET
       content_type = EXCLUDED.content_type,
       size_bytes = EXCLUDED.size_bytes,
       sha256 = EXCLUDED.sha256`,
    [versionRow.rows[0].id, archive.bucket, archive.key, contentType, archive.size, archive.hash]
  );

  await pool.query(`DELETE FROM package_dependencies WHERE package_version_id = $1`, [versionRow.rows[0].id]);
  for (const dependency of manifest.dependencies || []) {
    const [depNamespace, nameAndVersion] = dependency.split('/');
    const [depName, versionRequirement = '*'] = String(nameAndVersion || '').split('@');
    if (!depNamespace || !depName) continue;
    await pool.query(
      `INSERT INTO package_dependencies (package_version_id, dependency_namespace, dependency_name, version_requirement, dependency_kind)
       VALUES ($1, $2, $3, $4, 'runtime')`,
      [versionRow.rows[0].id, depNamespace, depName, versionRequirement]
    );
  }
}

async function collectPackageSnapshot(rootDir, options = {}) {
  const ignoredDirectories = new Set(options.ignoredDirectories || ['.git', '.github', '.nav', 'build', 'node_modules']);
  const ignoredPathPrefixes = (options.ignoredPathPrefixes || ['datasheets']).map(item => item.replaceAll('\\', '/').toLowerCase());
  const maxFileBytes = options.maxFileBytes || 1024 * 1024;
  const files = [];

  async function visit(dir) {
    const entries = await fs.promises.readdir(dir, { withFileTypes: true });
    for (const entry of entries) {
      if (ignoredDirectories.has(entry.name)) continue;
      const fullPath = path.join(dir, entry.name);
      const relativePath = path.relative(rootDir, fullPath).replaceAll('\\', '/');
      const normalized = relativePath.toLowerCase();
      if (ignoredPathPrefixes.some(prefix => normalized === prefix || normalized.startsWith(`${prefix}/`))) continue;

      if (entry.isDirectory()) {
        await visit(fullPath);
      } else if (entry.isFile()) {
        const stat = await fs.promises.stat(fullPath);
        if (stat.size > maxFileBytes) continue;
        files.push({
          path: relativePath,
          content_base64: (await fs.promises.readFile(fullPath)).toString('base64')
        });
      }
    }
  }

  await visit(rootDir);
  files.sort((a, b) => a.path.localeCompare(b.path));
  return files;
}

async function getNavHalPackageFiles() {
  const sourceDir = process.env.NAVHAL_SOURCE_DIR || path.join(process.cwd(), 'vendor', 'NavHAL');
  try {
    await fs.promises.access(path.join(sourceDir, 'CMakeLists.txt'));
    const files = await collectPackageSnapshot(sourceDir);
    if (!files.some(file => file.path === 'nav.json' || file.path === 'nav.toml')) {
      files.push({
        path: 'nav.json',
        content_base64: Buffer.from(JSON.stringify({
          name: 'navhal',
          namespace: 'nav',
          version: '0.1.0',
          description: 'Hardware abstraction layer mirrored from ragnar-vallhala/NavHAL.',
          license: 'MIT',
          repository: 'https://github.com/ragnar-vallhala/NavHAL.git',
          kind: 'library',
          language: ['c', 'c++'],
          targets: ['esp32', 'stm32', 'rp2040', 'avr'],
          toolchains: []
        }, null, 2)).toString('base64')
      });
    }
    console.log(`[seed] nav/navhal uses real NavHAL snapshot from ${sourceDir} (${files.length} files)`);
    return files;
  } catch (error) {
    console.warn(`[seed] NavHAL source unavailable at ${sourceDir}; using minimal fallback package. ${error.message}`);
    return [
      {
        path: 'README.md',
        content_base64: Buffer.from('# NavHAL\n\nFallback package. Mount test_database/vendor/NavHAL to seed the full source snapshot.\n').toString('base64')
      },
      {
        path: 'nav.json',
        content_base64: Buffer.from(JSON.stringify({
          name: 'navhal',
          namespace: 'nav',
          version: '0.1.0',
          kind: 'library',
          auto_compile: false
        }, null, 2)).toString('base64')
      }
    ];
  }
}

async function seedEmbeddedCatalog(maintainer, namespaceId) {
  const vendorSeeds = [
    { name: 'Nav', slug: 'nav', kind: 'nav', trust: 'first_party', url: config.frontendUrl },
    { name: 'Arm', slug: 'arm', kind: 'official', trust: 'mirrored_official', url: 'https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads' },
    { name: 'Espressif', slug: 'espressif', kind: 'hardware', trust: 'mirrored_official', url: 'https://docs.espressif.com/projects/esp-idf/' },
    { name: 'Arduino', slug: 'arduino', kind: 'official', trust: 'mirrored_official', url: 'https://docs.arduino.cc/arduino-cli/' },
    { name: 'STMicroelectronics', slug: 'stmicroelectronics', kind: 'hardware', trust: 'mirrored_official', url: 'https://www.st.com' },
    { name: 'stlink-org', slug: 'stlink-org', kind: 'hardware', trust: 'open_source', url: 'https://github.com/stlink-org/stlink' },
    { name: 'Raspberry Pi', slug: 'raspberrypi', kind: 'hardware', trust: 'mirrored_official', url: 'https://github.com/raspberrypi/pico-sdk' },
    { name: 'Nordic Semiconductor', slug: 'nordic', kind: 'hardware', trust: 'mirrored_official', url: 'https://www.nordicsemi.com/Products/Development-tools' },
    { name: 'Zephyr Project', slug: 'zephyr', kind: 'official', trust: 'open_source', url: 'https://docs.zephyrproject.org' },
    { name: 'Microchip', slug: 'microchip', kind: 'hardware', trust: 'mirrored_official', url: 'https://www.microchip.com' },
    { name: 'NXP', slug: 'nxp', kind: 'hardware', trust: 'mirrored_official', url: 'https://www.nxp.com' },
    { name: 'MSYS2', slug: 'msys2', kind: 'official', trust: 'open_source', url: 'https://www.msys2.org' },
    { name: 'AVRDUDE', slug: 'avrdude', kind: 'official', trust: 'open_source', url: 'https://github.com/avrdudes/avrdude' }
  ];

  const vendors = new Map();
  for (const vendor of vendorSeeds) {
    const row = await pool.query(
      `INSERT INTO toolchain_vendors (name, slug, kind, homepage_url, trust_level)
       VALUES ($1, $2, $3, $4, $5)
       ON CONFLICT (slug) DO UPDATE SET
         name = EXCLUDED.name,
         kind = EXCLUDED.kind,
         homepage_url = EXCLUDED.homepage_url,
         trust_level = EXCLUDED.trust_level
       RETURNING *`,
      [vendor.name, vendor.slug, vendor.kind, vendor.url, vendor.trust]
    );
    vendors.set(vendor.slug, row.rows[0]);
  }

  const commonPlatforms = ['windows/x64', 'linux/x64', 'darwin/arm64', 'darwin/x64'];
  const toolchains = [
    {
      vendorSlug: 'nav',
      name: 'nav-packager',
      description: 'Nav package archive and manifest utility.',
      version: '0.1.0',
      sourceKind: 'nav',
      manifest: { package_formats: ['navpkg'], commands: ['pack', 'publish', 'verify'] },
      platforms: commonPlatforms
    },
    {
      vendorSlug: 'arm',
      name: 'arm-none-eabi',
      description: 'Arm GNU embedded compiler toolchain for Cortex-M firmware.',
      version: '14.3.rel1',
      sourceKind: 'official',
      manifest: { targets: ['cortex-m0', 'cortex-m3', 'cortex-m4', 'cortex-m7', 'cortex-m33'], executables: ['arm-none-eabi-gcc', 'arm-none-eabi-g++', 'arm-none-eabi-objcopy'] },
      platforms: commonPlatforms,
      platformDownloads: {
        'windows/x64': 'https://developer.arm.com/-/media/Files/downloads/gnu/14.3.rel1/binrel/arm-gnu-toolchain-14.3.rel1-mingw-w64-x86_64-arm-none-eabi.zip'
      }
    },
    {
      vendorSlug: 'espressif',
      name: 'esp-idf',
      description: 'Espressif IoT Development Framework and managed build tools.',
      version: '5.3.2',
      sourceKind: 'hardware',
      manifest: { targets: ['esp32', 'esp32s3', 'esp32c3'], executables: ['idf.py'], env: { IDF_TOOLS_PATH: ['tools'] } },
      platforms: commonPlatforms
    },
    {
      vendorSlug: 'espressif',
      name: 'xtensa-esp32-elf',
      description: 'Xtensa compiler used by classic ESP32 targets.',
      version: 'esp-14.2.0-20241119',
      sourceKind: 'hardware',
      manifest: { targets: ['esp32'], executables: ['xtensa-esp32-elf-gcc', 'xtensa-esp32-elf-g++'] },
      platforms: commonPlatforms
    },
    {
      vendorSlug: 'espressif',
      name: 'esptool',
      description: 'ESP ROM bootloader flashing utility.',
      version: '4.8.1',
      sourceKind: 'hardware',
      manifest: { targets: ['esp32', 'esp32s3', 'esp32c3'], executables: ['esptool.py', 'esptool'] },
      platforms: commonPlatforms
    },
    {
      vendorSlug: 'arduino',
      name: 'arduino-cli',
      description: 'Arduino command line board/package/build manager.',
      version: '1.5.0',
      sourceKind: 'official',
      manifest: {
        targets: ['avr', 'samd', 'esp32'],
        executables: ['arduino-cli'],
        manages: ['esp32:esp32'],
        board_manager_urls: ['https://espressif.github.io/arduino-esp32/package_esp32_index.json']
      },
      platforms: commonPlatforms,
      platformDownloads: {
        'windows/x64': 'https://github.com/arduino/arduino-cli/releases/download/v1.5.0/arduino-cli_1.5.0_Windows_64bit.zip',
        'linux/x64': 'https://github.com/arduino/arduino-cli/releases/download/v1.5.0/arduino-cli_1.5.0_Linux_64bit.tar.gz',
        'darwin/x64': 'https://github.com/arduino/arduino-cli/releases/download/v1.5.0/arduino-cli_1.5.0_macOS_64bit.tar.gz',
        'darwin/arm64': 'https://github.com/arduino/arduino-cli/releases/download/v1.5.0/arduino-cli_1.5.0_macOS_ARM64.tar.gz'
      }
    },
    {
      vendorSlug: 'stlink-org',
      name: 'stlink',
      description: 'Open-source ST-Link flashing and probing utilities.',
      version: '1.8.0',
      sourceKind: 'hardware',
      manifest: { targets: ['stm32'], executables: ['st-flash', 'st-info'] },
      platforms: commonPlatforms,
      platformDownloads: {
        'windows/x64': 'https://github.com/stlink-org/stlink/releases/download/v1.8.0/stlink-1.8.0-win32.zip'
      }
    },
    {
      vendorSlug: 'zephyr',
      name: 'openocd',
      description: 'Open On-Chip Debugger for flashing and debug adapters.',
      version: '0.12.0',
      sourceKind: 'official',
      manifest: { targets: ['stm32', 'rp2040', 'nrf52'], executables: ['openocd'] },
      platforms: commonPlatforms
    },
    {
      vendorSlug: 'raspberrypi',
      name: 'pico-sdk',
      description: 'Raspberry Pi Pico SDK for RP2040 and RP2350 boards.',
      version: '2.1.1',
      sourceKind: 'hardware',
      manifest: { targets: ['rp2040', 'rp2350'], executables: ['pico-sdk'] },
      platforms: commonPlatforms
    },
    {
      vendorSlug: 'nordic',
      name: 'nrf-command-line-tools',
      description: 'Nordic command-line flashing and device utility tools.',
      version: '10.24.2',
      sourceKind: 'hardware',
      manifest: { targets: ['nrf52', 'nrf53'], executables: ['nrfjprog', 'mergehex'] },
      platforms: commonPlatforms
    },
    {
      vendorSlug: 'avrdude',
      name: 'avrdude',
      description: 'AVR uploader for classic Arduino and AVR boards.',
      version: '8.0',
      sourceKind: 'official',
      manifest: { targets: ['avr'], executables: ['avrdude'] },
      platforms: commonPlatforms
    },
    {
      vendorSlug: 'msys2',
      name: 'mingw-gcc',
      description: 'Native C/C++ compiler used by Nav for production-style local build tests.',
      version: '14.2.0',
      sourceKind: 'official',
      manifest: {
        targets: ['host-cpp', 'native-cpp'],
        executables: ['gcc', 'g++'],
        detector: { windows: ['g++.exe', 'gcc.exe'], linux: ['g++', 'gcc'], darwin: ['clang++', 'clang'] }
      },
      platforms: commonPlatforms
    }
  ];

  for (const toolchain of toolchains) {
    await seedToolchain({
      vendors,
      homepageUrl: vendors.get(toolchain.vendorSlug)?.homepage_url,
      upstreamUrl: vendors.get(toolchain.vendorSlug)?.homepage_url,
      ...toolchain
    });
  }

  const navhalFiles = await getNavHalPackageFiles();
  await seedPackageVersion({
    namespaceId,
    maintainerId: maintainer.id,
    slug: 'navhal',
    name: 'NavHAL',
    description: 'Hardware abstraction layer package mirrored from the real ragnar-vallhala/NavHAL repository.',
    version: '0.1.0',
    license: 'MIT',
    repositoryUrl: 'https://github.com/ragnar-vallhala/NavHAL.git',
    manifest: {
      kind: 'library',
      language: ['c', 'c++'],
      targets: ['esp32', 'stm32', 'rp2040', 'avr'],
      toolchains: [],
      build_system: 'cmake',
      cmake_generator: 'Ninja',
      cmake_sample: 'hal_blink',
      build_output: 'build/navhal/samples/portable/01_hal_blink/hal_blink',
      board: null,
      target: 'host',
      uploader: 'local',
      flash_address: null,
      auto_compile: false,
      include_paths: [
        'include',
        'include/common',
        'include/port/cortex-m4',
        'src/vendor/stm32/family/stm32f4/include'
      ],
      source: {
        type: 'git',
        url: 'https://github.com/ragnar-vallhala/NavHAL.git',
        commit: '2eb92bb3c97204ee59dd2e6610f8dbebd0796926'
      }
    },
    files: navhalFiles
  });

  const boardPackages = [
    ['board-esp32-devkit-v1', 'ESP32 DevKit V1 board definition', 'esp32-devkit-v1', ['arduino-cli'], ['nav/navhal@0.1.0']],
    ['board-esp32-s3-devkitc', 'ESP32-S3 DevKitC board definition', 'esp32-s3-devkitc', ['arduino-cli'], ['nav/navhal@0.1.0']],
    ['board-stm32f401', 'STM32F401 board definition', 'stm32f401', ['arm-none-eabi', 'stlink', 'openocd'], ['nav/navhal@0.1.0']],
    ['board-stm32f103-bluepill', 'STM32F103 Blue Pill board definition', 'stm32f103-bluepill', ['arm-none-eabi', 'stlink', 'openocd'], ['nav/navhal@0.1.0']],
    ['board-arduino-uno-r3', 'Arduino Uno R3 board definition', 'arduino-uno-r3', ['arduino-cli', 'avrdude'], ['nav/navhal@0.1.0']],
    ['board-arduino-nano-33-iot', 'Arduino Nano 33 IoT board definition', 'arduino-nano-33-iot', ['arduino-cli', 'arm-none-eabi'], ['nav/navhal@0.1.0']],
    ['board-raspberry-pi-pico', 'Raspberry Pi Pico board definition', 'raspberry-pi-pico', ['arm-none-eabi', 'pico-sdk', 'openocd'], ['nav/navhal@0.1.0']],
    ['board-rp2040-zero', 'RP2040-Zero board definition', 'rp2040-zero', ['arm-none-eabi', 'pico-sdk', 'openocd'], ['nav/navhal@0.1.0']],
    ['board-nrf52840-dk', 'Nordic nRF52840 DK board definition', 'nrf52840-dk', ['arm-none-eabi', 'nrf-command-line-tools', 'openocd'], ['nav/navhal@0.1.0']],
    ['board-teensy-4-1', 'Teensy 4.1 board definition', 'teensy-4-1', ['arm-none-eabi'], ['nav/navhal@0.1.0']],
    ['board-microchip-samd21', 'Microchip SAMD21 board definition', 'microchip-samd21', ['arm-none-eabi', 'openocd'], ['nav/navhal@0.1.0']]
  ];

  for (const [slug, description, board, boardToolchains, dependencies] of boardPackages) {
    await seedPackageVersion({
      namespaceId,
      maintainerId: maintainer.id,
      slug,
      name: board,
      description,
      version: '0.1.0',
      manifest: {
        kind: 'board',
        board,
        targets: [board.split('-')[0], board],
        toolchains: boardToolchains,
        dependencies,
        frameworks: ['navhal']
      }
    });
  }

  if (config.seedDemoData) {
    await seedPackageVersion({
      namespaceId,
      maintainerId: maintainer.id,
      slug: 'blink-add',
      name: 'Blink Add',
      description: 'STM32 helper package that adds a reusable blink-twice action after a button release.',
      version: '0.1.0',
      manifest: {
        kind: 'library',
        language: ['c'],
        targets: ['stm32f401', 'cortex-m4'],
        toolchains: ['arm-none-eabi', 'stlink'],
        dependencies: ['nav/board-stm32f401@0.1.0'],
        build_system: 'stm32-gcc',
        linker_script: 'linker.ld',
        cpu: 'cortex-m4',
        flash_address: '0x08000000',
        build_output: 'build/blink_hold'
      },
      files: [
        {
          path: 'README.md',
          content_base64: Buffer.from(`# Blink Add\n\nAdds blink_add_blink_twice(), a tiny STM32-friendly helper for package install/build tests.\n`).toString('base64')
        },
        {
          path: 'include/blink_add.h',
          content_base64: Buffer.from(`#ifndef BLINK_ADD_H\n#define BLINK_ADD_H\n\n#include <stdint.h>\n\n#ifdef __cplusplus\nextern "C" {\n#endif\n\ntypedef void (*blink_add_set_led_fn)(int on);\ntypedef void (*blink_add_delay_ms_fn)(uint32_t ms);\n\nvoid blink_add_blink_twice(blink_add_set_led_fn set_led, blink_add_delay_ms_fn delay_ms);\n\n#ifdef __cplusplus\n}\n#endif\n\n#endif\n`).toString('base64')
        },
        {
          path: 'src/blink_add.c',
          content_base64: Buffer.from(`#include "blink_add.h"\n\nvoid blink_add_blink_twice(blink_add_set_led_fn set_led, blink_add_delay_ms_fn delay_ms) {\n    if (!set_led || !delay_ms) {\n        return;\n    }\n\n    for (uint32_t i = 0; i < 2; ++i) {\n        set_led(1);\n        delay_ms(120);\n        set_led(0);\n        delay_ms(120);\n    }\n}\n`).toString('base64')
        }
      ]
    });

    await seedPackageVersion({
      namespaceId,
      maintainerId: maintainer.id,
      slug: 'esp32-blink',
      name: 'ESP32 Blink',
      description: 'Reference blink package for ESP32 projects using NavHAL and ESP-IDF managed tools.',
      version: '0.1.0',
      manifest: {
        kind: 'example',
        board: 'esp32-devkit-v1',
        targets: ['esp32'],
        toolchains: ['arduino-cli'],
        dependencies: ['nav/navhal@0.1.0', 'nav/board-esp32-devkit-v1@0.1.0'],
        build_system: 'arduino-cli',
        fqbn: 'esp32:esp32:esp32',
        arduino_core: 'esp32:esp32',
        board_manager_urls: ['https://espressif.github.io/arduino-esp32/package_esp32_index.json'],
        build_output: 'build/esp32-blink'
      }
    });
  }
}

async function ensureSchema() {
  await pool.query(`CREATE EXTENSION IF NOT EXISTS pgcrypto`);
  await pool.query(`ALTER TABLE users ADD COLUMN IF NOT EXISTS system_role TEXT NOT NULL DEFAULT 'user'`);
  await pool.query(`
    CREATE TABLE IF NOT EXISTS http_sessions (
      sid VARCHAR NOT NULL PRIMARY KEY,
      sess JSON NOT NULL,
      expire TIMESTAMP(6) NOT NULL
    )
  `);
  await pool.query(`CREATE INDEX IF NOT EXISTS idx_http_sessions_expire ON http_sessions(expire)`);
  await pool.query(`
    CREATE TABLE IF NOT EXISTS tokens (
      id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
      user_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
      token_prefix TEXT NOT NULL,
      token_hash TEXT UNIQUE NOT NULL,
      name TEXT,
      scopes JSONB NOT NULL DEFAULT '{}',
      expires_at TIMESTAMPTZ,
      last_used_at TIMESTAMPTZ,
      created_at TIMESTAMPTZ NOT NULL DEFAULT now()
    )
  `);
  await pool.query(`
    CREATE TABLE IF NOT EXISTS auth_otps (
      id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
      user_id UUID REFERENCES users(id) ON DELETE CASCADE,
      email TEXT NOT NULL,
      purpose TEXT NOT NULL,
      otp_hash TEXT NOT NULL,
      metadata JSONB NOT NULL DEFAULT '{}',
      expires_at TIMESTAMPTZ NOT NULL,
      consumed_at TIMESTAMPTZ,
      created_at TIMESTAMPTZ NOT NULL DEFAULT now()
    )
  `);
  await pool.query(`CREATE INDEX IF NOT EXISTS idx_auth_otps_lookup ON auth_otps(email, purpose, created_at DESC)`);
  await pool.query(`ALTER TABLE publish_sessions ADD COLUMN IF NOT EXISTS file_name TEXT`);
  await pool.query(`ALTER TABLE publish_sessions ADD COLUMN IF NOT EXISTS content_type TEXT`);
  await pool.query(`ALTER TABLE publish_sessions ADD COLUMN IF NOT EXISTS size_bytes BIGINT`);
  await pool.query(`ALTER TABLE publish_sessions ADD COLUMN IF NOT EXISTS uploaded_sha256 TEXT`);
  await pool.query(`ALTER TABLE publish_sessions ADD COLUMN IF NOT EXISTS quarantine_storage_key TEXT`);
  await pool.query(`ALTER TABLE publish_sessions ADD COLUMN IF NOT EXISTS manifest JSONB NOT NULL DEFAULT '{}'`);
  await pool.query(`ALTER TABLE publish_sessions ADD COLUMN IF NOT EXISTS readme TEXT`);
  await pool.query(`ALTER TABLE publish_sessions ADD COLUMN IF NOT EXISTS changelog TEXT`);
  await pool.query(`ALTER TABLE publish_sessions ADD COLUMN IF NOT EXISTS integrity_signature TEXT`);
  await pool.query(`
    CREATE TABLE IF NOT EXISTS scan_jobs (
      id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
      subject_type TEXT NOT NULL,
      subject_id UUID NOT NULL,
      status TEXT NOT NULL DEFAULT 'queued',
      priority INT NOT NULL DEFAULT 100,
      attempts INT NOT NULL DEFAULT 0,
      max_attempts INT NOT NULL DEFAULT 3,
      last_error TEXT,
      locked_by TEXT,
      locked_at TIMESTAMPTZ,
      started_at TIMESTAMPTZ,
      finished_at TIMESTAMPTZ,
      created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
      updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
    )
  `);
  await pool.query(`CREATE INDEX IF NOT EXISTS idx_scan_jobs_queue ON scan_jobs(status, priority, created_at)`);
  await pool.query(`ALTER TABLE package_versions ADD COLUMN IF NOT EXISTS changelog TEXT`);
  await pool.query(`ALTER TABLE toolchain_artifacts ALTER COLUMN signature DROP NOT NULL`);
}

async function audit(userId, action, subjectType, subjectId, metadata = {}, req = null) {
  await pool.query(
    `INSERT INTO audit_logs (user_id, action, subject_type, subject_id, metadata, client_ip)
     VALUES ($1, $2, $3, $4, $5, NULLIF($6, '')::inet)`,
    [userId, action, subjectType, subjectId, metadata, req?.ip?.replace('::ffff:', '') || null]
  ).catch(() => null);
}

async function requireNamespaceRole(userId, namespaceName, allowedRoles = ['owner', 'admin', 'maintainer']) {
  const result = await pool.query(`
    SELECT ns.*, nm.role
    FROM namespaces ns
    JOIN namespace_members nm ON nm.namespace_id = ns.id
    WHERE ns.name = $1 AND nm.user_id = $2
  `, [namespaceName, userId]);
  if (result.rowCount === 0 || !allowedRoles.includes(result.rows[0].role)) {
    const error = new Error('You do not have permission for this namespace');
    error.status = 403;
    throw error;
  }
  return result.rows[0];
}

async function requireRegistryPublisher(userId) {
  const result = await pool.query('SELECT system_role FROM users WHERE id = $1', [userId]);
  if (result.rows[0]?.system_role !== 'root') {
    const error = new Error('Root administrator access required for managed toolchains');
    error.status = 403;
    throw error;
  }
}

async function findPackage(namespace, slug) {
  const result = await pool.query(`
    SELECT p.*, ns.name AS namespace
    FROM packages p
    JOIN namespaces ns ON ns.id = p.namespace_id
    WHERE ns.name = $1 AND p.slug = $2
  `, [namespace, slug]);
  return result.rows[0] || null;
}

async function upsertUser({ name, email, password, avatarUrl = null, emailVerified = true, systemRole = 'user' }) {
  const passwordHash = password ? await bcrypt.hash(password, 10) : null;
  const result = await pool.query(
    `INSERT INTO users (name, email, password_hash, email_verified, avatar_url, system_role)
     VALUES ($1, $2, $3, $5, $4, $6)
     ON CONFLICT (email) DO UPDATE SET
       name = EXCLUDED.name,
       avatar_url = COALESCE(EXCLUDED.avatar_url, users.avatar_url),
       system_role = CASE WHEN EXCLUDED.system_role = 'root' THEN 'root' ELSE users.system_role END
     RETURNING *`,
    [name, email, passwordHash, avatarUrl, emailVerified, systemRole]
  );
  return result.rows[0];
}

async function findOrCreateOAuthUser({ provider, providerUserId, email, name, avatarUrl, profile }) {
  const existing = await pool.query(
    `SELECT u.* FROM oauth_accounts oa JOIN users u ON u.id = oa.user_id
     WHERE oa.provider = $1 AND oa.provider_user_id = $2`,
    [provider, providerUserId]
  );
  if (existing.rowCount > 0) {
    return existing.rows[0];
  }

  const user = await upsertUser({ name, email, avatarUrl });
  await pool.query(
    `INSERT INTO oauth_accounts (user_id, provider, provider_user_id, profile)
     VALUES ($1, $2, $3, $4)
     ON CONFLICT (provider, provider_user_id) DO NOTHING`,
    [user.id, provider, providerUserId, profile]
  );
  return user;
}

async function seedRegistry() {
  if (!config.seedCatalog && !config.seedDemoData) {
    return;
  }
  if (!config.rootAccount.email || !config.rootAccount.password) {
    throw new Error('NAV_ROOT_EMAIL and NAV_ROOT_PASSWORD are required when registry seeding is enabled in production');
  }
  const maintainer = await upsertUser({
    name: config.rootAccount.name || 'Nav Root Administrator',
    email: config.rootAccount.email,
    password: config.rootAccount.password,
    systemRole: 'root'
  });

  const namespace = await pool.query(
    `INSERT INTO namespaces (name, owner_user_id, kind)
     VALUES ('nav', $1, 'user')
     ON CONFLICT (name) DO UPDATE SET owner_user_id = EXCLUDED.owner_user_id
     RETURNING *`,
    [maintainer.id]
  );
  const namespaceId = namespace.rows[0].id;

  await pool.query(
    `INSERT INTO namespace_members (namespace_id, user_id, role, invited_by, accepted_at)
     VALUES ($1, $2, 'owner', $2, now())
     ON CONFLICT (namespace_id, user_id) DO UPDATE SET role = EXCLUDED.role`,
    [namespaceId, maintainer.id]
  );

  if (config.seedDemoData) {
    const packages = [
    {
      name: 'STM32 UART Driver',
      slug: 'stm32-uart',
      description: 'A tiny UART driver package for STM32 firmware projects.',
      license: 'MIT',
      version: '1.2.0'
    },
    {
      name: 'Generic IMU HAL',
      slug: 'imu-hal',
      description: 'Reusable IMU abstraction for embedded sensor stacks.',
      license: 'Apache-2.0',
      version: '0.8.1'
    }
    ];

    for (const pkg of packages) {
    const packageRow = await pool.query(
      `INSERT INTO packages (namespace_id, name, slug, description, readme, license, repository_url, homepage_url)
       VALUES ($1, $2, $3, $4, $5, $6, $7, $8)
       ON CONFLICT (namespace_id, slug) DO UPDATE SET description = EXCLUDED.description
       RETURNING *`,
      [
        namespaceId,
        pkg.name,
        pkg.slug,
        pkg.description,
        `# ${pkg.name}\n\n${pkg.description}`,
        pkg.license,
        `https://github.com/navrobotec/${pkg.slug}`,
        `https://registry.nav.local/nav/${pkg.slug}`
      ]
    );
    const packageId = packageRow.rows[0].id;

    await pool.query(
      `INSERT INTO package_maintainers (package_id, user_id, role)
       VALUES ($1, $2, 'owner')
       ON CONFLICT (package_id, user_id) DO NOTHING`,
      [packageId, maintainer.id]
    );

    const archiveBody = `sample archive for nav/${pkg.slug}@${pkg.version}\n`;
    const archiveKey = `public/nav/${pkg.slug}/${pkg.version}/${pkg.slug}-${pkg.version}.tar.zst`;
    const archive = await putSampleObject('nav-packages', archiveKey, archiveBody);

    const versionRow = await pool.query(
      `INSERT INTO package_versions (package_id, version, manifest, readme, checksum_sha256, published_by)
       VALUES ($1, $2, $3, $4, $5, $6)
       ON CONFLICT (package_id, version) DO UPDATE SET checksum_sha256 = EXCLUDED.checksum_sha256
       RETURNING *`,
      [
        packageId,
        pkg.version,
        { name: pkg.slug, version: pkg.version, target: ['stm32', 'cortex-m'] },
        `# ${pkg.name}\n\nVersion ${pkg.version}`,
        archive.hash,
        maintainer.id
      ]
    );
    const versionId = versionRow.rows[0].id;

    await pool.query(
      `INSERT INTO package_artifacts (package_version_id, artifact_type, storage_bucket, storage_key, content_type, size_bytes, sha256)
       VALUES ($1, 'archive', $2, $3, 'application/zstd', $4, $5)
       ON CONFLICT (storage_bucket, storage_key) DO UPDATE SET sha256 = EXCLUDED.sha256, size_bytes = EXCLUDED.size_bytes`,
      [versionId, archive.bucket, archive.key, archive.size, archive.hash]
    );
    }

    const vendors = [
    { name: 'Nav', slug: 'nav', kind: 'nav', trust: 'first_party', url: 'https://nav.local' },
    { name: 'Arm', slug: 'arm', kind: 'official', trust: 'mirrored_official', url: 'https://developer.arm.com' },
    { name: 'STMicroelectronics', slug: 'stmicroelectronics', kind: 'hardware', trust: 'mirrored_official', url: 'https://www.st.com' }
    ];

    const vendorMap = new Map();
    for (const vendor of vendors) {
    const row = await pool.query(
      `INSERT INTO toolchain_vendors (name, slug, kind, homepage_url, trust_level)
       VALUES ($1, $2, $3, $4, $5)
       ON CONFLICT (slug) DO UPDATE SET name = EXCLUDED.name
       RETURNING *`,
      [vendor.name, vendor.slug, vendor.kind, vendor.url, vendor.trust]
    );
    vendorMap.set(vendor.slug, row.rows[0]);
    }

    const toolchains = [
    {
      vendor: 'nav',
      name: 'nav-packager',
      description: 'Nav package archive and manifest utility.',
      version: '0.1.0',
      sourceKind: 'nav',
      os: 'windows',
      arch: 'x64'
    },
    {
      vendor: 'arm',
      name: 'arm-none-eabi',
      description: 'ARM GNU embedded compiler toolchain.',
      version: '14.2.rel1',
      sourceKind: 'official',
      os: 'windows',
      arch: 'x64'
    },
    {
      vendor: 'stmicroelectronics',
      name: 'stlink',
      description: 'ST-Link flashing and probing utilities.',
      version: '1.8.0',
      sourceKind: 'hardware',
      os: 'windows',
      arch: 'x64'
    }
    ];

    for (const tc of toolchains) {
    const vendor = vendorMap.get(tc.vendor);
    const toolchainRow = await pool.query(
      `INSERT INTO toolchains (vendor_id, name, description, source_kind, homepage_url)
       VALUES ($1, $2, $3, $4, $5)
       ON CONFLICT (name) DO UPDATE SET description = EXCLUDED.description
       RETURNING *`,
      [vendor.id, tc.name, tc.description, tc.sourceKind, vendor.homepage_url]
    );

    const versionRow = await pool.query(
      `INSERT INTO toolchain_versions (toolchain_id, version, manifest, release_notes)
       VALUES ($1, $2, $3, $4)
       ON CONFLICT (toolchain_id, version) DO UPDATE SET manifest = EXCLUDED.manifest
       RETURNING *`,
      [
        toolchainRow.rows[0].id,
        tc.version,
        { executables: [tc.name], env: { PATH: ['bin'] } },
        `Sample ${tc.name} ${tc.version}`
      ]
    );

    const archiveKey = `${tc.sourceKind}/${tc.vendor}/${tc.name}/${tc.version}/${tc.os}/${tc.arch}/${tc.name}-${tc.version}.zip`;
    const archive = await putSampleObject('nav-toolchains', archiveKey, `sample toolchain archive for ${tc.name}\n`);

    await pool.query(
      `INSERT INTO toolchain_artifacts
       (toolchain_version_id, os, arch, archive_format, storage_bucket, storage_key, size_bytes, sha256, signature, upstream_url, provenance)
       VALUES ($1, $2, $3, 'zip', $4, $5, $6, $7, $8, $9, $10)
       ON CONFLICT (toolchain_version_id, os, arch) DO UPDATE SET sha256 = EXCLUDED.sha256, storage_key = EXCLUDED.storage_key`,
      [
        versionRow.rows[0].id,
        tc.os,
        tc.arch,
        archive.bucket,
        archive.key,
        archive.size,
        archive.hash,
        `nav-sample-signature-${archive.hash.slice(0, 16)}`,
        vendor.homepage_url,
        { mirrored_by: 'nav-test', source_kind: tc.sourceKind }
      ]
    );
    }
  }

  if (config.seedCatalog) {
    await seedEmbeddedCatalog(maintainer, namespaceId);
  }
}

function configurePassport() {
  const google = loadGoogleSecret();
  if (google?.clientID && google?.clientSecret) {
    passport.use(new GoogleStrategy(
      {
        clientID: google.clientID,
        clientSecret: google.clientSecret,
        callbackURL: process.env.GOOGLE_CALLBACK_URL || `${config.backendUrl}/auth/google/callback`
      },
      async (_accessToken, _refreshToken, profile, done) => {
        try {
          const email = profile.emails?.[0]?.value;
          if (!email) {
            return done(new Error('Google profile did not include an email'));
          }
          const user = await findOrCreateOAuthUser({
            provider: 'google',
            providerUserId: profile.id,
            email,
            name: profile.displayName || email,
            avatarUrl: profile.photos?.[0]?.value,
            profile
          });
          done(null, user);
        } catch (error) {
          done(error);
        }
      }
    ));
  }

  if (process.env.GITHUB_CLIENT_ID && process.env.GITHUB_CLIENT_SECRET) {
    passport.use(new GitHubStrategy(
      {
        clientID: process.env.GITHUB_CLIENT_ID,
        clientSecret: process.env.GITHUB_CLIENT_SECRET,
        callbackURL: process.env.GITHUB_CALLBACK_URL || `${config.backendUrl}/auth/github/callback`,
        scope: ['user:email']
      },
      async (_accessToken, _refreshToken, profile, done) => {
        try {
          const email = profile.emails?.find(item => item.verified)?.value || profile.emails?.[0]?.value || `${profile.username}@github.local`;
          const user = await findOrCreateOAuthUser({
            provider: 'github',
            providerUserId: profile.id,
            email,
            name: profile.displayName || profile.username || email,
            avatarUrl: profile.photos?.[0]?.value,
            profile
          });
          done(null, user);
        } catch (error) {
          done(error);
        }
      }
    ));
  }

  if (process.env.FACEBOOK_CLIENT_ID && process.env.FACEBOOK_CLIENT_SECRET) {
    passport.use(new FacebookStrategy(
      {
        clientID: process.env.FACEBOOK_CLIENT_ID,
        clientSecret: process.env.FACEBOOK_CLIENT_SECRET,
        callbackURL: process.env.FACEBOOK_CALLBACK_URL || `${config.backendUrl}/auth/facebook/callback`,
        profileFields: ['id', 'displayName', 'photos', 'email']
      },
      async (_accessToken, _refreshToken, profile, done) => {
        try {
          const email = profile.emails?.[0]?.value || `${profile.id}@facebook.local`;
          const user = await findOrCreateOAuthUser({
            provider: 'facebook',
            providerUserId: profile.id,
            email,
            name: profile.displayName || email,
            avatarUrl: profile.photos?.[0]?.value,
            profile
          });
          done(null, user);
        } catch (error) {
          done(error);
        }
      }
    ));
  }
}

function oauthSuccessRedirect(req, res) {
  const token = signToken(req.user);
  const params = cliRedirectParams(req);
  if (req.session) {
    delete req.session.cliRedirect;
    delete req.session.cliState;
  }
  res.redirect(frontendAuthUrl('/auth/callback', { token, ...params }));
}

async function main() {
  if (config.isProduction && config.jwtSecret === 'nav_sample_dev_secret_change_me') {
    throw new Error('JWT_SECRET must be set to a strong secret in production');
  }
  await waitForDatabase();
  await ensureSchema();
  await waitForObjectStorage();
  await ensureBucket(PACKAGE_BUCKET);
  await ensureBucket(QUARANTINE_BUCKET);
  await ensureBucket('nav-toolchains');
  await seedRegistry();
  configurePassport();

  const app = express();
  if (config.isProduction) {
    app.set('trust proxy', 1);
  }
  app.use(cors({
    credentials: true,
    origin(origin, callback) {
      if (!origin) return callback(null, true);
      const allowed = origin === config.frontendUrl
        || /^https:\/\/[-a-zA-Z0-9]+\.trycloudflare\.com$/.test(origin)
        || /^http:\/\/(localhost|127\.0\.0\.1):\d+$/.test(origin);
      return callback(allowed ? null : new Error(`CORS blocked origin ${origin}`), allowed);
    }
  }));
  app.use(express.json({ limit: '2mb' }));
  app.use(cookieParser());
  app.use(session({
    store: new PgSessionStore({
      pool,
      tableName: 'http_sessions'
    }),
    name: `${config.cookieName}_oauth`,
    secret: config.jwtSecret,
    resave: false,
    saveUninitialized: false,
    proxy: config.isProduction,
    cookie: {
      httpOnly: true,
      sameSite: 'lax',
      secure: config.isProduction,
      maxAge: 10 * 60 * 1000
    }
  }));
  app.use(passport.initialize());

  app.get('/health', async (_req, res) => {
    const db = await pool.query('SELECT now() as now');
    res.json({
      status: 'healthy',
      database_time: db.rows[0].now,
      minio: 'configured',
      version: '1.0.0'
    });
  });

  app.get('/downloads/nav/nav.mjs', (_req, res) => {
    res.download(cliArtifactPath(), 'nav.mjs');
  });

  app.get('/downloads/nav/install.ps1', (_req, res) => {
    const installer = `$ErrorActionPreference = 'Stop'
if (-not (Get-Command node -ErrorAction SilentlyContinue)) {
  throw 'Nav requires Node.js 20 or later. Install Node.js, then run this installer again.'
}
$installDir = Join-Path $env:LOCALAPPDATA 'Nav\\bin'
New-Item -ItemType Directory -Force -Path $installDir | Out-Null
Invoke-WebRequest -UseBasicParsing -Uri '${config.backendUrl}/downloads/nav/nav.mjs' -OutFile (Join-Path $installDir 'nav.mjs')
@'
@echo off
set "NAV_REGISTRY_URL=${config.backendUrl}"
node "%~dp0nav.mjs" %*
'@ | Set-Content -Encoding Ascii (Join-Path $installDir 'nav.cmd')
$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
if (-not (($userPath -split ';') -contains $installDir)) {
  $nextPath = if ($userPath) { "$userPath;$installDir" } else { $installDir }
  [Environment]::SetEnvironmentVariable('Path', $nextPath, 'User')
}
Write-Host 'Nav installed. Open a new terminal, then run: nav check'
`;
    res.type('text/plain').send(installer);
  });

  app.get('/downloads/nav/install.sh', (_req, res) => {
    const installer = `#!/usr/bin/env sh
set -eu
command -v node >/dev/null 2>&1 || { echo "Nav requires Node.js 20 or later." >&2; exit 1; }
install_dir="\${XDG_DATA_HOME:-$HOME/.local/share}/nav/bin"
bin_dir="$HOME/.local/bin"
mkdir -p "$install_dir" "$bin_dir"
curl -fsSL '${config.backendUrl}/downloads/nav/nav.mjs' -o "$install_dir/nav.mjs"
cat > "$bin_dir/nav" <<'EOF'
#!/usr/bin/env sh
NAV_REGISTRY_URL='${config.backendUrl}' exec node "\${XDG_DATA_HOME:-$HOME/.local/share}/nav/bin/nav.mjs" "$@"
EOF
chmod +x "$bin_dir/nav"
profile_file="$HOME/.profile"
if [ "$(uname -s)" = "Darwin" ]; then
  profile_file="$HOME/.zprofile"
fi
path_line='export PATH="$HOME/.local/bin:$PATH"'
touch "$profile_file"
grep -F "$path_line" "$profile_file" >/dev/null 2>&1 || printf '\\n%s\\n' "$path_line" >> "$profile_file"
export PATH="$HOME/.local/bin:$PATH"
echo "Nav installed. Open a new terminal, or run: . \\"$profile_file\\"; nav check"
`;
    res.type('text/plain').send(installer);
  });

  app.get('/auth/providers', (_req, res) => {
    res.json({
      email_password: true,
      google: Boolean(passport._strategy('google')),
      github: Boolean(passport._strategy('github')),
      facebook: Boolean(passport._strategy('facebook')),
      callbacks: {
        google: `${config.backendUrl}/auth/google/callback`,
        github: `${config.backendUrl}/auth/github/callback`,
        facebook: `${config.backendUrl}/auth/facebook/callback`
      }
    });
  });

  app.get('/auth/cli/start', (req, res) => {
    const redirectUri = req.query.redirect_uri;
    const state = String(req.query.state || '');
    if (!isAllowedCliRedirectUri(redirectUri) || !state) {
      return res.status(400).json({ error: 'Invalid CLI redirect request' });
    }
    res.redirect(frontendAuthUrl('/login', { cli_redirect: redirectUri, state }));
  });

  app.post('/auth/register', async (req, res) => {
    const { name, email, password } = req.body;
    if (!name || !email || !password || password.length < 8) {
      return res.status(400).json({ error: 'Name, email, and password with at least 8 characters are required' });
    }
    const normalizedEmail = email.toLowerCase();
    const existing = await pool.query('SELECT * FROM users WHERE email = $1', [normalizedEmail]);
    if (existing.rowCount > 0 && existing.rows[0].password_hash && existing.rows[0].email_verified) {
      return res.status(409).json({ error: 'Email is already registered' });
    }
    const passwordHash = await bcrypt.hash(password, 10);
    const user = existing.rowCount > 0
      ? (await pool.query(
        `UPDATE users SET name = $1, password_hash = $2, email_verified = false, status = 'pending_verification', updated_at = now()
         WHERE email = $3 RETURNING *`,
        [name, passwordHash, normalizedEmail]
      )).rows[0]
      : (await pool.query(
        `INSERT INTO users (name, email, password_hash, email_verified, status)
         VALUES ($1, $2, $3, false, 'pending_verification') RETURNING *`,
        [name, normalizedEmail, passwordHash]
      )).rows[0];
    const otp = await createOtp({ userId: user.id, email: user.email, purpose: 'email_verify' });
    const mail = await sendOtpEmail({ to: user.email, name: user.name, otp, purpose: 'email_verify' }).catch(error => ({ sent: false, error: error.message }));
    res.status(201).json({
      pending_verification: true,
      email: user.email,
      expires_in_seconds: 300,
      mail
    });
  });

  app.post('/auth/verify-email', async (req, res) => {
    const email = String(req.body.email || '').toLowerCase();
    const otp = req.body.otp;
    const record = await consumeOtp({ email, purpose: 'email_verify', otp });
    if (!record) return res.status(400).json({ error: 'Invalid or expired verification code' });
    const result = await pool.query(
      `UPDATE users SET email_verified = true, status = 'active', updated_at = now()
       WHERE email = $1 RETURNING *`,
      [email]
    );
    if (result.rowCount === 0) return res.status(404).json({ error: 'User not found' });
    await pool.query(`UPDATE namespace_members SET accepted_at = COALESCE(accepted_at, now()) WHERE user_id = $1`, [result.rows[0].id]);
    await sendWelcomeEmail(result.rows[0]).catch(() => null);
    res.json({
      token: signToken(result.rows[0]),
      user: { id: result.rows[0].id, name: result.rows[0].name, email: result.rows[0].email }
    });
  });

  app.post('/auth/verify-email/resend', async (req, res) => {
    const email = String(req.body.email || '').toLowerCase();
    const result = await pool.query(`SELECT * FROM users WHERE email = $1`, [email]);
    if (result.rowCount === 0) return res.json({ sent: true, message: 'If the account exists, a code was sent.' });
    const user = result.rows[0];
    const otp = await createOtp({ userId: user.id, email: user.email, purpose: 'email_verify' });
    const mail = await sendOtpEmail({ to: user.email, name: user.name, otp, purpose: 'email_verify' }).catch(error => ({ sent: false, error: error.message }));
    res.json({ sent: Boolean(mail.sent), mail, expires_in_seconds: 300 });
  });

  app.post('/auth/login', async (req, res) => {
    const { email, password } = req.body;
    if (!email || !password) {
      return res.status(400).json({ error: 'Email and password are required' });
    }
    const result = await pool.query('SELECT * FROM users WHERE email = $1', [email.toLowerCase()]);
    if (result.rowCount === 0 || !result.rows[0].password_hash) {
      return res.status(401).json({ error: 'Invalid email or password' });
    }
    const ok = await bcrypt.compare(password, result.rows[0].password_hash);
    if (!ok) {
      return res.status(401).json({ error: 'Invalid email or password' });
    }
    if (!result.rows[0].email_verified) {
      const otp = await createOtp({ userId: result.rows[0].id, email: result.rows[0].email, purpose: 'email_verify' });
      const mail = await sendOtpEmail({ to: result.rows[0].email, name: result.rows[0].name, otp, purpose: 'email_verify' }).catch(error => ({ sent: false, error: error.message }));
      return res.status(403).json({
        error: 'Email verification required',
        pending_verification: true,
        email: result.rows[0].email,
        mail
      });
    }
    const token = signToken(result.rows[0]);
    res.json({ token, user: { id: result.rows[0].id, name: result.rows[0].name, email: result.rows[0].email } });
  });

  app.post('/auth/password/forgot', async (req, res) => {
    const email = String(req.body.email || '').toLowerCase();
    const result = await pool.query(`SELECT * FROM users WHERE email = $1`, [email]);
    if (result.rowCount > 0) {
      const user = result.rows[0];
      const otp = await createOtp({ userId: user.id, email: user.email, purpose: 'password_reset' });
      await sendOtpEmail({ to: user.email, name: user.name, otp, purpose: 'password_reset' }).catch(() => null);
    }
    res.json({ sent: true, message: 'If the account exists, a password reset code was sent.' });
  });

  app.post('/auth/password/reset', async (req, res) => {
    const email = String(req.body.email || '').toLowerCase();
    const otp = req.body.otp;
    const password = String(req.body.password || '');
    if (password.length < 8) return res.status(400).json({ error: 'Password must be at least 8 characters' });
    const record = await consumeOtp({ email, purpose: 'password_reset', otp });
    if (!record) return res.status(400).json({ error: 'Invalid or expired reset code' });
    const passwordHash = await bcrypt.hash(password, 10);
    const result = await pool.query(
      `UPDATE users SET password_hash = $1, email_verified = true, status = 'active', updated_at = now()
       WHERE email = $2 RETURNING *`,
      [passwordHash, email]
    );
    if (result.rowCount === 0) return res.status(404).json({ error: 'User not found' });
    res.json({
      token: signToken(result.rows[0]),
      user: { id: result.rows[0].id, name: result.rows[0].name, email: result.rows[0].email }
    });
  });

  app.get('/auth/google', (req, res, next) => {
    if (!passport._strategy('google')) {
      return res.status(503).json({ error: 'Google OAuth is not configured' });
    }
    if (isAllowedCliRedirectUri(req.query.cli_redirect)) {
      req.session.cliRedirect = req.query.cli_redirect;
      req.session.cliState = String(req.query.state || '');
      return req.session.save(() => passport.authenticate('google', { scope: ['profile', 'email'], session: false })(req, res, next));
    }
    return passport.authenticate('google', { scope: ['profile', 'email'], session: false })(req, res, next);
  });

  app.get('/auth/google/callback',
    passport.authenticate('google', { session: false, failureRedirect: `${config.frontendUrl}/?oauth=google_failed` }),
    oauthSuccessRedirect
  );

  app.get('/auth/github', (req, res, next) => {
    if (!passport._strategy('github')) {
      return res.status(503).json({ error: 'GitHub OAuth is not configured' });
    }
    if (isAllowedCliRedirectUri(req.query.cli_redirect)) {
      req.session.cliRedirect = req.query.cli_redirect;
      req.session.cliState = String(req.query.state || '');
      return req.session.save(() => passport.authenticate('github', { session: false })(req, res, next));
    }
    return passport.authenticate('github', { session: false })(req, res, next);
  });

  app.get('/auth/github/callback',
    passport.authenticate('github', { session: false, failureRedirect: `${config.frontendUrl}/?oauth=github_failed` }),
    oauthSuccessRedirect
  );

  app.get('/auth/facebook', (req, res, next) => {
    if (!passport._strategy('facebook')) {
      return res.status(503).json({ error: 'Facebook OAuth is not configured' });
    }
    if (isAllowedCliRedirectUri(req.query.cli_redirect)) {
      req.session.cliRedirect = req.query.cli_redirect;
      req.session.cliState = String(req.query.state || '');
      return req.session.save(() => passport.authenticate('facebook', { scope: ['email'], session: false })(req, res, next));
    }
    return passport.authenticate('facebook', { scope: ['email'], session: false })(req, res, next);
  });

  app.get('/auth/facebook/callback',
    passport.authenticate('facebook', { session: false, failureRedirect: `${config.frontendUrl}/?oauth=facebook_failed` }),
    oauthSuccessRedirect
  );

  app.get('/me', authRequired, async (req, res) => {
    const result = await pool.query('SELECT id, name, email, avatar_url, system_role FROM users WHERE id = $1', [req.user.sub]);
    res.json({ user: result.rows[0] });
  });

  app.get('/email/status', authRequired, (_req, res) => {
    res.json(mailStatus());
  });

  app.get('/tokens', authRequired, async (req, res) => {
    const result = await pool.query(
      `SELECT id, token_prefix, name, scopes, expires_at, last_used_at, created_at
       FROM tokens WHERE user_id = $1 ORDER BY created_at DESC`,
      [req.user.sub]
    );
    res.json({ tokens: result.rows });
  });

  app.post('/tokens', authRequired, async (req, res) => {
    const name = req.body.name || 'CLI token';
    const scopes = req.body.scopes || { registry: ['read', 'publish'] };
    const raw = `nav_${crypto.randomBytes(24).toString('base64url')}`;
    const prefix = raw.slice(0, 12);
    const result = await pool.query(
      `INSERT INTO tokens (user_id, token_prefix, token_hash, name, scopes, expires_at)
       VALUES ($1, $2, $3, $4, $5, $6)
       RETURNING id, token_prefix, name, scopes, expires_at, created_at`,
      [req.user.sub, prefix, sha256(raw), name, scopes, req.body.expires_at || null]
    );
    await audit(req.user.sub, 'token.create', 'token', result.rows[0].id, { name }, req);
    res.status(201).json({ token: raw, record: result.rows[0] });
  });

  app.delete('/tokens/:id', authRequired, async (req, res) => {
    const result = await pool.query(
      `DELETE FROM tokens WHERE id = $1 AND user_id = $2 RETURNING id`,
      [req.params.id, req.user.sub]
    );
    if (result.rowCount === 0) return res.status(404).json({ error: 'Token not found' });
    await audit(req.user.sub, 'token.delete', 'token', req.params.id, {}, req);
    res.json({ ok: true });
  });

  app.get('/stats', async (_req, res) => {
    const result = await pool.query(`
      SELECT
        (SELECT COUNT(*)::int FROM packages) AS packages,
        (SELECT COUNT(*)::int FROM package_versions) AS package_versions,
        (SELECT COUNT(*)::int FROM package_artifacts) AS package_artifacts,
        (SELECT COUNT(*)::int FROM toolchains) AS toolchains,
        (SELECT COUNT(*)::int FROM toolchain_artifacts) AS toolchain_artifacts,
        (SELECT COUNT(*)::int FROM users) AS users,
        (SELECT COUNT(*)::int FROM namespaces) AS namespaces,
        (SELECT COUNT(*)::int FROM namespace_members) AS namespace_members,
        (SELECT COUNT(*)::int FROM publish_sessions) AS publish_sessions
    `);
    res.json(result.rows[0]);
  });

  app.get('/namespaces', authRequired, async (req, res) => {
    const result = await pool.query(`
      SELECT ns.id, ns.name, ns.kind, ns.created_at, nm.role,
             (SELECT COUNT(*)::int FROM packages p WHERE p.namespace_id = ns.id) AS packages,
             (SELECT COUNT(*)::int FROM namespace_members nsm WHERE nsm.namespace_id = ns.id) AS members
      FROM namespaces ns
      JOIN namespace_members nm ON nm.namespace_id = ns.id
      WHERE nm.user_id = $1
      ORDER BY ns.created_at DESC
    `, [req.user.sub]);
    res.json({ namespaces: result.rows });
  });

  app.post('/namespaces', authRequired, async (req, res) => {
    const name = slugify(req.body.name);
    if (!name || name.length < 2) {
      return res.status(400).json({ error: 'Namespace name must be at least 2 characters' });
    }
    const result = await pool.query(
      `INSERT INTO namespaces (name, owner_user_id, kind)
       VALUES ($1, $2, $3)
       RETURNING *`,
      [name, req.user.sub, req.body.kind || 'user']
    ).catch(error => {
      if (error.code === '23505') {
        const conflict = new Error('Namespace already exists');
        conflict.status = 409;
        throw conflict;
      }
      throw error;
    });
    await pool.query(
      `INSERT INTO namespace_members (namespace_id, user_id, role, invited_by, accepted_at)
       VALUES ($1, $2, 'owner', $2, now())
       ON CONFLICT (namespace_id, user_id) DO UPDATE SET role = 'owner'`,
      [result.rows[0].id, req.user.sub]
    );
    await audit(req.user.sub, 'namespace.create', 'namespace', result.rows[0].id, { name }, req);
    res.status(201).json({ namespace: result.rows[0] });
  });

  app.get('/namespaces/:namespace/members', authRequired, async (req, res) => {
    await requireNamespaceRole(req.user.sub, req.params.namespace, ['owner', 'admin', 'maintainer', 'viewer']);
    const result = await pool.query(`
      SELECT u.id, u.name, u.email, u.avatar_url, nm.role, nm.invited_at, nm.accepted_at
      FROM namespace_members nm
      JOIN namespaces ns ON ns.id = nm.namespace_id
      JOIN users u ON u.id = nm.user_id
      WHERE ns.name = $1
      ORDER BY nm.created_at ASC
    `, [req.params.namespace]);
    res.json({ members: result.rows });
  });

  app.post('/namespaces/:namespace/members', authRequired, async (req, res) => {
    const namespace = await requireNamespaceRole(req.user.sub, req.params.namespace, ['owner', 'admin']);
    const email = String(req.body.email || '').trim().toLowerCase();
    const role = req.body.role || 'maintainer';
    if (!email || !['owner', 'admin', 'maintainer', 'viewer'].includes(role)) {
      return res.status(400).json({ error: 'Valid email and role are required' });
    }
    let user = (await pool.query('SELECT * FROM users WHERE email = $1', [email])).rows[0];
    if (!user) {
      user = (await pool.query(
        `INSERT INTO users (name, email, email_verified, status)
         VALUES ($1, $2, false, 'invited') RETURNING *`,
        [req.body.name || email.split('@')[0], email]
      )).rows[0];
    }
    await pool.query(
      `INSERT INTO namespace_members (namespace_id, user_id, role, invited_by, accepted_at)
       VALUES ($1, $2, $3, $4, NULL)
       ON CONFLICT (namespace_id, user_id) DO UPDATE SET role = EXCLUDED.role`,
      [namespace.id, user.id, role, req.user.sub]
    );
    const inviter = (await pool.query('SELECT id, name, email FROM users WHERE id = $1', [req.user.sub])).rows[0];
    const mail = await sendNamespaceInviteEmail({
      user,
      namespace: namespace.name,
      role,
      invitedBy: inviter
    }).catch(error => ({ sent: false, error: error.message }));
    await audit(req.user.sub, 'namespace.member.add', 'namespace', namespace.id, { email, role }, req);
    res.status(201).json({ member: { ...publicUser(user), role }, mail });
  });

  app.get('/packages', async (req, res) => {
    const q = `%${req.query.q || ''}%`;
    const result = await pool.query(`
      SELECT ns.name AS namespace, p.name, p.slug, p.description, p.license, p.total_downloads,
             pv.version AS latest_version, pa.sha256, pa.size_bytes
      FROM packages p
      JOIN namespaces ns ON ns.id = p.namespace_id
      LEFT JOIN LATERAL (
        SELECT * FROM package_versions pv WHERE pv.package_id = p.id ORDER BY pv.created_at DESC LIMIT 1
      ) pv ON true
      LEFT JOIN package_artifacts pa ON pa.package_version_id = pv.id AND pa.artifact_type = 'archive'
      WHERE ns.name ILIKE $1 OR p.name ILIKE $1 OR p.slug ILIKE $1 OR p.description ILIKE $1
      ORDER BY p.created_at DESC
      LIMIT 50
    `, [q]);
    res.json({ packages: result.rows });
  });

  app.post('/packages', authRequired, async (req, res) => {
    const namespaceName = slugify(req.body.namespace);
    const namespace = await requireNamespaceRole(req.user.sub, namespaceName);
    const name = String(req.body.name || '').trim();
    const slug = slugify(req.body.slug || name);
    if (!name || !slug) {
      return res.status(400).json({ error: 'Package name and slug are required' });
    }
    const result = await pool.query(
      `INSERT INTO packages (namespace_id, name, slug, description, readme, license, repository_url, homepage_url)
       VALUES ($1, $2, $3, $4, $5, $6, $7, $8)
       RETURNING *`,
      [
        namespace.id,
        name,
        slug,
        req.body.description || '',
        req.body.readme || `# ${name}\n\n${req.body.description || ''}`,
        req.body.license || 'UNLICENSED',
        req.body.repository_url || null,
        req.body.homepage_url || null
      ]
    ).catch(error => {
      if (error.code === '23505') {
        const conflict = new Error('Package already exists in this namespace');
        conflict.status = 409;
        throw conflict;
      }
      throw error;
    });
    await pool.query(
      `INSERT INTO package_maintainers (package_id, user_id, role)
       VALUES ($1, $2, 'owner')
       ON CONFLICT (package_id, user_id) DO NOTHING`,
      [result.rows[0].id, req.user.sub]
    );
    await audit(req.user.sub, 'package.create', 'package', result.rows[0].id, { namespace: namespaceName, slug }, req);
    res.status(201).json({ package: { ...result.rows[0], namespace: namespaceName } });
  });

  app.get('/packages/:namespace/:name', async (req, res) => {
    const pkg = await findPackage(req.params.namespace, req.params.name);
    if (!pkg) {
      return res.status(404).json({ error: 'Package not found' });
    }
    const versions = await pool.query(`
      SELECT pv.version, pv.manifest, pv.readme, pv.changelog, pv.checksum_sha256, pv.yanked, pv.downloads, pv.created_at,
             pa.content_type, pa.size_bytes, pa.sha256
      FROM package_versions pv
      LEFT JOIN package_artifacts pa ON pa.package_version_id = pv.id AND pa.artifact_type = 'archive'
      WHERE pv.package_id = $1
      ORDER BY pv.created_at DESC
    `, [pkg.id]);
    const maintainers = await pool.query(`
      SELECT u.name, pm.role
      FROM package_maintainers pm JOIN users u ON u.id = pm.user_id
      WHERE pm.package_id = $1 ORDER BY pm.created_at ASC
    `, [pkg.id]);
    res.json({ package: pkg, versions: versions.rows, maintainers: maintainers.rows });
  });

  app.get('/packages/:namespace/:name/versions', async (req, res) => {
    const result = await pool.query(`
      SELECT pv.version, pv.changelog, pv.checksum_sha256, pv.yanked, pv.downloads, pv.created_at,
             pa.size_bytes, pa.sha256
      FROM package_versions pv
      JOIN packages p ON p.id = pv.package_id
      JOIN namespaces ns ON ns.id = p.namespace_id
      LEFT JOIN package_artifacts pa ON pa.package_version_id = pv.id AND pa.artifact_type = 'archive'
      WHERE ns.name = $1 AND p.slug = $2
      ORDER BY pv.created_at DESC
    `, [req.params.namespace, req.params.name]);
    res.json({ versions: result.rows });
  });

  app.get('/packages/:namespace/:name/docs', async (req, res) => {
    const pkg = await findPackage(req.params.namespace, req.params.name);
    if (!pkg) return res.status(404).json({ error: 'Package not found' });
    const latest = await pool.query(
      `SELECT version, manifest FROM package_versions WHERE package_id = $1 ORDER BY created_at DESC LIMIT 1`,
      [pkg.id]
    );
    const spec = `${req.params.namespace}/${req.params.name}`;
    res.json({
      package: {
        namespace: req.params.namespace,
        name: req.params.name,
        latest_version: latest.rows[0]?.version || null,
        description: pkg.description,
        license: pkg.license
      },
      commands: {
        install: `nav install ${spec}`,
        install_version: latest.rows[0] ? `nav install ${spec}@${latest.rows[0].version}` : `nav install ${spec}@<version>`,
        info: `nav package info ${spec}`,
        download: `nav download ${spec}`,
        publish: `nav publish ${req.params.namespace} ${req.params.name} <version> <archive.navpkg>`
      },
      manifest: latest.rows[0]?.manifest || {}
    });
  });

  app.post('/packages/:namespace/:name/maintainers', authRequired, async (req, res) => {
    await requireNamespaceRole(req.user.sub, req.params.namespace, ['owner', 'admin']);
    const pkg = await findPackage(req.params.namespace, req.params.name);
    if (!pkg) return res.status(404).json({ error: 'Package not found' });
    const email = String(req.body.email || '').trim().toLowerCase();
    const role = req.body.role || 'maintainer';
    if (!email || !['owner', 'maintainer', 'publisher'].includes(role)) {
      return res.status(400).json({ error: 'Valid email and package role are required' });
    }
    let user = (await pool.query('SELECT * FROM users WHERE email = $1', [email])).rows[0];
    if (!user) {
      user = (await pool.query(
        `INSERT INTO users (name, email, email_verified, status)
         VALUES ($1, $2, false, 'invited') RETURNING *`,
        [req.body.name || email.split('@')[0], email]
      )).rows[0];
    }
    await pool.query(
      `INSERT INTO package_maintainers (package_id, user_id, role)
       VALUES ($1, $2, $3)
       ON CONFLICT (package_id, user_id) DO UPDATE SET role = EXCLUDED.role`,
      [pkg.id, user.id, role]
    );
    await audit(req.user.sub, 'package.maintainer.add', 'package', pkg.id, { email, role }, req);
    res.status(201).json({ maintainer: { ...publicUser(user), role } });
  });

  app.post('/publish/init', authRequired, async (req, res) => {
    const namespaceName = slugify(req.body.namespace);
    const packageSlug = slugify(req.body.package || req.body.name);
    await requireNamespaceRole(req.user.sub, namespaceName);
    const pkg = await findPackage(namespaceName, packageSlug);
    if (!pkg) return res.status(404).json({ error: 'Package not found' });
    const version = String(req.body.version || '').trim();
    if (!version) return res.status(400).json({ error: 'Version is required' });
    const existing = await pool.query(
      `SELECT id FROM package_versions WHERE package_id = $1 AND version = $2`,
      [pkg.id, version]
    );
    if (existing.rowCount > 0) return res.status(409).json({ error: 'Package version already exists and is immutable' });
    const fileName = slugify(req.body.file_name || `${packageSlug}-${version}.tar.zst`);
    const key = `public/${namespaceName}/${packageSlug}/${version}/${fileName}`;
    const quarantineKey = `quarantine/${resultSafeId()}/${namespaceName}/${packageSlug}/${version}/${fileName}`;
    const result = await pool.query(
      `INSERT INTO publish_sessions
       (user_id, package_id, version, status, file_name, content_type, size_bytes, upload_storage_key, quarantine_storage_key, expected_sha256, expires_at)
       VALUES ($1, $2, $3, 'initialized', $4, $5, $6, $7, $8, $9, now() + interval '30 minutes')
       RETURNING *`,
      [
        req.user.sub,
        pkg.id,
        version,
        fileName,
        req.body.content_type || 'application/octet-stream',
        req.body.size_bytes || null,
        key,
        quarantineKey,
        req.body.expected_sha256 || null
      ]
    );
    await audit(req.user.sub, 'publish.init', 'package', pkg.id, { namespace: namespaceName, package: packageSlug, version }, req);
    res.status(201).json({
      session: result.rows[0],
      upload: {
        method: 'PUT',
        url: `${config.backendUrl}/publish/${result.rows[0].id}/upload`,
        expires_at: result.rows[0].expires_at
      }
    });
  });

  app.put('/publish/:sessionId/upload', authRequired, express.raw({ type: '*/*', limit: '100mb' }), async (req, res) => {
    const session = await pool.query(
      `SELECT ps.*, p.slug, ns.name AS namespace
       FROM publish_sessions ps
       JOIN packages p ON p.id = ps.package_id
       JOIN namespaces ns ON ns.id = p.namespace_id
       WHERE ps.id = $1 AND ps.user_id = $2`,
      [req.params.sessionId, req.user.sub]
    );
    if (session.rowCount === 0) return res.status(404).json({ error: 'Publish session not found' });
    if (new Date(session.rows[0].expires_at).getTime() < Date.now()) {
      return res.status(410).json({ error: 'Publish session expired' });
    }
    const buffer = Buffer.isBuffer(req.body) ? req.body : Buffer.from(req.body || '');
    if (buffer.length === 0) return res.status(400).json({ error: 'Upload body is empty' });
    const hash = sha256(buffer);
    if (session.rows[0].expected_sha256 && session.rows[0].expected_sha256 !== hash) {
      return res.status(400).json({ error: 'Uploaded archive SHA256 does not match expected_sha256' });
    }
    await ensureBucket(QUARANTINE_BUCKET);
    const quarantineKey = session.rows[0].quarantine_storage_key || `quarantine/${session.rows[0].id}/${session.rows[0].file_name}`;
    await minio.putObject(QUARANTINE_BUCKET, quarantineKey, buffer, buffer.length, {
      'Content-Type': session.rows[0].content_type || req.headers['content-type'] || 'application/octet-stream',
      'x-amz-meta-sha256': hash
    });
    const updated = await pool.query(
      `UPDATE publish_sessions
       SET status = 'uploaded', size_bytes = $1, uploaded_sha256 = $2, quarantine_storage_key = $3
       WHERE id = $4 RETURNING *`,
      [buffer.length, hash, quarantineKey, session.rows[0].id]
    );
    await audit(req.user.sub, 'publish.upload', 'publish_session', session.rows[0].id, { bytes: buffer.length, sha256: hash }, req);
    res.json({ session: updated.rows[0], sha256: hash, size_bytes: buffer.length });
  });

  app.post('/publish/:sessionId/complete', authRequired, async (req, res) => {
    const session = await pool.query(
      `SELECT ps.*, p.slug, p.id AS resolved_package_id, ns.name AS namespace
       FROM publish_sessions ps
       JOIN packages p ON p.id = ps.package_id
       JOIN namespaces ns ON ns.id = p.namespace_id
       WHERE ps.id = $1 AND ps.user_id = $2`,
      [req.params.sessionId, req.user.sub]
    );
    if (session.rowCount === 0) return res.status(404).json({ error: 'Publish session not found' });
    const item = session.rows[0];
    if (item.status !== 'uploaded') return res.status(409).json({ error: 'Upload archive before completing publish' });
    const changelog = String(req.body.changelog || req.body.manifest?.changelog || '').trim();
    const manifest = { ...(req.body.manifest || {}) };
    if (changelog && !manifest.changelog) {
      manifest.changelog = changelog;
    }
    const existingJob = await pool.query(
      `SELECT id FROM scan_jobs WHERE subject_type = 'publish_session' AND subject_id = $1 AND status IN ('queued', 'running', 'passed')`,
      [item.id]
    );
    if (existingJob.rowCount === 0) {
      await pool.query(
        `INSERT INTO scan_jobs (subject_type, subject_id, status, priority)
         VALUES ('publish_session', $1, 'queued', 100)`,
        [item.id]
      );
    }
    const queued = await pool.query(
      `UPDATE publish_sessions
       SET status = 'queued',
           manifest = $1,
           readme = $2,
           changelog = $3,
           integrity_signature = $4
       WHERE id = $5
       RETURNING *`,
      [manifest, req.body.readme || '', changelog || null, req.body.integrity_signature || null, item.id]
    );
    await audit(req.user.sub, 'publish.queued', 'publish_session', item.id, {
      namespace: item.namespace,
      package: item.slug,
      version: item.version
    }, req);
    res.status(202).json({
      session: queued.rows[0],
      status: 'queued',
      message: 'Archive accepted into quarantine and queued for security scan.'
    });
  });

  app.get('/publish/:sessionId/status', authRequired, async (req, res) => {
    const result = await pool.query(
      `SELECT ps.*, p.slug, ns.name AS namespace,
              sj.id AS scan_job_id, sj.status AS scan_status, sj.last_error AS scan_error,
              pv.id AS package_version_id
       FROM publish_sessions ps
       JOIN packages p ON p.id = ps.package_id
       JOIN namespaces ns ON ns.id = p.namespace_id
       LEFT JOIN scan_jobs sj ON sj.subject_type = 'publish_session' AND sj.subject_id = ps.id
       LEFT JOIN package_versions pv ON pv.package_id = ps.package_id AND pv.version = ps.version
       WHERE ps.id = $1 AND ps.user_id = $2
       ORDER BY sj.created_at DESC
       LIMIT 1`,
      [req.params.sessionId, req.user.sub]
    );
    if (result.rowCount === 0) return res.status(404).json({ error: 'Publish session not found' });
    res.json({ session: result.rows[0] });
  });

  app.get('/packages/:namespace/:name/versions/:version/download', async (req, res) => {
    const result = await pool.query(`
      SELECT pv.id AS version_id, pv.version, p.slug, ns.name AS namespace,
             pa.storage_bucket, pa.storage_key, pa.content_type, pa.size_bytes, pa.sha256
      FROM package_versions pv
      JOIN packages p ON p.id = pv.package_id
      JOIN namespaces ns ON ns.id = p.namespace_id
      JOIN package_artifacts pa ON pa.package_version_id = pv.id AND pa.artifact_type = 'archive'
      WHERE ns.name = $1 AND p.slug = $2 AND pv.version = $3
    `, [req.params.namespace, req.params.name, req.params.version]);
    if (result.rowCount === 0) return res.status(404).json({ error: 'Package version archive not found' });
    const artifact = result.rows[0];
    await pool.query(`UPDATE package_versions SET downloads = downloads + 1 WHERE id = $1`, [artifact.version_id]);
    await pool.query(`UPDATE packages SET total_downloads = total_downloads + 1 WHERE slug = $1`, [req.params.name]);
    await pool.query(
      `INSERT INTO download_events (package_version_id, ip_hash, user_agent) VALUES ($1, $2, $3)`,
      [artifact.version_id, sha256(req.ip || 'local'), req.headers['user-agent'] || 'unknown']
    );
    res.setHeader('Content-Type', artifact.content_type || 'application/octet-stream');
    res.setHeader('Content-Length', artifact.size_bytes);
    res.setHeader('X-Nav-SHA256', artifact.sha256);
    res.setHeader('Content-Disposition', `attachment; filename="${req.params.name}-${artifact.version}.tar.zst"`);
    const stream = await minio.getObject(artifact.storage_bucket, artifact.storage_key);
    stream.pipe(res);
  });

  app.get('/toolchains', async (_req, res) => {
    const result = await pool.query(`
      SELECT tv.name AS vendor, tv.kind AS vendor_kind, tv.trust_level,
             t.name, t.description, t.source_kind, t.status, t.homepage_url,
             tver.version, tver.manifest, ta.os, ta.arch, ta.archive_format,
             ta.size_bytes, ta.sha256, ta.signature, ta.upstream_url, ta.provenance
      FROM toolchains t
      JOIN toolchain_vendors tv ON tv.id = t.vendor_id
      LEFT JOIN LATERAL (
        SELECT * FROM toolchain_versions WHERE toolchain_id = t.id ORDER BY created_at DESC LIMIT 1
      ) tver ON true
      LEFT JOIN toolchain_artifacts ta ON ta.toolchain_version_id = tver.id
      ORDER BY tv.kind, t.name
    `);
    res.json({ toolchains: result.rows });
  });

  app.get('/toolchains/:name/versions/:version/:os/:arch/download', async (req, res) => {
    const result = await pool.query(`
      SELECT ta.id AS artifact_id, t.name, tv.name AS vendor, tver.version,
             ta.os, ta.arch, ta.archive_format, ta.storage_bucket, ta.storage_key,
             ta.size_bytes, ta.sha256
      FROM toolchain_artifacts ta
      JOIN toolchain_versions tver ON tver.id = ta.toolchain_version_id
      JOIN toolchains t ON t.id = tver.toolchain_id
      JOIN toolchain_vendors tv ON tv.id = t.vendor_id
      WHERE t.name = $1 AND tver.version = $2 AND ta.os = $3 AND ta.arch = $4
    `, [req.params.name, req.params.version, req.params.os, req.params.arch]);
    if (result.rowCount === 0) return res.status(404).json({ error: 'Toolchain artifact not found for this platform' });
    const artifact = result.rows[0];
    await pool.query(
      `INSERT INTO download_events (toolchain_artifact_id, ip_hash, user_agent) VALUES ($1, $2, $3)`,
      [artifact.artifact_id, sha256(req.ip || 'local'), req.headers['user-agent'] || 'unknown']
    );
    res.setHeader('Content-Type', 'application/vnd.nav.toolchain+json');
    res.setHeader('Content-Length', artifact.size_bytes);
    res.setHeader('X-Nav-SHA256', artifact.sha256);
    res.setHeader('Content-Disposition', `attachment; filename="${req.params.name}-${artifact.version}-${artifact.os}-${artifact.arch}.${artifact.archive_format}"`);
    const stream = await minio.getObject(artifact.storage_bucket, artifact.storage_key);
    stream.pipe(res);
  });

  app.get('/mirrors/arduino/esp32/package_esp32_index.json', async (_req, res) => {
    const upstream = await fetch('https://espressif.github.io/arduino-esp32/package_esp32_index.json');
    if (!upstream.ok) return res.status(502).json({ error: `Could not fetch upstream ESP32 package index: HTTP ${upstream.status}` });
    const index = await upstream.json();
    const artifacts = await pool.query(`
      SELECT storage_key, size_bytes, sha256
      FROM toolchain_artifacts
      WHERE storage_bucket = 'nav-toolchains'
        AND storage_key LIKE 'hardware/espressif/arduino-esp32/%/windows/x64/%'
    `);
    const byFileName = new Map(artifacts.rows.map(row => [row.storage_key.split('/').pop(), row]));
    const rewrite = item => {
      if (!item?.archiveFileName) return item;
      const artifact = byFileName.get(item.archiveFileName);
      if (!artifact) return item;
      return {
        ...item,
        url: `${config.backendUrl}/mirrors/arduino/esp32/files/${encodeURIComponent(item.archiveFileName)}`,
        checksum: `SHA-256:${artifact.sha256}`,
        size: String(artifact.size_bytes)
      };
    };
    for (const pkg of index.packages || []) {
      if (pkg.name !== 'esp32') continue;
      pkg.platforms = (pkg.platforms || []).map(platform => rewrite(platform));
      pkg.tools = (pkg.tools || []).map(tool => ({
        ...tool,
        systems: (tool.systems || []).map(system => rewrite(system))
      }));
    }
    res.json(index);
  });

  app.get('/mirrors/arduino/esp32/files/:fileName', async (req, res) => {
    const result = await pool.query(`
      SELECT storage_bucket, storage_key, archive_format, size_bytes, sha256
      FROM toolchain_artifacts
      WHERE storage_bucket = 'nav-toolchains'
        AND storage_key LIKE 'hardware/espressif/arduino-esp32/%/windows/x64/' || $1
      ORDER BY created_at DESC
      LIMIT 1
    `, [req.params.fileName]);
    if (result.rowCount === 0) return res.status(404).json({ error: 'Mirrored ESP32 artifact not found' });
    const artifact = result.rows[0];
    res.setHeader('Content-Type', artifact.archive_format === 'zip' ? 'application/zip' : 'application/gzip');
    res.setHeader('Content-Length', artifact.size_bytes);
    res.setHeader('X-Nav-SHA256', artifact.sha256);
    res.setHeader('Content-Disposition', `attachment; filename="${req.params.fileName}"`);
    const stream = await minio.getObject(artifact.storage_bucket, artifact.storage_key);
    stream.pipe(res);
  });

  app.post('/toolchains/publish', authRequired, express.raw({ type: '*/*', limit: '100mb' }), async (req, res) => {
    await requireRegistryPublisher(req.user.sub);
    const metadata = JSON.parse(req.headers['x-nav-toolchain-metadata'] || '{}');
    const vendorSlug = slugify(metadata.vendor_slug || metadata.vendor || 'nav');
    const vendorName = metadata.vendor_name || metadata.vendor || 'Nav';
    const vendorKind = metadata.vendor_kind || 'nav';
    const toolName = slugify(metadata.name);
    const version = String(metadata.version || '').trim();
    const os = metadata.os || 'windows';
    const arch = metadata.arch || 'x64';
    if (!toolName || !version || !['nav', 'official', 'hardware'].includes(vendorKind)) {
      return res.status(400).json({ error: 'Toolchain name, version, and valid vendor_kind are required' });
    }
    if (config.isProduction && (!metadata.signature || !metadata.upstream_url)) {
      return res.status(400).json({ error: 'Production toolchain publication requires signature and upstream_url metadata' });
    }
    const buffer = Buffer.isBuffer(req.body) ? req.body : Buffer.from(req.body || '');
    if (buffer.length === 0) return res.status(400).json({ error: 'Toolchain archive body is empty' });
    const vendor = await pool.query(
      `INSERT INTO toolchain_vendors (name, slug, kind, homepage_url, trust_level)
       VALUES ($1, $2, $3, $4, $5)
       ON CONFLICT (slug) DO UPDATE SET name = EXCLUDED.name, kind = EXCLUDED.kind
       RETURNING *`,
      [vendorName, vendorSlug, vendorKind, metadata.homepage_url || null, metadata.trust_level || 'maintainer_uploaded']
    );
    const toolchain = await pool.query(
      `INSERT INTO toolchains (vendor_id, name, description, source_kind, homepage_url)
       VALUES ($1, $2, $3, $4, $5)
       ON CONFLICT (name) DO UPDATE SET description = EXCLUDED.description
       RETURNING *`,
      [vendor.rows[0].id, toolName, metadata.description || '', vendorKind, metadata.homepage_url || null]
    );
    const toolVersion = await pool.query(
      `INSERT INTO toolchain_versions (toolchain_id, version, manifest, release_notes)
       VALUES ($1, $2, $3, $4)
       RETURNING *`,
      [toolchain.rows[0].id, version, metadata.manifest || {}, metadata.release_notes || '']
    ).catch(error => {
      if (error.code === '23505') {
        const conflict = new Error('Toolchain version already exists');
        conflict.status = 409;
        throw conflict;
      }
      throw error;
    });
    const hash = sha256(buffer);
    const key = `${vendorKind}/${vendorSlug}/${toolName}/${version}/${os}/${arch}/${toolName}-${version}.zip`;
    await ensureBucket('nav-toolchains');
    await minio.putObject('nav-toolchains', key, buffer, buffer.length, {
      'Content-Type': metadata.content_type || 'application/zip',
      'x-amz-meta-sha256': hash
    });
    const artifact = await pool.query(
      `INSERT INTO toolchain_artifacts
       (toolchain_version_id, os, arch, archive_format, storage_bucket, storage_key, size_bytes, sha256, signature, upstream_url, provenance)
       VALUES ($1, $2, $3, $4, 'nav-toolchains', $5, $6, $7, $8, $9, $10)
       RETURNING *`,
      [
        toolVersion.rows[0].id,
        os,
        arch,
        metadata.archive_format || 'zip',
        key,
        buffer.length,
        hash,
        metadata.signature || null,
        metadata.upstream_url || null,
        metadata.provenance || { uploaded_by: req.user.email, ingestion: 'maintainer_upload' }
      ]
    );
    await audit(req.user.sub, 'toolchain.publish', 'toolchain_artifact', artifact.rows[0].id, { toolName, version, vendorKind }, req);
    res.status(201).json({ vendor: vendor.rows[0], toolchain: toolchain.rows[0], version: toolVersion.rows[0], artifact: artifact.rows[0] });
  });

  app.get('/storage/objects', authRequired, async (_req, res) => {
    const packages = await pool.query(`
      SELECT 'package' AS kind, artifact_type, storage_bucket, storage_key, size_bytes, sha256
      FROM package_artifacts
      ORDER BY created_at DESC
    `);
    const toolchains = await pool.query(`
      SELECT 'toolchain' AS kind, 'archive' AS artifact_type, storage_bucket, storage_key, size_bytes, sha256
      FROM toolchain_artifacts
      ORDER BY created_at DESC
    `);
    res.json({ objects: [...packages.rows, ...toolchains.rows] });
  });

  app.get('/audit', authRequired, async (req, res) => {
    const result = await pool.query(`
      SELECT action, subject_type, subject_id, metadata, created_at
      FROM audit_logs
      WHERE user_id = $1
      ORDER BY created_at DESC
      LIMIT 100
    `, [req.user.sub]);
    res.json({ audit_logs: result.rows });
  });

  app.use((error, _req, res, _next) => {
    console.error(error);
    res.status(error.status || 500).json({ error: error.message || 'Internal server error' });
  });

  app.listen(config.port, () => {
    console.log(`[nav-registry] backend listening on ${config.backendUrl}`);
  });
}

main().catch(error => {
  console.error(error);
  process.exit(1);
});
