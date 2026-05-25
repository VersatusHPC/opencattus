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

exec ./format-changed.sh --check
