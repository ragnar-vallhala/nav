# Nav System: MVP Specification & Feature Matrix

This matrix outlines the absolute mandatory minimum requirements to achieve active, self-sustained registry interactions between the distributed CLI and self-hosted infrastructure.

## 1. Nav CLI Core (C++)
The base client tool must transition from mock implementations to active network transactions.

### Package Acquisition & Resolution
- [ ] **Dependency Graph Solver**: Simple DFS resolver taking `nav.toml` constraints and evaluating compatible semantic versions.
- [ ] **Tarball Unpacking Engine**: Integration of `zstd` / `tar` decompression streams to reliably hydrate `extern/` repositories.
- [ ] **Index Synchronization**: Shallow local clone strategy mapped to `~/.nav/registry/index` synced via `libgit2` or shell relays.

### Authorization Mechanics
- [ ] **Credential Silhouette**: Local reader/writer for `~/.nav/credentials.toml` keyed on multi-registry domains.
- [ ] **Auth Interception Layer**: Dynamic injection of `Authorization: Bearer ...` payload headers onto all HTTP traffic.

## 2. Registry Backend (Go)
Transition from static route listeners to transactional orchestrators.

### Persistence Engine (SQL)
- [x] **PostgreSQL Baseline Schema**: 
    - `users` map (Argon2 hashes).
    - `tokens` table (salted hash digests).
    - `packages` registry metadata.
- [x] **Token Handler Logic**: Capability to generate, hash, securely store, and instantly revoke cryptographic opaque identifiers.

### Storage & Sync Flow
- [ ] **MinIO Object Uplink**: Functional handler utilizing the Go SDK to push `.tar.zst` binary streams into configured production buckets.
- [ ] **Pre-Signed URL Dispatcher**: Instead of tunneling uploads, returning signed short-lived storage vectors to the CLI clients.
- [ ] **Git Automation Driver**: Triggers post-upload commit queues which inject exact newline JSON metadata into the standard index vector repository.

## 3. Infrastructure
- [ ] **End-to-End TLS Termination**: Configured proxy server forcing secure orbits for standard remote authentications.
- [ ] **Checksum Matrix**: Hard requirement mapping SHA256 digests against incoming archives to verify chain of custody from upload inception to download terminal.
