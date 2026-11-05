#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$root/build/host"
${CXX:-clang++} -std=c++17 -Wall -Wextra -Werror -pedantic -I"$root/core" "$root/core/note_store.cpp" "$root/core/task_store.cpp" "$root/tests/task_store_tests.cpp" -o "$root/build/host/task_store_tests"
"$root/build/host/task_store_tests"
