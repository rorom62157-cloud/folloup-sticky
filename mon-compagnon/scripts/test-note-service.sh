#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$root/build/host"
${CXX:-clang++} -std=c++17 -Wall -Wextra -Werror -pedantic -I"$root/core" "$root/core/note_store.cpp" "$root/core/note_service.cpp" "$root/tests/note_service_tests.cpp" -o "$root/build/host/note_service_tests"
"$root/build/host/note_service_tests"
