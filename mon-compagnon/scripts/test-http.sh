#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$root/build/host"
${CXX:-clang++} -std=c++17 -Wall -Wextra -Werror -pedantic -I"$root/core" "$root/core/compagnon.cpp" "$root/core/http_access.cpp" "$root/tests/http_access_tests.cpp" -o "$root/build/host/http_access_tests"
"$root/build/host/http_access_tests"
