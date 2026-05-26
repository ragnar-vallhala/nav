import 'dotenv/config';
import pg from 'pg';
import * as Minio from 'minio';
import crypto from 'crypto';
import os from 'os';

const { Pool } = pg;

const config = {
  workerId: process.env.WORKER_ID || `nav-worker-${os.hostname()}-${process.pid}`,
  pollMs: Number(process.env.WORKER_POLL_MS || 3000),
  maxArchiveBytes: Number(process.env.NAV_SCAN_MAX_ARCHIVE_BYTES || 100 * 1024 * 1024),
  maxExpandedBytes: Number(process.env.NAV_SCAN_MAX_EXPANDED_BYTES || 250 * 1024 * 1024),
  maxFileBytes: Number(process.env.NAV_SCAN_MAX_FILE_BYTES || 25 * 1024 * 1024),
  maxFiles: Number(process.env.NAV_SCAN_MAX_FILES || 5000),
  maxCompressionRatio: Number(process.env.NAV_SCAN_MAX_COMPRESSION_RATIO || 100),
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

function scanNavPackage(buffer) {
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

  checks.push('path-traversal-policy', 'file-count-limit', 'expanded-size-limit', 'secret-patterns');
  return {
    passed: findings.length === 0,
    checks,
    findings,
    expandedBytes,
    fileCount: parsed.files?.length || 0,
    manifest: parsed.manifest || {}
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
  const scan = scanNavPackage(buffer);
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

loop().catch(error => {
  console.error('[worker] fatal', error);
  process.exit(1);
});
