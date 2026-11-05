#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/compagnon-raster.XXXXXX")
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic \
    ${CXXFLAGS:-} "$root/firmware/device/raster.cpp" "$root/tests/raster_tests.cpp" \
    -o "$build_dir/raster-tests"
"$build_dir/raster-tests"
