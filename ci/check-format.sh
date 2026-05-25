#!/usr/bin/env bash
set -euo pipefail

REQUIRED_VERSION="$(tr -d '[:space:]' < .clang-format-version)"

if ! command -v "clang-format-${REQUIRED_VERSION}" >/dev/null 2>&1; then
    if command -v pip3 >/dev/null 2>&1; then
        pip3 install "clang-format==${REQUIRED_VERSION}.*"
    elif command -v pip >/dev/null 2>&1; then
        pip install "clang-format==${REQUIRED_VERSION}.*"
    else
        echo "clang-format-${REQUIRED_VERSION} not found and pip not available" >&2
        exit 1
    fi
fi

if git rev-parse --verify origin/master >/dev/null 2>&1; then
    exec ./format-changed.sh --check origin/master
fi

echo "origin/master not available; checking all source files"
CLANG_FORMAT_BIN="$(command -v "clang-format-${REQUIRED_VERSION}" 2>/dev/null || command -v clang-format)"
export CLANG_FORMAT_BIN

failed=0
while IFS= read -r -d '' file; do
    if ! "${CLANG_FORMAT_BIN}" --dry-run --Werror "$file" 2>/dev/null; then
        echo "Format error: $file" >&2
        failed=1
    fi
done < <(find include src -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) -print0)

exit "$failed"
