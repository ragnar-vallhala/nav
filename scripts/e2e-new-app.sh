#!/usr/bin/env bash
# End-to-end check: a freshly created nav app builds against the NavHAL that
# nav pins.
#
#   scripts/e2e-new-app.sh [path/to/nav]        (default: build/nav)
#
# Builds nothing itself. Runs `nav create` + `nav build` in a throwaway
# NAV_HOME and temp directory, then checks the firmware ELF is an ARM binary.
# Needs on PATH: git, cmake, python3 (+venv/pip), arm-none-eabi-gcc, file.
# Needs network: NavHAL is cloned from GitHub and kconfiglib is pip-installed
# into the throwaway NAV_HOME.
#
# Environment:
#   NAV_E2E_NAVHAL_REF   if set, rewrite [navhal] ref in the new app's nav.toml
#                        (and drop nav.lock) before building. Used to prove the
#                        check fails when the pin is bad.
set -euo pipefail

NAV=${1:-build/nav}
[ -x "$NAV" ] || { echo "e2e: nav binary not found or not executable: $NAV" >&2; exit 2; }
NAV=$(cd "$(dirname "$NAV")" && pwd)/$(basename "$NAV")

for tool in git cmake python3 arm-none-eabi-gcc file; do
    command -v "$tool" >/dev/null || { echo "e2e: '$tool' not found on PATH" >&2; exit 2; }
done

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export NAV_HOME="$work/nav-home"
mkdir -p "$NAV_HOME"
export GIT_TERMINAL_PROMPT=0

APP=e2e-app
cd "$work"

echo "== nav create $APP"
# git clone progress is very noisy; keep the full log for failures only.
if ! "$NAV" create "$APP" >"$work/create.log" 2>&1; then
    tail -n 40 "$work/create.log" >&2
    echo "e2e FAIL: nav create" >&2
    exit 1
fi
grep -E "NavHAL ready|Complete" "$work/create.log" | sed 's/(cache:.*//' || true

cd "$APP"

if [ -n "${NAV_E2E_NAVHAL_REF:-}" ]; then
    echo "== overriding NavHAL ref with '$NAV_E2E_NAVHAL_REF' (negative test)"
    # Only inside the [navhal] table (up to the next table header).
    sed -i "/^\[navhal\]/,/^\[/ s|^ref = .*|ref = \"$NAV_E2E_NAVHAL_REF\"|" nav.toml
    grep -q "^ref = \"$NAV_E2E_NAVHAL_REF\"" nav.toml \
        || { echo "e2e: could not set the [navhal] ref in nav.toml" >&2; exit 2; }
    rm -f nav.lock
fi

pin_ref=$(sed -n 's/^ref = "\(.*\)"/\1/p' nav.toml | tail -n1)
pin_commit=$(python3 -I -c 'import json,sys; print(json.load(open("nav.lock")).get("navhal",{}).get("commit",""))' 2>/dev/null || true)
echo "== NavHAL pinned by nav: ref=${pin_ref:-?} commit=${pin_commit:-<no nav.lock>}"

echo "== nav build"
if ! "$NAV" build >"$work/build.log" 2>&1; then
    tail -n 40 "$work/build.log" >&2
    echo "e2e FAIL: nav build exited non-zero" >&2
    exit 1
fi
tail -n 15 "$work/build.log"

# nav names the firmware after the project, with no extension.
elf=build/$APP
[ -f "$elf" ] || elf=$(find build -maxdepth 2 -name '*.elf' -type f 2>/dev/null | head -n1)
[ -n "$elf" ] && [ -f "$elf" ] || { echo "e2e FAIL: no firmware ELF produced under build/" >&2; exit 1; }
desc=$(file -b "$elf")
echo "== firmware: $elf"
echo "   $desc"
case "$desc" in
    *ELF*ARM*) ;;
    *) echo "e2e FAIL: $elf is not an ARM ELF" >&2; exit 1 ;;
esac

echo "== size"
arm-none-eabi-size "$elf"

echo "e2e PASS: new app built against NavHAL ${pin_ref:-?} (${pin_commit:0:12})"
