#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/compagnon-ink.XXXXXX")
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer -g ${CXXFLAGS:-} \
    "$root/firmware/device/raster.cpp" "$root/firmware/device/ink.cpp" "$root/tests/ink_tests.cpp" \
    -o "$build_dir/ink-tests"
"$build_dir/ink-tests"
