#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$root/build/host"
${CXX:-clang++} -std=c++17 -Wall -Wextra -Werror -pedantic ${EXTRA_CXXFLAGS:-} \
  -I"$root/firmware/device" \
  "$root/firmware/device/local_files.cpp" "$root/tests/local_files_tests.cpp" \
  -o "$root/build/host/local_files_tests"
"$root/build/host/local_files_tests"
