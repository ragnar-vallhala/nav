-- Core Infrastructure Provisioning
CREATE EXTENSION IF NOT EXISTS "uuid-ossp";

-- 1. Users Authority Layer
CREATE TABLE IF NOT EXISTS users (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    username VARCHAR(64) UNIQUE NOT NULL,
    email VARCHAR(255) UNIQUE NOT NULL,
    password_hash VARCHAR(255) NOT NULL, -- Salted Argon2 digest vector
    email_verified BOOLEAN DEFAULT FALSE,
    verification_code VARCHAR(6),
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

-- 2. High-Security Token Subsystem
CREATE TABLE IF NOT EXISTS tokens (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    user_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    token_prefix VARCHAR(16) NOT NULL,     -- Public identifier (e.g., 'nav_abcd')
    token_hash VARCHAR(255) UNIQUE NOT NULL, -- Mandated cryptographic concealment
    name VARCHAR(64),
    scopes JSONB DEFAULT '{}',              -- Highly flexible dynamic capability mapping
    expires_at TIMESTAMP WITH TIME ZONE,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    last_used_at TIMESTAMP WITH TIME ZONE
);

-- 3. Core Package Hierarchy - The Registry Node (Identity Only)
CREATE TABLE IF NOT EXISTS packages (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    namespace VARCHAR(128) NOT NULL,        -- Critical naming collision bulkhead
    name VARCHAR(255) NOT NULL,
    description TEXT,
    visibility VARCHAR(32) DEFAULT 'public', -- ['public', 'private', 'internal']
    owner_id UUID NOT NULL REFERENCES users(id),
    total_downloads BIGINT DEFAULT 0,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(namespace, name)                 -- Composite collision prevention logic
);

-- 4. Package Version Graph - Immutable Transaction History
CREATE TABLE IF NOT EXISTS package_versions (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    package_id UUID NOT NULL REFERENCES packages(id) ON DELETE CASCADE,
    version VARCHAR(64) NOT NULL,           -- Standard Semantic String
    
    description TEXT,
    checksum_sha256 VARCHAR(128) NOT NULL,  -- Direct content integrity bound
    archive_url TEXT NOT NULL,              -- S3 Canonical Locater
    
    manifest JSONB NOT NULL,                -- Complete dependency metadata lockfile
    dependencies JSONB DEFAULT '[]',        -- Normalized quick-graph JSON lookup
    integrity_signature TEXT,               -- Future-proofed chain trust (GPG/SigStore)
    
    published_by UUID NOT NULL REFERENCES users(id),
    downloads BIGINT DEFAULT 0,
    yanked BOOLEAN DEFAULT FALSE,           -- Replaces DELETE to preserve deterministic reproducibility
    
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(package_id, version)
);

-- 5. Audit Control Silo
CREATE TABLE IF NOT EXISTS audit_logs (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    user_id UUID REFERENCES users(id),
    action TEXT NOT NULL,                    -- e.g. 'package.publish', 'token.revoke'
    metadata JSONB,
    client_ip INET,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

-- 6. Velocity Optimized Indexes
CREATE INDEX IF NOT EXISTS idx_tokens_hash ON tokens(token_hash);
CREATE INDEX IF NOT EXISTS idx_tokens_prefix ON tokens(token_prefix);
CREATE INDEX IF NOT EXISTS idx_versions_pkg ON package_versions(package_id);

-- Advanced GIN Vector Index for ultra-fast localized searching
CREATE INDEX IF NOT EXISTS idx_package_fulltext_search 
ON packages USING GIN(to_tsvector('english', namespace || ' ' || name || ' ' || COALESCE(description, '')));
