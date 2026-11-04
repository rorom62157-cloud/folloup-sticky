#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$root/build/host"
${CXX:-clang++} -std=c++17 -Wall -Wextra -Werror -pedantic -I"$root/core" "$root/core/compagnon.cpp" "$root/tests/core_tests.cpp" -o "$root/build/host/core_tests"
"$root/build/host/core_tests"
