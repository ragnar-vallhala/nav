#!/bin/bash

# Terminal Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m' # No Color

if [ -z "$1" ]; then
    echo -e "${RED}Error: Missing version number.${NC}"
    echo -e "Usage: ./release.sh <version>"
    echo -e "Example: ${CYAN}./release.sh v1.0.1${NC}"
    exit 1
fi

VERSION=$1

# Basic sanity prefix ensure
if [[ ! $VERSION =~ ^v.* ]]; then
    VERSION="v$VERSION"
fi

echo -e "${CYAN}${BOLD}=> Preparing Final Deployment Stage...${NC}"

# 1. Local Tagging
echo -e "Creating local timestamp anchor for [${GREEN}${VERSION}${NC}]..."
git tag $VERSION
if [ $? -ne 0 ]; then
    echo -e "${RED}Failed to create local tag. Does it already exist?${NC}"
    exit 1
fi

# 2. Remote Uplink
echo -e "Igniting uplink... launching release into GitHub CI/CD orbit..."
git push origin $VERSION
if [ $? -ne 0 ]; then
    echo -e "${RED}Push sequence failure! Check internet connectivity or credentials.${NC}"
    exit 1
fi

echo -e "\n${GREEN}${BOLD}✔ IGNITION SUCCESSFUL!${NC}"
echo -e "Monitor the automated cloud build at:"
echo -e "${CYAN}https://github.com/ragnar-vallhala/nav/actions${NC}\n"
