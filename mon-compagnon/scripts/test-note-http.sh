#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$root/build/host"
${CXX:-clang++} -std=c++17 -Wall -Wextra -Werror -pedantic -I"$root/core" -I"$root/firmware/storage" \
 "$root/core/compagnon.cpp" "$root/core/http_access.cpp" "$root/core/note_store.cpp" \
 "$root/core/note_service.cpp" "$root/core/note_json.cpp" "$root/core/note_http.cpp" \
 "$root/firmware/storage/posix_note_files.cpp" "$root/tests/note_http_tests.cpp" \
 -o "$root/build/host/note_http_tests"
"$root/build/host/note_http_tests"
