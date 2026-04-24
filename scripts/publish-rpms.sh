#!/usr/bin/env bash
#
# Publish the OpenCATTUS RPM repository.
#
# Stages CPack-built RPMs by Enterprise Linux generation, generates dnf
# repository metadata, and mirrors the result to the repository server.
#
# Usage:
#   scripts/publish-rpms.sh [--source-dir DIR] [--staging-dir DIR] [--dry-run] [--skip-sync]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SOURCE_DIR="out/rpm"
STAGING_DIR="${STAGING_DIR:-${TMPDIR:-/tmp}/opencattus-rpm-repo}"
REMOTE_USER="${REMOTE_USER:-reposync}"
REMOTE_HOST="${REMOTE_HOST:-172.21.1.40}"
REMOTE_PATH="${REMOTE_PATH:-/mnt/pool1/repos/opencattus}"
SSH_KEY="${SSH_KEY:-}"
DRY_RUN=""
LFTP_DRY_RUN=""
SKIP_SYNC=""

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

seed_remote_repo_dir() {
    local repo_dir="$1"

    mkdir -p "${STAGING_DIR}/${repo_dir}"

    echo "==> Fetching current remote contents for ${repo_dir}"
    lftp -e "
set cmd:fail-exit yes;
set sftp:connect-program 'ssh -l ${REMOTE_USER} -i ${SSH_KEY} -o StrictHostKeyChecking=no -o BatchMode=yes';
open sftp://${REMOTE_HOST};
mkdir -p ${REMOTE_PATH}/${repo_dir};
mirror --verbose ${REMOTE_PATH}/${repo_dir}/ ${STAGING_DIR}/${repo_dir}/;
bye;
"
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

stage_el_rpms() {
    local el_number="$1"
    local repo_dir="el${el_number}"
    local rpm_glob="*.el${el_number}.*.rpm"
    local destination="${STAGING_DIR}/${repo_dir}/x86_64"
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

    mkdir -p "${destination}"
    for rpm in "${rpms[@]}"; do
        cp -f "${rpm}" "${destination}/"
    done
    cp "${repo_file}" "${STAGING_DIR}/${repo_dir}/"

    rm -rf "${STAGING_DIR}/${repo_dir}/repodata"
    echo "==> Running createrepo_c for ${repo_dir}"
    createrepo_c --update "${STAGING_DIR}/${repo_dir}"
}

require_command createrepo_c
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

if [[ -z "${SKIP_SYNC}" ]]; then
    seed_remote_repo_dir el8
    seed_remote_repo_dir el9
    seed_remote_repo_dir el10
fi

stage_el_rpms 8
stage_el_rpms 9
stage_el_rpms 10

if [[ -n "${SKIP_SYNC}" ]]; then
    echo "==> Skipping remote sync"
else
    echo "==> Syncing to ${REMOTE_USER}@${REMOTE_HOST}:${REMOTE_PATH} via SFTP"
    for repo_dir in el8 el9 el10; do
        lftp -e "
set sftp:connect-program 'ssh -l ${REMOTE_USER} -i ${SSH_KEY} -o StrictHostKeyChecking=no -o BatchMode=yes';
open sftp://${REMOTE_HOST};
mkdir -p ${REMOTE_PATH}/${repo_dir};
mirror --reverse --delete --verbose ${LFTP_DRY_RUN} \
    ${STAGING_DIR}/${repo_dir}/ ${REMOTE_PATH}/${repo_dir}/;
bye;
"
    done
fi

echo ""
echo "==> Done. RPM counts:"
echo "    el8/x86_64:  $(find "${STAGING_DIR}/el8/x86_64" -name '*.rpm' | wc -l | tr -d ' ')"
echo "    el9/x86_64:  $(find "${STAGING_DIR}/el9/x86_64" -name '*.rpm' | wc -l | tr -d ' ')"
echo "    el10/x86_64: $(find "${STAGING_DIR}/el10/x86_64" -name '*.rpm' | wc -l | tr -d ' ')"
echo ""
echo "    Staged repository: ${STAGING_DIR}"
echo "    Remote repository: sftp://${REMOTE_USER}@${REMOTE_HOST}${REMOTE_PATH}"
echo ""
echo "    Users enable the repo with:"
echo "      EL8:  curl -o /etc/yum.repos.d/versatushpc-opencattus.repo https://repos.versatushpc.com.br/opencattus/el8/versatushpc-opencattus.repo"
echo "      EL9:  curl -o /etc/yum.repos.d/versatushpc-opencattus.repo https://repos.versatushpc.com.br/opencattus/el9/versatushpc-opencattus.repo"
echo "      EL10: curl -o /etc/yum.repos.d/versatushpc-opencattus.repo https://repos.versatushpc.com.br/opencattus/el10/versatushpc-opencattus.repo"
