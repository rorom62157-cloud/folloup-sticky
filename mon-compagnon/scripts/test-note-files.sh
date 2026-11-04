#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$root/build/host"
${CXX:-clang++} -std=c++17 -Wall -Wextra -Werror -pedantic \
  -I"$root/core" -I"$root/firmware/storage" \
  "$root/core/note_store.cpp" "$root/firmware/storage/posix_note_files.cpp" \
  "$root/tests/note_files_tests.cpp" -o "$root/build/host/note_files_tests"
"$root/build/host/note_files_tests"
