#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$root/build/host"
${CXX:-clang++} -std=c++17 -Wall -Wextra -Werror -pedantic -I"$root/core" -I"$root/firmware/device" "$root/core/note_store.cpp" "$root/core/agenda_store.cpp" "$root/firmware/device/clock.cpp" "$root/tests/agenda_tests.cpp" -o "$root/build/host/agenda_tests"
"$root/build/host/agenda_tests"
