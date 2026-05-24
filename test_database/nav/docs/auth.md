# Nav Registry Authentication Architecture

## Core Philosophy
The authentication system must remain stateless, registry-specific, and inherently revocable without heavy cryptography rotate keys on clients. **Nav CLI must NEVER store raw user passwords.**

### Opaque Token Standard
We enforce opaque, DB-backed API tokens rather than static long-lived JWTs to enable instantaneous server-side revocation and tight scoped permission rotations.

**Format**: `nav_[a-zA-Z0-9]{32,}`
**Example**: `nav_h8d92jsk82jsL0xP9R7z3a`

## 1. Credentials Subsystem
The client cache is partitioned logically by registry domain to seamlessly facilitate corporate, secure private, and public registries concurrently.

### Storage Configuration
**Location**: `~/.nav/credentials.toml`

```toml
[registries.default]
url = "https://registry.nav.sh"
token = "nav_x8F29skL0xP9"

[registries.enterprise]
url = "https://registry.company.local"
token = "nav_z44K8sL2xY0"
```

## 2. Handshake Flow
Standard authentication negotiation executed over strictly enforced HTTPS.

1. **Identification**: User invokes `nav login`.
2. **Assertion**: Backend validates incoming credentials (`username`/`password`) utilizing **Argon2id** salted digests.
3. **Issuance**: Backend returns distinct cryptographic token payload.
4. **Latching**: CLI stores token within the designated credential silo.

### HTTP Protocol Layer
All protected resources utilize standard RFC 6750 Bearer notation:
`Authorization: Bearer <TOKEN>`

## 3. Data Model Constraints

### Tokens Table
| Field | Restriction |
|-------|-------------|
| `id`  | Primary UUID |
| `user_id` | Foreign constraint |
| `token_hash` | **MANDATORY** (Never store plaintext) |
| `expires_at` | Dynamic TTL |

## 4. Scale Strategy: Pre-Signed Flow
To prevent registry API gateways from becoming upload chokepoints:

1. **Init**: `POST /api/v1/publish/init` -> Validates Token.
2. **Vector**: Returns secure **S3 Pre-Signed Upload URL**.
3. **Broadcast**: CLI pushes direct payload to MinIO bucket without tunneling through Go handler memory.
