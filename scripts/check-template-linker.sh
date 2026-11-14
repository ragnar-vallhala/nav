#!/bin/sh
# Runs the linker-script block of templates/cmakelists.txt.in under cmake -P for
# the four cases that matter: NavHAL exports NAVHAL_LINKER_SCRIPT (ARM, and AVR),
# no export on AVR (no board -T), and no export on ARM (a clear error).
# Usage: check-template-linker.sh [template]   (default: templates/cmakelists.txt.in)
set -eu
tpl=${1:-$(dirname "$0")/../templates/cmakelists.txt.in}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
# From the section header to just before the libc block.
awk '/^# 6\. Hardware-Bound Linker Directives\./{p=1} /^# \[build\] libc:/{p=0} p{print}' "$tpl" > "$work/block.cmake"
[ -s "$work/block.cmake" ] || { echo "FAIL: linker block not found in $tpl"; exit 1; }

# run <arch> <exported script or empty>; sets rc and out
run() {
  {
    echo "set(NAV_BOARD_ARCH \"$1\")"
    [ -n "$2" ] && echo "set(NAVHAL_LINKER_SCRIPT \"$2\")"
    echo "include(\"$work/block.cmake\")"
    echo 'message(STATUS "OPTS=[${NAV_APP_LINK_OPTIONS}]")'
  } > "$work/run.cmake"
  rc=0
  out=$(cmake -P "$work/run.cmake" 2>&1) || rc=$?
}
fail() { echo "FAIL: $1"; echo "$out"; exit 1; }

run cortex-m4 /x/board.ld
[ "$rc" = 0 ] && echo "$out" | grep -q -- '-T "/x/board.ld"' || fail "ARM with export should pass -T"

run avr /x/board.ld
[ "$rc" = 0 ] && echo "$out" | grep -q -- '-T "/x/board.ld"' || fail "an exported script wins on AVR too"

run avr ""
[ "$rc" = 0 ] || fail "AVR without export must configure"
echo "$out" | grep -q -- '-T' && fail "AVR without export must not pass -T"

run cortex-m4 ""
[ "$rc" != 0 ] || fail "ARM without export must fail"
# cmake wraps the message across lines; collapse whitespace before matching.
echo "$out" | tr -s ' \n' '  ' | grep -q "Update NavHAL to v0.3.11 or later" || fail "ARM error must say to update NavHAL"
echo PASS
