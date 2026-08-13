#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

files=$(git ls-files '*.h' '*.hpp' '*.cpp' '*.cc' '*.cxx')

if [[ -z "$files" ]]; then
    echo "No C/C++ sources tracked; nothing to format."
    exit 0
fi

echo "Formatting $(echo "$files" | wc -l | tr -d ' ') file(s) with clang-format..."
clang-format -i $files
echo "Done."