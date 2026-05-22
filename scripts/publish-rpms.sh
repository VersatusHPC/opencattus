#!/usr/bin/env bash
#
# Publish the OpenCATTUS RPM repository.
#
# Copies CPack-built RPMs into the repo target directory (typically an NFS
# mount of the storage pool) and regenerates dnf repository metadata in place
# with createrepo_c. createrepo_c writes new metadata under .repodata/ and
# renames it to repodata/ atomically, so HTTPS clients never observe a
# partial state.
#
# Usage:
#   scripts/publish-rpms.sh --source-dir DIR --target-dir DIR [--dry-run]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SOURCE_DIR=""
TARGET_DIR=""
DRY_RUN=""

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

require_command createrepo_c

if [[ ! -d "${SOURCE_DIR}" ]]; then
    echo "Source directory does not exist: ${SOURCE_DIR}" >&2
    exit 1
fi
if [[ ! -d "${TARGET_DIR}" ]]; then
    echo "Target directory does not exist: ${TARGET_DIR}" >&2
    exit 1
fi

publish_el() {
    local el_number="$1"
    local repo_dir="el${el_number}"
    local rpm_glob="*.el${el_number}.*.rpm"
    local destination="${TARGET_DIR}/${repo_dir}/x86_64"
    local repo_file="${SCRIPT_DIR}/repo-files/${repo_dir}/versatushpc-opencattus.repo"
    local rpms=()

    while IFS= read -r -d '' rpm; do
        rpms+=("${rpm}")
    done < <(find "${SOURCE_DIR}" -type f -name "${rpm_glob}" -print0 | sort -z)
    if [[ "${#rpms[@]}" -eq 0 ]]; then
        echo "No EL${el_number} RPMs found under ${SOURCE_DIR}" >&2
        exit 1
    fi

    if [[ ! -f "${repo_file}" ]]; then
        echo "Missing repository file template: ${repo_file}" >&2
        exit 1
    fi

    if [[ -n "${DRY_RUN}" ]]; then
        echo "==> [dry-run] would publish ${#rpms[@]} RPM(s) into ${destination}"
        for rpm in "${rpms[@]}"; do
            echo "    - $(basename "${rpm}")"
        done
        echo "==> [dry-run] would copy ${repo_file} into ${TARGET_DIR}/${repo_dir}/"
        echo "==> [dry-run] would run: createrepo_c --update ${TARGET_DIR}/${repo_dir}"
        return
    fi

    mkdir -p "${destination}"
    for rpm in "${rpms[@]}"; do
        cp -f "${rpm}" "${destination}/"
    done
    cp -f "${repo_file}" "${TARGET_DIR}/${repo_dir}/"

    echo "==> Running createrepo_c for ${repo_dir}"
    createrepo_c --update "${TARGET_DIR}/${repo_dir}"
}

if [[ -n "${DRY_RUN}" ]]; then
    echo "*** DRY RUN - no files will be copied; metadata will not be regenerated ***"
fi

publish_el 8
publish_el 9
publish_el 10

echo ""
echo "==> Done. RPM counts under ${TARGET_DIR}:"
for el in el8 el9 el10; do
    count=$(find "${TARGET_DIR}/${el}/x86_64" -maxdepth 1 -name '*.rpm' 2>/dev/null | wc -l | tr -d ' ')
    printf '    %-5s x86_64: %s\n' "${el}" "${count}"
done
echo ""
echo "    Users enable the repo with:"
echo "      EL8:  curl -o /etc/yum.repos.d/versatushpc-opencattus.repo https://repos.versatushpc.com.br/opencattus/el8/versatushpc-opencattus.repo"
echo "      EL9:  curl -o /etc/yum.repos.d/versatushpc-opencattus.repo https://repos.versatushpc.com.br/opencattus/el9/versatushpc-opencattus.repo"
echo "      EL10: curl -o /etc/yum.repos.d/versatushpc-opencattus.repo https://repos.versatushpc.com.br/opencattus/el10/versatushpc-opencattus.repo"
