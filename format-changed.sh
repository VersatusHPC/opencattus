#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: ./format-changed.sh [--check] [<base-ref>]

Format or validate changed C/C++ files under include/ and src/ against a base
reference. The default base reference is origin/master.

Examples:
  ./format-changed.sh
  ./format-changed.sh --check
  ./format-changed.sh origin/master
EOF
}

mode="format"
if [[ "${1:-}" == "--check" ]]; then
    mode="check"
    shift
fi

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
    usage
    exit 0
fi

base_ref="${1:-origin/master}"

CLANG_FORMAT_BIN="${CLANG_FORMAT_BIN:-}"
if [[ -z "${CLANG_FORMAT_BIN}" ]]; then
    CLANG_FORMAT_BIN="$(command -v clang-format-19 || command -v clang-format || true)"
fi

if [[ -z "${CLANG_FORMAT_BIN}" ]]; then
    echo "clang-format is not installed" >&2
    exit 1
fi

if ! git rev-parse --verify "${base_ref}" >/dev/null 2>&1; then
    echo "Base ref '${base_ref}' was not found" >&2
    exit 1
fi

merge_base="$(git merge-base "${base_ref}" HEAD)"

mapfile -t files < <(
    git diff --name-only --diff-filter=ACMR "${merge_base}" HEAD -- include src \
        | grep -E '\.(h|hpp|c|cc|cpp)$' || true
)

if [[ "${#files[@]}" -eq 0 ]]; then
    echo "No changed C/C++ files under include/ or src/."
    exit 0
fi

if [[ "${mode}" == "check" ]]; then
    printf '%s\0' "${files[@]}" \
        | xargs -0 "${CLANG_FORMAT_BIN}" --dry-run --Werror
    echo "clang-format check passed for ${#files[@]} files."
    exit 0
fi

printf '%s\0' "${files[@]}" | xargs -0 "${CLANG_FORMAT_BIN}" -i
echo "Formatted ${#files[@]} files."
