#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

mapfile -t files < <(git ls-files '*.h' '*.hpp' '*.cpp' '*.cc' '*.cxx')

if [[ ${#files[@]} -eq 0 ]]; then
  echo "no C++ files to check"
  exit 0
fi

clang-format --dry-run --Werror "${files[@]}"
echo "clang-format: OK (${#files[@]} files)"