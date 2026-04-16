#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: ./format-changed.sh [--check] [<base-ref>]

Format or validate changed C/C++ files under include/ and src/ against a base
reference. The default base reference is origin/master.

The repository pins the required clang-format major version in
.clang-format-version. Set CLANG_FORMAT_BIN to a matching executable when your
default clang-format is a different major version.

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
version_file=".clang-format-version"
required_version=""

if [[ -f "${version_file}" ]]; then
    required_version="$(tr -d '[:space:]' < "${version_file}")"
fi

resolve_clang_format_bin() {
    local candidate
    local candidates=()

    if [[ -n "${CLANG_FORMAT_BIN:-}" ]]; then
        echo "${CLANG_FORMAT_BIN}"
        return 0
    fi

    if [[ -n "${required_version}" ]]; then
        candidates+=("clang-format-${required_version}")
        candidates+=("/opt/homebrew/opt/llvm@${required_version}/bin/clang-format")
    fi

    candidates+=("clang-format-19")
    candidates+=("clang-format")

    for candidate in "${candidates[@]}"; do
        if [[ "${candidate}" == /* ]]; then
            if [[ -x "${candidate}" ]]; then
                echo "${candidate}"
                return 0
            fi
            continue
        fi

        if command -v "${candidate}" >/dev/null 2>&1; then
            command -v "${candidate}"
            return 0
        fi
    done

    return 1
}

extract_major_version() {
    local version_line

    version_line="$("$1" --version 2>/dev/null | head -n 1)"
    if [[ "${version_line}" =~ ([0-9]+)(\.[0-9]+)+ ]]; then
        echo "${BASH_REMATCH[1]}"
        return 0
    fi

    return 1
}

CLANG_FORMAT_BIN="$(resolve_clang_format_bin || true)"

if [[ -z "${CLANG_FORMAT_BIN}" ]]; then
    echo "clang-format is not installed" >&2
    exit 1
fi

detected_version="$(extract_major_version "${CLANG_FORMAT_BIN}" || true)"
if [[ -n "${required_version}" && "${CLANG_FORMAT_ALLOW_VERSION_MISMATCH:-0}" != "1" ]]; then
    if [[ -z "${detected_version}" ]]; then
        echo "Unable to determine the clang-format major version for '${CLANG_FORMAT_BIN}'" >&2
        exit 1
    fi

    if [[ "${detected_version}" != "${required_version}" ]]; then
        echo "clang-format major version ${detected_version} does not match the repository requirement ${required_version}" >&2
        echo "Set CLANG_FORMAT_BIN to a compatible executable or set CLANG_FORMAT_ALLOW_VERSION_MISMATCH=1 to override." >&2
        exit 1
    fi
fi

echo "Using ${CLANG_FORMAT_BIN} ($("${CLANG_FORMAT_BIN}" --version | head -n 1))"

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
