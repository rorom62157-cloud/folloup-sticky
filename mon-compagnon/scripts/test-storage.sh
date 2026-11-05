#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$root/build/host"
${CXX:-clang++} -std=c++17 -Wall -Wextra -Werror -pedantic -I"$root/core" "$root/core/wav_writer.cpp" "$root/tests/wav_writer_tests.cpp" -o "$root/build/host/wav_writer_tests"
"$root/build/host/wav_writer_tests"
