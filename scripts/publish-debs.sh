#!/usr/bin/env bash
#
# Publish the OpenCATTUS Ubuntu APT repository.
#
# Copies built DEB packages into the repo target directory (typically an NFS
# mount of the storage pool) and regenerates flat APT repository metadata in
# place. Metadata is written to temp files and renamed into place so HTTPS
# clients do not observe partial writes.
#
# Usage:
#   scripts/publish-debs.sh --source-dir DIR --target-dir DIR [--dry-run]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SOURCE_DIR=""
TARGET_DIR=""
DRY_RUN=""
REPO_DIR="ubuntu2404"

usage() {
    sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --source-dir)
            SOURCE_DIR="$2"
            shift 2
            ;;
        --target-dir)
            TARGET_DIR="$2"
            shift 2
            ;;
        --dry-run)
            DRY_RUN="yes"
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

require_command() {
    local command_name="$1"
    command -v "${command_name}" >/dev/null || {
        echo "Missing required command: ${command_name}" >&2
        exit 1
    }
}

if [[ -z "${SOURCE_DIR}" ]]; then
    echo "--source-dir is required" >&2
    usage >&2
    exit 2
fi
if [[ -z "${TARGET_DIR}" ]]; then
    echo "--target-dir is required" >&2
    usage >&2
    exit 2
fi

require_command dpkg-scanpackages
require_command gzip

if [[ ! -d "${SOURCE_DIR}" ]]; then
    echo "Source directory does not exist: ${SOURCE_DIR}" >&2
    exit 1
fi
if [[ ! -d "${TARGET_DIR}" ]]; then
    echo "Target directory does not exist: ${TARGET_DIR}" >&2
    exit 1
fi

publish_ubuntu() {
    local destination="${TARGET_DIR}/${REPO_DIR}"
    local repo_file="${SCRIPT_DIR}/repo-files/${REPO_DIR}/versatushpc-opencattus.list"
    local debs=()

    while IFS= read -r -d '' deb; do
        debs+=("${deb}")
    done < <(find "${SOURCE_DIR}" -type f -name '*.deb' -print0 | sort -z)
    if [[ "${#debs[@]}" -eq 0 ]]; then
        echo "No DEB packages found under ${SOURCE_DIR}" >&2
        exit 1
    fi

    if [[ ! -f "${repo_file}" ]]; then
        echo "Missing repository file template: ${repo_file}" >&2
        exit 1
    fi

    if [[ -n "${DRY_RUN}" ]]; then
        echo "==> [dry-run] would publish ${#debs[@]} DEB(s) into ${destination}"
        for deb in "${debs[@]}"; do
            echo "    - $(basename "${deb}")"
        done
        echo "==> [dry-run] would copy ${repo_file} into ${destination}/"
        echo "==> [dry-run] would regenerate Packages and Packages.gz in ${destination}"
        return
    fi

    mkdir -p "${destination}"
    for deb in "${debs[@]}"; do
        cp -f "${deb}" "${destination}/"
    done
    cp -f "${repo_file}" "${destination}/"

    echo "==> Running dpkg-scanpackages for ${REPO_DIR}"
    (
        cd "${destination}"
        dpkg-scanpackages . /dev/null > Packages.new
        gzip -9c Packages.new > Packages.gz.new
        mv -f Packages.new Packages
        mv -f Packages.gz.new Packages.gz
    )
}

if [[ -n "${DRY_RUN}" ]]; then
    echo "*** DRY RUN - no files will be copied; metadata will not be regenerated ***"
fi

publish_ubuntu

echo ""
echo "==> Done. DEB count under ${TARGET_DIR}/${REPO_DIR}:"
count=$(find "${TARGET_DIR}/${REPO_DIR}" -maxdepth 1 -name '*.deb' 2>/dev/null | wc -l | tr -d ' ')
printf '    %s: %s\n' "${REPO_DIR}" "${count}"
echo ""
echo "    Users enable the repo with:"
echo "      curl -o /etc/apt/sources.list.d/versatushpc-opencattus.list https://repos.versatushpc.com.br/opencattus/${REPO_DIR}/versatushpc-opencattus.list"
