# Nav — Embedded Package & Build Manager Architecture

## Vision

Nav is a unified embedded development orchestration system designed for robotics, embedded boards, microcontrollers, edge AI systems, and distributed hardware platforms.

The goal is to provide:

* Reproducible builds
* Cross-platform toolchain management
* Dependency resolution
* Board abstraction
* Firmware deployment
* Device communication
* Remote build/execution
* Simulation support
* Workspace isolation
* Distributed robotics workflows

Nav should eventually become:

* PlatformIO + Cargo + Docker + ROS tooling for robotics
* A unified environment for embedded and robotics engineering

---

# Core Philosophy

## 1. Hermetic Environments

Every project should:

* build identically on every machine
* pin exact tool versions
* avoid system-level dependencies
* isolate compilers/frameworks

No global SDK chaos.

---

## 2. Multi-Target First

Nav should support:

* AVR
* ARM Cortex-M
* RP2040
* ESP32
* Linux SBCs
* FPGA toolchains
* CUDA/AI systems
* Simulators
* Robotics middleware

From the beginning, architecture should assume multiple targets.

---

## 3. Workspace-Centric Design

A workspace may contain:

* firmware
* AI models
* simulation
* CAD metadata
* robotics pipelines
* deployment scripts
* cloud integrations

Like:

* Cargo workspaces
* ROS workspaces
* Bazel monorepos

---

# High-Level Architecture

```text
CLI
 ↓
Core Engine
 ├── Manifest Parser
 ├── Dependency Resolver
 ├── Package Manager
 ├── Toolchain Manager
 ├── Build Orchestrator
 ├── Device Manager
 ├── Registry Client
 ├── Cache Manager
 └── Plugin System
        ↓
Build Backend
 ├── CMake
 ├── Ninja
 ├── Cargo
 ├── Make
 ├── SCons
 └── Custom Backends
        ↓
Compiler/SDK/Tools
```

---

# Recommended Tech Stack

## Language

Primary recommendation:

* Rust

Why:

* excellent CLI ecosystem
* async support
* cross-platform
* package management experience from Cargo ecosystem
* memory safety
* fast
* easy static binaries
* good TOML support

Alternative:

* Go

Not recommended for core:

* Python (good for plugins though)
* Node.js

---

# Project Structure

```text
nav/
 ├── cli/
 ├── core/
 ├── resolver/
 ├── registry/
 ├── build/
 ├── toolchains/
 ├── devices/
 ├── plugins/
 ├── sdk/
 ├── daemon/
 └── docs/
```

---

# Manifest Design

Use TOML.

Example:

```toml
[project]
name = "flight-controller"
version = "0.1.0"

[target]
board = "rp2040"
framework = "freertos"

[toolchain]
compiler = "arm-gcc@13.2"

[dependencies]
imu-driver = "^1.2"
nav-hal = "^0.5"

[build]
backend = "cmake"
optimization = "O2"

[upload]
method = "picotool"
```

---

# Core Components

# 1. CLI Layer

Examples:

```bash
nav init
nav build
nav run
nav flash
nav monitor
nav test
nav deps
nav toolchain install arm-gcc
nav doctor
```

Responsibilities:

* UX
* command parsing
* invoking engine
* terminal UI

Use:

* clap (Rust)
* ratatui for TUI

---

# 2. Manifest Parser

Reads:

```text
nav.toml
```

Responsibilities:

* schema validation
* semantic validation
* defaults
* inheritance
* workspace support

Use:

* serde
* toml

---

# 3. Dependency Resolver

Most important subsystem.

Responsibilities:

* semver resolution
* transitive dependency graph
* conflict detection
* lockfile generation
* platform-specific selection

Inputs:

```text
nav.toml
nav.lock
registry metadata
```

Outputs:

```text
resolved graph
```

Recommended:

* SAT solver approach later
* simple DFS resolver initially

---

# 4. Package Registry

Registry stores:

* libraries
* frameworks
* board definitions
* toolchains
* plugins

Metadata example:

```json
{
  "name": "nav-hal",
  "version": "0.5.0",
  "targets": ["rp2040", "stm32"],
  "dependencies": {
    "cmsis": "^6.0"
  }
}
```

---

# Registry Architecture

## Phase 1

GitHub-based registry.

Like:

```text
index repository
```

Each package:

```text
metadata + tarball URL
```

---

## Phase 2

Dedicated registry service.

Components:

* API server
* CDN
* package storage
* authentication
* package signing

---

# 5. Toolchain Manager

Critical subsystem.

Responsibilities:

* download compilers
* install SDKs
* version pinning
* environment isolation
* checksum verification

Structure:

```text
~/.nav/toolchains/
~/.nav/packages/
~/.nav/cache/
```

Toolchain manifest:

```json
{
  "name": "arm-gcc",
  "version": "13.2",
  "url": "...",
  "sha256": "..."
}
```

---

# 6. Build Orchestrator

This converts resolved graph into actual build execution.

Responsibilities:

* generate build environments
* invoke backends
* parallel builds
* caching
* artifact tracking

Should support:

* Ninja
* CMake
* Make
* Cargo
* custom pipelines

---

# 7. Device Manager

One of Nav’s differentiators.

Responsibilities:

* serial detection
* flashing
* debugging
* remote device execution
* telemetry
* device inventory

Commands:

```bash
nav device list
nav flash
nav shell
nav logs
nav monitor
```

Future:

* network-connected robotics systems
* OTA firmware
* cluster management
* fleet deployment

---

# 8. Plugin System

Very important.

Plugins can add:

* new boards
* toolchains
* frameworks
* uploaders
* custom commands
* AI integrations
* simulators

Plugin examples:

```bash
nav plugin install stm32
nav plugin install ros2
nav plugin install gazebo
```

---

# Build Flow

```text
nav build
   ↓
Parse manifest
   ↓
Resolve dependencies
   ↓
Resolve toolchains
   ↓
Create build graph
   ↓
Generate isolated env
   ↓
Invoke backend
   ↓
Store artifacts
```

---

# Lockfile

Very important.

Example:

```toml
[[package]]
name = "nav-hal"
version = "0.5.0"
checksum = "..."
```

Purpose:

* reproducibility
* deterministic builds
* caching

---

# Caching Strategy

Cache:

* downloaded packages
* compiled artifacts
* dependency graphs
* toolchains

Potential future:

Distributed cache.

Like:

* Bazel remote cache
* CI artifact reuse

---

# Board Abstraction Layer

Board definitions should be declarative.

Example:

```json
{
  "id": "pico",
  "arch": "arm",
  "mcu": "rp2040",
  "frameworks": ["pico-sdk", "arduino"],
  "upload": {
    "tool": "picotool"
  }
}
```

---

# Suggested Internal Data Flow

```text
nav.toml
    ↓
Manifest AST
    ↓
Dependency Graph
    ↓
Resolved Build Graph
    ↓
Execution Plan
    ↓
Artifacts
```

---

# Suggested Development Roadmap

# Phase 1 — MVP

Goal:

Basic reproducible embedded builds.

Features:

* CLI
* nav.toml
* toolchain install
* package install
* dependency resolver
* local registry
* CMake backend
* RP2040 support
* AVR support

Commands:

```bash
nav init
nav build
nav flash
```

Timeline:

2–6 weeks.

---

# Phase 2 — Serious Tooling

Features:

* lockfiles
* caching
* multiple environments
* remote package registry
* semantic versioning
* plugin system
* serial monitor
* debugger support
* ESP32 support

---

# Phase 3 — Robotics Platform

Features:

* ROS integration
* simulator integration
* fleet deployment
* OTA updates
* distributed builds
* remote execution
* AI model deployment
* robotics pipelines

---

# Phase 4 — Full Ecosystem

Features:

* GUI
* cloud builds
* package marketplace
* collaborative workspaces
* CI/CD integration
* hardware inventory system

---

# Recommended First Prototype

Do NOT start with everything.

Start with:

## Supported Targets

Only:

* RP2040
* AVR

## Supported Build Backend

Only:

* CMake + Ninja

## Supported Manifest

Minimal:

```toml
[project]
name = "test"

[target]
board = "pico"
```

## Supported Commands

```bash
nav init
nav build
nav flash
```

## Internal Storage

```text
~/.nav/
 ├── toolchains/
 ├── packages/
 ├── cache/
 └── registry/
```

---

# Important Design Decisions

## Avoid PlatformIO Mistakes

### 1. Avoid Python Core

Python becomes difficult to scale for:

* performance
* dependency isolation
* concurrency
* packaging

---

### 2. Avoid Hidden Magic

Prefer explicit configuration.

Magic auto-detection becomes painful.

---

### 3. Avoid Monolithic Internals

Use modular architecture.

---

### 4. Treat Toolchains as Immutable

Never modify installed toolchains.

Use versioned directories.

---

### 5. Build Around DAGs

Everything should become graphs:

* dependency graph
* build graph
* execution graph

---

# Long-Term Vision for Nav

Nav can evolve into:

* robotics OS tooling
* embedded cloud infrastructure
* deployment system
* distributed robotics runtime
* autonomous system deployment framework
* hardware DevOps platform

Especially valuable for:

* robotics startups
* ecosystems
* edge AI systems
* industrial automation
* embedded AI inference

---

# Recommended Immediate Next Steps

1. Define nav.toml schema
2. Build CLI skeleton
3. Implement manifest parser
4. Implement toolchain installer
5. Add CMake backend
6. Add RP2040 build support
7. Add package metadata format
8. Add dependency graph resolver
9. Add flash/upload subsystem
10. Build local package cache

---

# Recommended Libraries (Rust)

| Purpose            | Library      |
| ------------------ | ------------ |
| CLI                | clap         |
| TOML               | toml         |
| Serialization      | serde        |
| Async              | tokio        |
| HTTP               | reqwest      |
| Graphs             | petgraph     |
| Semver             | semver       |
| Archive extraction | tar + flate2 |
| Checksums          | sha2         |
| Terminal UI        | ratatui      |
| Logging            | tracing      |

---

# Example Future UX

```bash
nav init fw
cd fw

nav add imu-driver
nav add freertos

nav build
nav flash
nav monitor
```

Future:

```bash
nav deploy fleet-alpha
nav ota push firmware.bin
nav sim run
nav ai deploy yolo11n
```

---

# Key Insight

The hardest parts are NOT:

* CLI
* downloads
* compilation

The hardest parts are:

* dependency resolution
* reproducibility
* cross-platform toolchains
* caching
* metadata design
* long-term architecture stability

Design these carefully from the beginning.
