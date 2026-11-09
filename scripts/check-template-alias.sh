#!/bin/sh
# Runs the SYSTEM-include walk and the deps_opt loop of templates/cmakelists.txt.in
# against a fake dependency exposed through a name::name ALIAS, as navlink does.
# Usage: check-template-alias.sh [template]   (default: templates/cmakelists.txt.in)
set -eu
tpl=${1:-$(dirname "$0")/../templates/cmakelists.txt.in}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
{
  echo 'cmake_minimum_required(VERSION 3.20)'
  echo 'project(aliastest C)'
  echo 'add_library(hal INTERFACE)'
  echo 'add_library(fake STATIC fake.c)'
  echo 'target_include_directories(fake PUBLIC inc)'
  echo 'add_library(fake::fake ALIAS fake)'
  echo 'add_library(top STATIC fake.c)'
  echo 'target_link_libraries(top PUBLIC fake::fake)'
  echo 'set(NAV_DEP_TARGETS top fake::fake)'
  echo 'set(NAV_ALL_DEP_TARGETS top fake::fake)'
  echo 'set(NAV_APP_DEPS_OPT O2)'
  echo 'add_executable(${PROJECT_NAME} fake.c)'
  # the SYSTEM-include walk
  awk '/^set\(_nav_todo/{p=1} p{print} p&&/^endwhile\(\)/{exit}' "$tpl"
  # the deps_opt loop (nav_directory_targets is stubbed: no NavHAL here)
  echo 'function(nav_directory_targets d out)'
  echo '  set(${out} hal PARENT_SCOPE)'
  echo 'endfunction()'
  awk '/^if\(NAV_APP_DEPS_OPT\)/{p=1} p{print} p&&/^endif\(\)/{exit}' "$tpl"
} > "$work/CMakeLists.txt"
echo 'int f(void){return 0;}' > "$work/fake.c"
mkdir "$work/inc"
cmake -S "$work" -B "$work/b" >"$work/log" 2>&1 || { cat "$work/log"; echo FAIL; exit 1; }
echo PASS
