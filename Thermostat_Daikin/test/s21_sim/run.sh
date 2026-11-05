#!/bin/bash
# Runs the real S21 driver against a simulated AC. Needs only a C++ compiler, not ESP-IDF
# or any hardware. Exits non-zero if a check fails.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
"${CXX:-c++}" -std=c++17 -w -I"$here/stubs" -I"$here/../../main" \
    "$here/s21_sim_test.cpp" "$here/../../main/s21_driver.cpp" -o "$out/s21_sim_test"
# Leave out the driver's own debug dump of each status frame.
"$out/s21_sim_test" | grep -v '^\[S21\]'
