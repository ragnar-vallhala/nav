# Nav Registry

`test_database` is now the deployable Nav Registry application source. It contains the public website, registry API, Postgres metadata database, MinIO package/toolchain blob storage, and the registry-connected Nav CLI distribution.

The public production URL is intended to be:

```text
https://navdev.navrobotec.com
```

## Components

| Component | Responsibility |
| --- | --- |
| `frontend/` | Public registry UI, authentication, packages, namespaces, toolchains, and root administration |
| `backend/` | Auth, OTP, OAuth, package publish/download, toolchain delivery, CLI installer endpoints |
| `database/init.sql` | Registry metadata schema |
| `nav/nav.mjs` | Nav CLI distributed from the website |
| `vendor/NavHAL/` | First-party NavHAL source used to seed the base package |
| MinIO | Immutable package archives and managed toolchain archives |

## Public And Maintainer Behavior

Anyone can browse public packages and download published package artifacts through the CLI. An account is required to create namespaces, invite members, or publish package versions. Only the configured root administrator can publish official managed toolchain archives.

Published package versions are immutable. Changelog text is stored with each version. Toolchains and packages live as MinIO blobs with database metadata and SHA256 integrity data; internal bucket/object keys are not exposed in the public UI.

## Run Locally

The development compose file keeps local ports visible and may seed convenience fixture data.

```powershell
docker compose -f test_database/docker-compose.yml up --build
```

Open:

```text
Website:       http://localhost:13000
API:           http://localhost:14000
MinIO console: http://localhost:29001
```

For local-only fixture data, the development stack seeds:

```text
root email:    root@nav.local
root password: navrootpass123
```

Do not use the development compose file or these credentials for the public server.

## CLI From The Website

Once the API is running, the CLI installers are delivered by the registry:

```powershell
irm http://localhost:14000/downloads/nav/install.ps1 | iex
nav check
```

For Linux or macOS:

```bash
curl -fsSL http://localhost:14000/downloads/nav/install.sh | sh
nav check
```

On production these become:

```powershell
irm https://navdev.navrobotec.com/api/downloads/nav/install.ps1 | iex
```

```bash
curl -fsSL https://navdev.navrobotec.com/api/downloads/nav/install.sh | sh
```

The installer pins the CLI to the registry URL and adds `nav` to the user's shell PATH. Toolchains downloaded later by `nav setup` or `nav build` are kept in the user's Nav cache, not installed globally.

## Main CLI Flow

```powershell
nav signup "Your Name" you@example.com "strong-password"
nav login
nav logout
nav namespace create your-name
nav package create your-name blink-driver "Blink driver for STM32"
nav pack .\blink-driver --out .\blink-driver-0.1.0.navpkg
nav publish your-name blink-driver 0.1.0 .\blink-driver-0.1.0.navpkg --changelog .\blink-driver\CHANGELOG.md
nav search blink
```

For a firmware project:

```powershell
mkdir firmware-app
cd firmware-app
nav setup
nav add your-name/blink-driver@0.1.0
nav build
nav run
nav monitor --port COM8 --baud 9600
```

`nav setup` in an empty folder creates an ABI v1 starter app with `navmod.toml`. The starter pulls board support and NavHAL from the registry as packages instead of copying those internals into the project root. Existing ABI v1 modules/apps use `navmod.toml`; legacy projects can still use `nav.toml` or `nav.json`. Nav resolves package, build, and uploader toolchains from the manifest, so `uploader = "stlink"` is locked and installed by `nav setup`.

## Deploy To `navdev.navrobotec.com`

For a standalone server, production can use [docker-compose.production.yml](./docker-compose.production.yml) and [Caddyfile](./Caddyfile). Caddy terminates HTTPS and routes `/api/*` to the backend. Postgres and MinIO remain on the private Docker network.

The current Nav hosting server already has a shared Kubernetes `ingress-nginx` edge and cert-manager Let's Encrypt issuer serving other applications. On that server, use [deploy/kubernetes/nav-registry.yaml](./deploy/kubernetes/nav-registry.yaml) instead of starting another Caddy listener. This preserves the existing ports and registers `navdev.navrobotec.com` with the established multi-application HTTPS ingress.

1. Point the DNS record for `navdev.navrobotec.com` to the server IP.
2. Install Docker Engine with the Compose plugin on the server.
3. Copy this folder to the server.
4. Create the production environment file:

```bash
cd test_database
cp .env.production.example .env
chmod 600 .env
```

5. Fill `.env` with strong unique secrets, OAuth application credentials, SMTP credentials, and the initial Nav root administrator account:

```dotenv
NAV_ROOT_NAME=Nav Root Administrator
NAV_ROOT_EMAIL=your-admin@example.com
NAV_ROOT_PASSWORD=use-a-strong-unique-password
```
6. Configure OAuth callback URLs:

```text
https://navdev.navrobotec.com/api/auth/google/callback
https://navdev.navrobotec.com/api/auth/github/callback
https://navdev.navrobotec.com/api/auth/facebook/callback
```

7. Start the registry:

```bash
docker compose -f docker-compose.production.yml --env-file .env up -d --build
```

8. Verify:

```bash
curl https://navdev.navrobotec.com/api/health
curl -fsSL https://navdev.navrobotec.com/api/downloads/nav/install.sh | head
```

Production settings deliberately enforce:

- a strong `JWT_SECRET`
- explicit root administrator credentials when catalog seeding is enabled
- no sample login or fixture packages
- no public Postgres or MinIO ports
- authenticated internal storage metadata access

## Root Administration

The root account is a normal authenticated user with `system_role = root`. It owns the seeded `nav` namespace for first-party package publication, and it is the only role allowed to upload compiler and vendor toolchain archives through the `Admin` screen. Production credentials are not hardcoded; they are chosen in the server `.env` through `NAV_ROOT_EMAIL` and `NAV_ROOT_PASSWORD`.

## Server Access

To complete the real deployment, provide the server host, SSH username and an SSH key-based access method. Store credentials in the server `.env`; do not commit passwords, SMTP app passwords, OAuth secrets, or MinIO keys to Git.
