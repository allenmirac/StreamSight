#!/usr/bin/env bash
set -euo pipefail

# Run from the repository root so the exclude paths below resolve correctly.
if ! cd "$(git rev-parse --show-toplevel 2>/dev/null)"; then
  echo "Not inside a git repository."
  exit 1
fi

if ! command -v clang-format >/dev/null 2>&1; then
  echo "clang-format not found in PATH."
  echo "   Install it, e.g.:"
  echo "     Ubuntu:  sudo apt-get install -y clang-format"
  echo "     macOS:   brew install clang-format"
  echo "     pip:     pip install clang-format"
  exit 1
fi

CLANG_FORMAT="$(command -v clang-format)"
echo "Using: $CLANG_FORMAT ($($CLANG_FORMAT --version))"

# Paths that must never be reformatted: build trees and vendored third-party code.
EXCLUDES=(
  './.git'
  './build*'
  './src/third_party'
)

find_args=(. -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.cc' \
  -o -name '*.cxx' -o -name '*.h' -o -name '*.hh' \))
for exclude in "${EXCLUDES[@]}"; do
  find_args+=(-not -path "$exclude/*")
done

FILES=$(find "${find_args[@]}")

if [ -z "$FILES" ]; then
  echo "No C/C++ files found."
  exit 0
fi

echo "$FILES" | xargs "$CLANG_FORMAT" -i

echo "Formatting done (Google style)."