# Nav — Embedded Development Orchestrator 

**Nav** is a specialized, unified system orchestrator purpose-built for managing high-performance embedded workflows. Engineered natively in C++, it seamlessly connects source repositories, hardware drivers, and binary toolchains into one highly optimized, fault-tolerant command-line toolkit.

Designed for advanced Robotics, systems and low-level Firmware integration.

---

## Core Capabilities

| Feature | Engine Description |
| :--- | :--- |
| **Instant Scaffolding** | One-command initialization of standard hardware workspace trees. |
| **Cross-Compile Engine** | Automated injection of Cortex-M target architecture directives & hardware linker mapping. |
| **Auto-Provisioning** | Dynamic `$PATH` discovery & adaptive auto-installation of system packages. |
| **Native POSIX Monitor** | Direct kernel file descriptor telemetry streaming without heavy subprocess overlays. |
| **CI/CD Ready** | Native Debian `.deb` packaging framework for seamless workstation deployment. |

---

## Instant Quick Start

Kick off a pristine development cycle in seconds. Drop these commands sequentially to verify a full operational loop:

```bash
# 1. Initialize absolute workspace
nav create my_project

# 2. Enter working context
cd my_project

# 3. Cross-compile the firmware
nav build

# 4. Deploy to physical hardware
nav upload

# 5. Real-time telemetry
nav monitor --baud 115200
```

---

## Operational Commands

Nav exposes a clean, robust orchestration layer:

### `nav create <project_name>`
Creates a master hardware workspace, pulls dependency frameworks (like NavHAL), and bootstraps the root `.config` and dynamic makefiles.

### `nav build`
Fires up the `arm-none-eabi` cross-compiler stack. Automatically invalidates caching context whenever root configurations are mutated.

### `nav upload`
Connects direct to physical link interfaces (like `st-flash`) delivering atomic payload injection and hardware reset verification.

### `nav monitor`
Attaches direct to raw telemetry streams on active devices with auto-port negotiation logic.
```bash
nav monitor --baud 115200
```

### `nav check`
Performs continuous environmental audits, confirming validity of compiler toolchains, package managers, and flasher drivers.

### `nav update`
Heals broken dependency nodes and runs the adaptive package-acquisition routine to secure any missing local tools.

---

## Installation & Setup

Download the official compiled `.deb` package from the **GitHub Releases** tab, and install via native packet manager:

```bash
sudo dpkg -i nav-X.X.X-Linux.deb
```

*Alternatively, to build from source locally:*
```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

---

## License
nav is licensed under the **Apache License, Version 2.0** — see [`LICENSE`](LICENSE).
Copyright 2026 NAVRobotec Pvt Ltd. 
