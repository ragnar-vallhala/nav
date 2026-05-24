# Nav CLI Distribution Source

This directory contains the CLI artifact served by Nav Registry. During local development it connects to `http://localhost:14000`; the website installer sets `NAV_REGISTRY_URL` to the deployed API URL.

Run the source CLI locally:

```powershell
.\test_database\nav\nav.ps1 help
```

Override the registry explicitly:

```powershell
$env:NAV_REGISTRY_URL="https://nav.navrobotec.online/api"
.\test_database\nav\nav.ps1 check
```

## Publisher Flow

```powershell
nav signup "Your Name" you@example.com "strong-password"
nav login you@example.com "strong-password"
nav namespace create your-name
nav package create your-name blink-led "STM32 blink LED package"
nav pack .\blink-led --out .\blink-led-0.1.0.navpkg
nav publish your-name blink-led 0.1.0 .\blink-led-0.1.0.navpkg --changelog .\blink-led\CHANGELOG.md
```

## Consumer Flow

```powershell
nav search blink
nav setup
nav add your-name/blink-led@0.1.0
nav build
nav run
```

## Toolchain Publisher Flow

Managed compiler or uploader archives must be ingested through the authenticated registry command with truthful provenance and signature metadata:

```powershell
nav toolchain publish .\toolchain-metadata.json .\toolchain-archive.zip
```

Do not write directly to MinIO or Postgres for production catalog entries.
