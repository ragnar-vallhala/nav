#!/bin/bash

# Exit immediately on errors
set -e

# Terminal Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
YELLOW='\033[1;33m'
BOLD='\033[1m'
NC='\033[0m' # No Color

echo -e "${CYAN}${BOLD}==> Starting Debian Packaging Sequence...${NC}"

# 1. Resolve workspace path
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$WORKSPACE_DIR/build"

cd "$WORKSPACE_DIR"

# 2. Prepare Build Environment
if [ ! -d "$BUILD_DIR" ]; then
    echo -e "Creating fresh build cache..."
    mkdir -p "$BUILD_DIR"
fi

# 3. Configuration
echo -e "${YELLOW}Configuring dynamic build system...${NC}"
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release

# 4. Compile binary
echo -e "${YELLOW}Compiling native components...${NC}"
cmake --build "$BUILD_DIR" --parallel $(nproc)

# 5. Package construction
echo -e "${YELLOW}Invoking CPack backend generator (DEB)...${NC}"
cd "$BUILD_DIR"
cpack -G DEB

# 6. Report
DEB_FILE=$(ls "$BUILD_DIR"/*.deb 2>/dev/null | tail -n 1)
if [ -n "$DEB_FILE" ]; then
    echo -e "\n${GREEN}${BOLD}✔ DEB PACKAGE SUCCESSFULLY SYNCED!${NC}"
    echo -e "Artifact location: ${CYAN}$(basename "$DEB_FILE")${NC}"
    echo -e "Full path: ${CYAN}$DEB_FILE${NC}\n"
else
    echo -e "${RED}Error: Package generation failed or output vanished.${NC}"
    exit 1
fi
