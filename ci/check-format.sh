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

CLANG_FORMAT_BIN="$(command -v "clang-format-${REQUIRED_VERSION}" 2>/dev/null || command -v clang-format)"
export CLANG_FORMAT_BIN

actual_version=$("${CLANG_FORMAT_BIN}" --version | grep -oP '[0-9]+' | head -1)
if [ "${actual_version}" != "${REQUIRED_VERSION}" ]; then
    echo "clang-format version mismatch: got ${actual_version}, need ${REQUIRED_VERSION}" >&2
    exit 1
fi

failed=0
while IFS= read -r -d '' file; do
    if ! "${CLANG_FORMAT_BIN}" --dry-run --Werror "$file"; then
        failed=1
    fi
done < <(find include src test fuzz_test -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) -print0)

exit "$failed"
