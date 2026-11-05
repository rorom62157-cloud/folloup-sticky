#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$root/build/host"
${CXX:-clang++} -std=c++17 -Wall -Wextra -Werror -pedantic ${SANITIZER_FLAGS:-} -I"$root/core" \
 "$root/core/compagnon.cpp" "$root/core/http_access.cpp" "$root/core/note_store.cpp" \
 "$root/core/note_service.cpp" "$root/core/note_http.cpp" "$root/core/note_json.cpp" \
 "$root/core/postit_store.cpp" "$root/core/postit_http.cpp" "$root/tests/postit_http_tests.cpp" \
 -o "$root/build/host/postit_http_tests"
"$root/build/host/postit_http_tests"
