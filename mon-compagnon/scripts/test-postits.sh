#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$root/build/host"
${CXX:-clang++} -std=c++17 -Wall -Wextra -Werror -pedantic ${SANITIZER_FLAGS:-} -I"$root/core" "$root/core/note_store.cpp" "$root/core/postit_store.cpp" "$root/tests/postit_store_tests.cpp" -o "$root/build/host/postit_store_tests"
"$root/build/host/postit_store_tests"
