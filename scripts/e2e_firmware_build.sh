#!/usr/bin/env bash

# End-to-end proof that `nav build` cross-compiles a real firmware ELF using the
# toolchain resolved from the package cache — not the one on $PATH.
#
# Flow:
#   1. Package the host's arm-none-eabi-* tools into a real toolchain artifact
#      (bin/ = symlinks to the installed tools), upload it to the dev MinIO, and
#      point the registry's arm-none-eabi-gcc@13.2.0 manifest at it (real sha).
#   2. `nav create` a project (clones NavHAL), `nav add arm-none-eabi-gcc@13.2.0`
#      to cache the toolchain, then `nav build`.
#   3. Assert: main.c was compiled by the CACHED arm-none-eabi-gcc, and the
#      output is an ARM ELF.
#
# Requires: the dev stack up (nav-db, nav-minio, registry-api on :8081 — e.g.
# `docker compose up -d`), a host arm-none-eabi-gcc, git/docker, and network for
# the one-time NavHAL clone. The cache + project live under a throwaway NAV_HOME
# and temp dir, so nothing on the host is disturbed. Re-running is idempotent;
# `python3 infra/seed_fixtures.py` resets the registry's toolchain to its stub.

set -euo pipefail

RED='\033[0;31m'; GREEN='\033[0;32m'; CYAN='\033[0;36m'; YELLOW='\033[1;33m'; BOLD='\033[1m'; NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_DIR="$(dirname "$SCRIPT_DIR")"

REGISTRY_URL="${NAV_REGISTRY_URL:-http://localhost:8081}"
MINIO_CONTAINER="nav-minio"
DB_CONTAINER="nav-db"
BUCKET="nav-packages"
OBJ="arm-none-eabi-gcc-13.2.0-linux_x86_64.tar.xz"

fail() { echo -e "${RED}${BOLD}E2E FAILED:${NC} $1" >&2; exit 1; }
step() { echo -e "${CYAN}${BOLD}==> $1${NC}"; }

# --- Preflight ---------------------------------------------------------------
command -v arm-none-eabi-gcc >/dev/null 2>&1 || fail "host arm-none-eabi-gcc not found (apt install gcc-arm-none-eabi)"
command -v docker >/dev/null 2>&1 || fail "docker not found"
command -v git >/dev/null 2>&1 || fail "git not found"
curl -fsS "$REGISTRY_URL/api/v1/index/arm-none-eabi-gcc" >/dev/null 2>&1 \
    || fail "registry not reachable at $REGISTRY_URL (try: docker compose up -d)"

NAV="$WORKSPACE_DIR/build/nav"
if [ ! -x "$NAV" ]; then
    step "Building nav"
    cmake -S "$WORKSPACE_DIR" -B "$WORKSPACE_DIR/build" -DCMAKE_BUILD_TYPE=Release -DNAV_BUILD_TESTS=OFF >/dev/null
    cmake --build "$WORKSPACE_DIR/build" --parallel "$(nproc)" --target nav >/dev/null
fi

TMP="$(mktemp -d /tmp/nav_e2e_fw.XXXX)"
export NAV_HOME="$(mktemp -d /tmp/nav_e2e_home.XXXX)"
export NAV_REGISTRY_URL="$REGISTRY_URL"
cleanup() { rm -rf "$TMP" "$NAV_HOME"; }
trap cleanup EXIT

# --- 1. Real toolchain artifact -> MinIO + registry --------------------------
step "Packaging host arm toolchain into a registry artifact"
PKG="$TMP/pkg"; mkdir -p "$PKG/bin"
for t in /usr/bin/arm-none-eabi-*; do ln -s "$t" "$PKG/bin/$(basename "$t")"; done
printf '{"name":"arm-none-eabi-gcc","version":"13.2.0","kind":"toolchain"}\n' > "$PKG/nav-package.json"
tar -C "$PKG" -cJf "$TMP/$OBJ" .   # symlinks preserved; resolve to host tools on extract
SHA="$(sha256sum "$TMP/$OBJ" | awk '{print $1}')"
echo -e "${YELLOW}artifact sha256=$SHA${NC}"

docker cp "$TMP/$OBJ" "$MINIO_CONTAINER:/tmp/$OBJ" >/dev/null
docker exec "$MINIO_CONTAINER" mc alias set local http://127.0.0.1:9000 navroot navpassword123 >/dev/null 2>&1 || true
docker exec "$MINIO_CONTAINER" mc mb --ignore-existing "local/$BUCKET" >/dev/null 2>&1 || true
docker exec "$MINIO_CONTAINER" mc anonymous set download "local/$BUCKET" >/dev/null 2>&1 || true
docker exec "$MINIO_CONTAINER" mc cp "/tmp/$OBJ" "local/$BUCKET/$OBJ" >/dev/null
docker exec -i "$DB_CONTAINER" psql -U nav_admin -d nav_registry -v ON_ERROR_STOP=1 >/dev/null <<SQL
UPDATE package_versions
SET manifest = jsonb_set(manifest, '{downloads,linux_x86_64,sha256}', '"$SHA"'),
    checksum_sha256 = '$SHA'
WHERE version = '13.2.0'
  AND package_id = (SELECT id FROM packages WHERE name = 'arm-none-eabi-gcc');
SQL

# --- 2. create -> add -> build ----------------------------------------------
step "nav create + add toolchain + build"
( cd "$TMP" && "$NAV" --quiet create fw >/dev/null )
PROJ="$TMP/fw"
( cd "$PROJ" && "$NAV" add arm-none-eabi-gcc@13.2.0 >/dev/null )
( cd "$PROJ" && "$NAV" build >/dev/null ) || fail "nav build returned non-zero"

# --- 3. Assertions -----------------------------------------------------------
step "Verifying the cached toolchain drove the compile"
CC_DB="$PROJ/build/compile_commands.json"
[ -f "$CC_DB" ] || fail "no compile_commands.json"
grep -q "$NAV_HOME/packages/arm-none-eabi-gcc" "$CC_DB" \
    || fail "compile did not use the cached toolchain"
ELF="$PROJ/build/fw"
[ -f "$ELF" ] || fail "firmware ELF not produced"
file "$ELF" | grep -q "ARM" || fail "output is not an ARM ELF: $(file "$ELF")"

echo -e "${GREEN}${BOLD}==> E2E PASSED${NC}"
echo -e "    compiler: ${GREEN}$(grep -o "$NAV_HOME/packages/arm-none-eabi-gcc[^\" ]*arm-none-eabi-gcc" "$CC_DB" | head -1)${NC}"
echo -e "    artifact: ${GREEN}$(file "$ELF" | cut -d, -f1-3)${NC}"
