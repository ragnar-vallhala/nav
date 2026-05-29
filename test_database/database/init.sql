CREATE EXTENSION IF NOT EXISTS pgcrypto;

CREATE TABLE IF NOT EXISTS users (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    name TEXT NOT NULL,
    email TEXT UNIQUE NOT NULL,
    password_hash TEXT,
    email_verified BOOLEAN NOT NULL DEFAULT FALSE,
    avatar_url TEXT,
    status TEXT NOT NULL DEFAULT 'active',
    system_role TEXT NOT NULL DEFAULT 'user' CHECK (system_role IN ('user', 'root')),
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS oauth_accounts (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    provider TEXT NOT NULL,
    provider_user_id TEXT NOT NULL,
    profile JSONB NOT NULL DEFAULT '{}',
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    UNIQUE(provider, provider_user_id)
);

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
);

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
);

CREATE TABLE IF NOT EXISTS legal_documents (
    slug TEXT PRIMARY KEY,
    title TEXT NOT NULL,
    body TEXT NOT NULL,
    effective_date DATE NOT NULL DEFAULT CURRENT_DATE,
    updated_by UUID REFERENCES users(id),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS namespaces (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    name TEXT UNIQUE NOT NULL,
    owner_user_id UUID REFERENCES users(id),
    kind TEXT NOT NULL DEFAULT 'user',
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS namespace_members (
    namespace_id UUID NOT NULL REFERENCES namespaces(id) ON DELETE CASCADE,
    user_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    role TEXT NOT NULL,
    invited_by UUID REFERENCES users(id),
    invited_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    accepted_at TIMESTAMPTZ,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    PRIMARY KEY(namespace_id, user_id)
);

CREATE TABLE IF NOT EXISTS packages (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    namespace_id UUID NOT NULL REFERENCES namespaces(id) ON DELETE CASCADE,
    name TEXT NOT NULL,
    slug TEXT NOT NULL,
    description TEXT,
    readme TEXT,
    license TEXT,
    repository_url TEXT,
    homepage_url TEXT,
    visibility TEXT NOT NULL DEFAULT 'public',
    status TEXT NOT NULL DEFAULT 'active',
    total_downloads BIGINT NOT NULL DEFAULT 0,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    UNIQUE(namespace_id, slug)
);

CREATE TABLE IF NOT EXISTS package_maintainers (
    package_id UUID NOT NULL REFERENCES packages(id) ON DELETE CASCADE,
    user_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    role TEXT NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    PRIMARY KEY(package_id, user_id)
);

CREATE TABLE IF NOT EXISTS package_versions (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    package_id UUID NOT NULL REFERENCES packages(id) ON DELETE CASCADE,
    version TEXT NOT NULL,
    manifest JSONB NOT NULL DEFAULT '{}',
    readme TEXT,
    changelog TEXT,
    checksum_sha256 TEXT NOT NULL,
    integrity_signature TEXT,
    published_by UUID REFERENCES users(id),
    yanked BOOLEAN NOT NULL DEFAULT FALSE,
    yank_reason TEXT,
    downloads BIGINT NOT NULL DEFAULT 0,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    UNIQUE(package_id, version)
);

CREATE TABLE IF NOT EXISTS package_artifacts (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    package_version_id UUID NOT NULL REFERENCES package_versions(id) ON DELETE CASCADE,
    artifact_type TEXT NOT NULL,
    storage_bucket TEXT NOT NULL,
    storage_key TEXT NOT NULL,
    content_type TEXT,
    size_bytes BIGINT NOT NULL,
    sha256 TEXT NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    UNIQUE(storage_bucket, storage_key)
);

CREATE UNIQUE INDEX IF NOT EXISTS one_primary_archive_per_package_version
ON package_artifacts(package_version_id)
WHERE artifact_type = 'archive';

CREATE TABLE IF NOT EXISTS package_dependencies (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    package_version_id UUID NOT NULL REFERENCES package_versions(id) ON DELETE CASCADE,
    dependency_namespace TEXT NOT NULL,
    dependency_name TEXT NOT NULL,
    version_requirement TEXT NOT NULL,
    dependency_kind TEXT NOT NULL DEFAULT 'runtime'
);

CREATE TABLE IF NOT EXISTS toolchain_vendors (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    name TEXT NOT NULL,
    slug TEXT UNIQUE NOT NULL,
    kind TEXT NOT NULL,
    homepage_url TEXT,
    trust_level TEXT NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS toolchains (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    vendor_id UUID NOT NULL REFERENCES toolchain_vendors(id) ON DELETE CASCADE,
    name TEXT UNIQUE NOT NULL,
    description TEXT,
    source_kind TEXT NOT NULL,
    homepage_url TEXT,
    status TEXT NOT NULL DEFAULT 'active'
);

CREATE TABLE IF NOT EXISTS toolchain_versions (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    toolchain_id UUID NOT NULL REFERENCES toolchains(id) ON DELETE CASCADE,
    version TEXT NOT NULL,
    manifest JSONB NOT NULL DEFAULT '{}',
    release_notes TEXT,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    UNIQUE(toolchain_id, version)
);

CREATE TABLE IF NOT EXISTS toolchain_artifacts (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    toolchain_version_id UUID NOT NULL REFERENCES toolchain_versions(id) ON DELETE CASCADE,
    os TEXT NOT NULL,
    arch TEXT NOT NULL,
    archive_format TEXT NOT NULL,
    storage_bucket TEXT NOT NULL,
    storage_key TEXT NOT NULL,
    size_bytes BIGINT NOT NULL,
    sha256 TEXT NOT NULL,
    signature TEXT,
    sbom_storage_key TEXT,
    upstream_url TEXT,
    provenance JSONB NOT NULL DEFAULT '{}',
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    UNIQUE(toolchain_version_id, os, arch),
    UNIQUE(storage_bucket, storage_key)
);

CREATE TABLE IF NOT EXISTS publish_sessions (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id UUID REFERENCES users(id),
    package_id UUID REFERENCES packages(id),
    version TEXT NOT NULL,
    status TEXT NOT NULL,
    file_name TEXT,
    content_type TEXT,
    size_bytes BIGINT,
    upload_storage_key TEXT NOT NULL,
    quarantine_storage_key TEXT,
    expected_sha256 TEXT,
    uploaded_sha256 TEXT,
    manifest JSONB NOT NULL DEFAULT '{}',
    readme TEXT,
    changelog TEXT,
    integrity_signature TEXT,
    expires_at TIMESTAMPTZ NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

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
);

CREATE INDEX IF NOT EXISTS idx_scan_jobs_queue ON scan_jobs(status, priority, created_at);

CREATE TABLE IF NOT EXISTS scan_job_events (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    scan_job_id UUID REFERENCES scan_jobs(id) ON DELETE CASCADE,
    phase TEXT NOT NULL,
    level TEXT NOT NULL DEFAULT 'info',
    message TEXT NOT NULL,
    metadata JSONB NOT NULL DEFAULT '{}',
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_scan_job_events_recent ON scan_job_events(created_at DESC);
CREATE INDEX IF NOT EXISTS idx_scan_job_events_job ON scan_job_events(scan_job_id, created_at);

CREATE TABLE IF NOT EXISTS security_scans (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    subject_type TEXT NOT NULL,
    subject_id UUID NOT NULL,
    scanner TEXT NOT NULL,
    status TEXT NOT NULL,
    result JSONB NOT NULL DEFAULT '{}',
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS download_events (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    package_version_id UUID REFERENCES package_versions(id),
    toolchain_artifact_id UUID REFERENCES toolchain_artifacts(id),
    ip_hash TEXT,
    user_agent TEXT,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS audit_logs (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id UUID REFERENCES users(id),
    action TEXT NOT NULL,
    subject_type TEXT,
    subject_id UUID,
    metadata JSONB NOT NULL DEFAULT '{}',
    client_ip INET,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS http_sessions (
    sid VARCHAR NOT NULL PRIMARY KEY,
    sess JSON NOT NULL,
    expire TIMESTAMP(6) NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_packages_search ON packages USING GIN(to_tsvector('english', name || ' ' || slug || ' ' || COALESCE(description, '')));
CREATE INDEX IF NOT EXISTS idx_tokens_user ON tokens(user_id, created_at DESC);
CREATE INDEX IF NOT EXISTS idx_namespace_members_user ON namespace_members(user_id);
CREATE INDEX IF NOT EXISTS idx_package_versions_package ON package_versions(package_id, created_at DESC);
CREATE INDEX IF NOT EXISTS idx_package_artifacts_version ON package_artifacts(package_version_id, artifact_type);
CREATE INDEX IF NOT EXISTS idx_toolchain_artifacts_lookup ON toolchain_artifacts(os, arch);
CREATE INDEX IF NOT EXISTS idx_audit_logs_action_time ON audit_logs(action, created_at DESC);
CREATE INDEX IF NOT EXISTS idx_http_sessions_expire ON http_sessions(expire);
