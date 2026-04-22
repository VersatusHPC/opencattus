#!/usr/bin/env bash
#
# Publish the OpenCATTUS Ubuntu APT repository.
#
# Stages built DEB packages, generates flat APT repository metadata, and
# mirrors only the Ubuntu subtree to the repository server.
#
# Usage:
#   scripts/publish-debs.sh [--source-dir DIR] [--staging-dir DIR] [--dry-run] [--skip-sync]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SOURCE_DIR="out/deb"
STAGING_DIR="${STAGING_DIR:-${TMPDIR:-/tmp}/opencattus-deb-repo}"
REMOTE_USER="${REMOTE_USER:-reposync}"
REMOTE_HOST="${REMOTE_HOST:-172.21.1.40}"
REMOTE_PATH="${REMOTE_PATH:-/mnt/pool1/repos/opencattus}"
SSH_KEY="${SSH_KEY:-}"
DRY_RUN=""
LFTP_DRY_RUN=""
SKIP_SYNC=""
REPO_DIR="ubuntu24"

usage() {
    sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --source-dir)
            SOURCE_DIR="$2"
            shift 2
            ;;
        --staging-dir)
            STAGING_DIR="$2"
            shift 2
            ;;
        --dry-run)
            DRY_RUN="yes"
            LFTP_DRY_RUN="--dry-run"
            shift
            ;;
        --skip-sync)
            SKIP_SYNC="yes"
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

resolve_ssh_key() {
    if [[ -n "${SSH_KEY}" ]]; then
        [[ -f "${SSH_KEY}" ]] && return 0
        echo "SSH key not found: ${SSH_KEY}" >&2
        echo "Set SSH_KEY=/path/to/reposync_key before publishing." >&2
        exit 1
    fi

    local candidate
    for candidate in \
        "${HOME}/.ssh/id_ed25519_openhpc" \
        "${HOME}/.ssh/id_ed25519" \
        "${HOME}/.ssh/id_rsa"; do
        if [[ -f "${candidate}" ]]; then
            SSH_KEY="${candidate}"
            return 0
        fi
    done

    echo "No SSH key found under ${HOME}/.ssh." >&2
    echo "Set SSH_KEY=/path/to/reposync_key before publishing." >&2
    exit 1
}

stage_ubuntu_debs() {
    local destination="${STAGING_DIR}/${REPO_DIR}"
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

    mkdir -p "${destination}"
    for deb in "${debs[@]}"; do
        cp -f "${deb}" "${destination}/"
    done
    cp "${repo_file}" "${destination}/"

    echo "==> Running dpkg-scanpackages for ${REPO_DIR}"
    (
        cd "${destination}"
        dpkg-scanpackages . /dev/null >Packages
        gzip -9c Packages >Packages.gz
    )
}

require_command dpkg-scanpackages
if [[ -z "${SKIP_SYNC}" ]]; then
    require_command lftp
    resolve_ssh_key
fi

if [[ -n "${DRY_RUN}" ]]; then
    echo "*** DRY RUN - repository metadata will be staged, no files will be transferred ***"
fi

echo "==> Preparing staging directory: ${STAGING_DIR}"
rm -rf "${STAGING_DIR}"
mkdir -p "${STAGING_DIR}"

stage_ubuntu_debs

if [[ -n "${SKIP_SYNC}" ]]; then
    echo "==> Skipping remote sync"
else
    echo "==> Syncing ${REPO_DIR} to ${REMOTE_USER}@${REMOTE_HOST}:${REMOTE_PATH}/${REPO_DIR} via SFTP"
    lftp -e "
set sftp:connect-program 'ssh -l ${REMOTE_USER} -i ${SSH_KEY} -o StrictHostKeyChecking=no -o BatchMode=yes';
open sftp://${REMOTE_HOST};
mkdir -p ${REMOTE_PATH}/${REPO_DIR};
mirror --reverse --delete --verbose ${LFTP_DRY_RUN} \
    ${STAGING_DIR}/${REPO_DIR}/ ${REMOTE_PATH}/${REPO_DIR}/;
bye;
"
fi

echo ""
echo "==> Done. DEB count:"
echo "    ${REPO_DIR}: $(find "${STAGING_DIR}/${REPO_DIR}" -name '*.deb' | wc -l | tr -d ' ')"
echo ""
echo "    Staged repository: ${STAGING_DIR}/${REPO_DIR}"
echo "    Remote repository: sftp://${REMOTE_USER}@${REMOTE_HOST}${REMOTE_PATH}/${REPO_DIR}"
echo ""
echo "    Users enable the repo with:"
echo "      curl -o /etc/apt/sources.list.d/versatushpc-opencattus.list https://repos.versatushpc.com.br/opencattus/${REPO_DIR}/versatushpc-opencattus.list"
