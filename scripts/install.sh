#!/bin/bash

# Build Nav from source and install it (the `nav` binary plus the bundled board
# catalog) into a prefix. Mirrors the CMake install rules:
#   <prefix>/bin/nav
#   <prefix>/share/nav/boards/
# nav resolves its board catalog relative to the binary (<bindir>/../share/nav/
# boards), so installing both under one prefix keeps `nav board list` working.
#
# Usage:
#   ./scripts/install.sh [--prefix DIR] [--build-type TYPE]
#
#   --prefix DIR       install root (default: $PREFIX or /usr/local)
#   --build-type TYPE  CMake build type (default: Release)
#
# Examples:
#   ./scripts/install.sh                      # -> /usr/local (uses sudo if needed)
#   ./scripts/install.sh --prefix ~/.local    # per-user, no sudo
#   PREFIX=/opt/nav ./scripts/install.sh

set -euo pipefail

# Terminal Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
YELLOW='\033[1;33m'
BOLD='\033[1m'
NC='\033[0m' # No Color

# 1. Resolve workspace path
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$WORKSPACE_DIR/build"

# 2. Parse arguments
PREFIX="${PREFIX:-/usr/local}"
BUILD_TYPE="Release"
while [ $# -gt 0 ]; do
    case "$1" in
        --prefix)     PREFIX="$2"; shift 2 ;;
        --prefix=*)   PREFIX="${1#*=}"; shift ;;
        --build-type) BUILD_TYPE="$2"; shift 2 ;;
        --build-type=*) BUILD_TYPE="${1#*=}"; shift ;;
        -h|--help)
            grep '^#' "$0" | sed 's/^# \{0,1\}//; 1d'
            exit 0 ;;
        *)
            echo -e "${RED}Unknown argument: $1${NC}" >&2
            exit 1 ;;
    esac
done

# 3. Preflight: required tooling
for tool in cmake; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo -e "${RED}Missing required tool: '$tool'. Install it and re-run.${NC}" >&2
        exit 1
    fi
done

echo -e "${CYAN}${BOLD}==> Installing Nav (${BUILD_TYPE}) into ${PREFIX}${NC}"
cd "$WORKSPACE_DIR"

# 4. Configure + compile. Tests are forced OFF: the test harness pulls in
#    GoogleTest via FetchContent, whose own install rules would otherwise leak
#    gtest/gmock headers + libs into the prefix alongside nav.
echo -e "${YELLOW}Configuring build system...${NC}"
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DNAV_BUILD_TESTS=OFF

echo -e "${YELLOW}Compiling native components...${NC}"
cmake --build "$BUILD_DIR" --parallel "$(nproc)"

# 5. Decide whether the install step needs elevation. Walk up to the nearest
#    existing ancestor of PREFIX and test writability there.
need_sudo() {
    [ "$(id -u)" -eq 0 ] && return 1
    local dir="$PREFIX"
    while [ ! -e "$dir" ]; do dir="$(dirname "$dir")"; done
    [ -w "$dir" ] && return 1 || return 0
}

SUDO=""
if need_sudo; then
    if command -v sudo >/dev/null 2>&1; then
        SUDO="sudo"
        echo -e "${YELLOW}${PREFIX} is not writable — using sudo for the install step.${NC}"
    else
        echo -e "${RED}${PREFIX} is not writable and 'sudo' is unavailable.${NC}" >&2
        echo -e "${RED}Re-run with --prefix \$HOME/.local or as root.${NC}" >&2
        exit 1
    fi
fi

# 6. Install
echo -e "${YELLOW}Installing artifacts...${NC}"
$SUDO cmake --install "$BUILD_DIR" --prefix "$PREFIX"

# 7. Report + PATH guidance
NAV_BIN="$PREFIX/bin/nav"
echo -e "${GREEN}${BOLD}==> Nav installed: ${NAV_BIN}${NC}"
if command -v nav >/dev/null 2>&1 && [ "$(command -v nav)" = "$NAV_BIN" ]; then
    echo -e "${GREEN}On PATH and ready:${NC} $("$NAV_BIN" --version 2>/dev/null || echo 'nav')"
else
    case ":$PATH:" in
        *":$PREFIX/bin:"*) : ;;
        *)
            echo -e "${YELLOW}${PREFIX}/bin is not on your PATH. Add it:${NC}"
            echo -e "    ${CYAN}export PATH=\"$PREFIX/bin:\$PATH\"${NC}" ;;
    esac
fi
