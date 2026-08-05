#!/usr/bin/env bash

# Stub-based tests for the ${WORKSPACE} ownership-restore exit trap in
# opencattus-el9-lab.sh. The harness runs as real root in CI, so these
# tests never touch real ownership: `id` is stubbed to report root and
# `chown` is stubbed to record its arguments, while the real `find`
# selects paths inside a throwaway workspace.

set -Eeuo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
HARNESS_DIR=$(cd -- "${SCRIPT_DIR}/.." && pwd)

TARGET_UID=4242
TARGET_GID=4343

SANDBOXES=()

cleanup_sandboxes() {
    local sandbox
    for sandbox in "${SANDBOXES[@]:-}"; do
        [[ -n "${sandbox}" ]] && rm -rf "${sandbox}"
    done
}
trap cleanup_sandboxes EXIT

log() {
    printf '%s\n' "$*" >&2
}

die() {
    log "FAIL: $*"
    exit 1
}

# Creates a sandbox with a fake workspace and PATH stubs, and exports
# SANDBOX, STUB_BIN, WORKSPACE_DIR, and CHOWN_LOG for the current case.
make_sandbox() {
    SANDBOX=$(mktemp -d)
    SANDBOXES+=("${SANDBOX}")

    STUB_BIN="${SANDBOX}/stub-bin"
    WORKSPACE_DIR="${SANDBOX}/workspace"
    CHOWN_LOG="${SANDBOX}/chown.log"

    mkdir -p "${STUB_BIN}" "${WORKSPACE_DIR}/nested"
    : >"${WORKSPACE_DIR}/build-artifact"
    : >"${WORKSPACE_DIR}/nested/lab-log"

    cat >"${STUB_BIN}/id" <<'EOF'
#!/usr/bin/env bash
if [[ "${1:-}" == "-u" ]]; then
    printf '%s\n' "${LAB_TEST_ID_UID:-0}"
else
    exec /usr/bin/id "$@"
fi
EOF

    cat >"${STUB_BIN}/chown" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >>"${LAB_TEST_CHOWN_LOG}"
EOF

    chmod +x "${STUB_BIN}/id" "${STUB_BIN}/chown"
}

# Builds the controlled harness environment into the ENV_ARGS global.
# usage: build_lab_env_args <id_uid> <workspace> <sudo_uid> <sudo_gid>
# Empty workspace/sudo values leave the matching variable unset.
build_lab_env_args() {
    local id_uid=$1
    local workspace=$2
    local sudo_uid=$3
    local sudo_gid=$4

    ENV_ARGS=(
        PATH="${STUB_BIN}:${PATH}"
        HOME="${SANDBOX}"
        LAB_TEST_ID_UID="${id_uid}"
        LAB_TEST_CHOWN_LOG="${CHOWN_LOG}"
    )
    [[ -n "${workspace}" ]] && ENV_ARGS+=(WORKSPACE="${workspace}")
    [[ -n "${sudo_uid}" ]] && ENV_ARGS+=(SUDO_UID="${sudo_uid}")
    [[ -n "${sudo_gid}" ]] && ENV_ARGS+=(SUDO_GID="${sudo_gid}")
    return 0
}

# Runs a lab script inside the sandbox with a controlled environment.
# usage: run_lab <script> <id_uid> <workspace> <sudo_uid> <sudo_gid> [args...]
run_lab() {
    local script=$1
    shift

    build_lab_env_args "$1" "$2" "$3" "$4"
    shift 4

    env -i "${ENV_ARGS[@]}" "${script}" "$@"
}

assert_chown_restored_workspace() {
    local context=$1

    [[ -s "${CHOWN_LOG}" ]] || die "${context}: expected chown to run, but nothing was recorded"

    local line
    local -a words
    local path
    while IFS= read -r line; do
        read -r -a words <<<"${line}"
        [[ "${words[0]}" == "-h" ]] \
            || die "${context}: chown must not follow symlinks, got: ${line}"
        [[ "${words[1]}" == "${TARGET_UID}:${TARGET_GID}" ]] \
            || die "${context}: chown must target SUDO_UID:SUDO_GID, got: ${line}"
        for path in "${words[@]:2}"; do
            [[ "${path}" == "${WORKSPACE_DIR}" || "${path}" == "${WORKSPACE_DIR}"/* ]] \
                || die "${context}: chown touched a path outside the workspace: ${path}"
        done
    done <"${CHOWN_LOG}"
}

assert_no_chown() {
    local context=$1

    [[ ! -s "${CHOWN_LOG}" ]] \
        || die "${context}: chown ran but should have been skipped: $(cat "${CHOWN_LOG}")"
}

test_success_restores_ownership_in_every_variant() {
    local variant
    local rc

    for variant in \
        opencattus-el8-lab.sh \
        opencattus-el9-lab.sh \
        opencattus-el10-lab.sh \
        opencattus-ubuntu24-lab.sh; do
        make_sandbox

        rc=0
        run_lab "${HARNESS_DIR}/${variant}" 0 "${WORKSPACE_DIR}" \
            "${TARGET_UID}" "${TARGET_GID}" -h >/dev/null 2>&1 || rc=$?
        [[ "${rc}" -eq 0 ]] || die "${variant}: -h should exit 0, got ${rc}"

        assert_chown_restored_workspace "${variant} success path"
        log "ok: ${variant} restores workspace ownership on success"
    done
}

test_failure_still_restores_ownership_and_keeps_exit_code() {
    local rc=0

    make_sandbox
    run_lab "${HARNESS_DIR}/opencattus-el9-lab.sh" 0 "${WORKSPACE_DIR}" \
        "${TARGET_UID}" "${TARGET_GID}" -c "${SANDBOX}/missing.env" run \
        >/dev/null 2>&1 || rc=$?

    [[ "${rc}" -eq 1 ]] || die "failure path: expected exit 1, got ${rc}"
    assert_chown_restored_workspace "failure path"
    log "ok: failure path restores workspace ownership and preserves exit code"
}

test_sigterm_restores_ownership() {
    local rc=0
    local pid
    local marker

    make_sandbox
    marker="${SANDBOX}/lab-running"

    cat >"${SANDBOX}/hang.env" <<EOF
: >'${marker}'
sleep 5 &
wait \$!
EOF

    # exec so the forked subshell becomes the harness itself; without it,
    # kill would signal the subshell and never reach the harness traps.
    build_lab_env_args 0 "${WORKSPACE_DIR}" "${TARGET_UID}" "${TARGET_GID}"
    (
        exec env -i "${ENV_ARGS[@]}" "${HARNESS_DIR}/opencattus-el9-lab.sh" \
            -c "${SANDBOX}/hang.env" run
    ) >/dev/null 2>&1 &
    pid=$!

    for _ in $(seq 1 50); do
        [[ -f "${marker}" ]] && break
        sleep 0.1
    done
    [[ -f "${marker}" ]] || die "signal path: harness never reached the config sleep"

    kill -TERM "${pid}"
    wait "${pid}" || rc=$?

    [[ "${rc}" -eq 143 ]] || die "signal path: expected exit 143, got ${rc}"
    assert_chown_restored_workspace "signal path"
    log "ok: SIGTERM restores workspace ownership and exits 143"
}

test_skipped_without_workspace() {
    make_sandbox
    run_lab "${HARNESS_DIR}/opencattus-el9-lab.sh" 0 "" \
        "${TARGET_UID}" "${TARGET_GID}" -h >/dev/null 2>&1 \
        || die "no-workspace path: -h should exit 0"

    assert_no_chown "no-workspace path"
    log "ok: trap stays inert when WORKSPACE is unset"
}

test_skipped_when_not_root() {
    make_sandbox
    run_lab "${HARNESS_DIR}/opencattus-el9-lab.sh" 1000 "${WORKSPACE_DIR}" \
        "${TARGET_UID}" "${TARGET_GID}" -h >/dev/null 2>&1 \
        || die "non-root path: -h should exit 0"

    assert_no_chown "non-root path"
    log "ok: trap stays inert when the harness does not run as root"
}

test_skipped_without_sudo_ids() {
    local stderr_log

    make_sandbox
    stderr_log="${SANDBOX}/stderr.log"
    run_lab "${HARNESS_DIR}/opencattus-el9-lab.sh" 0 "${WORKSPACE_DIR}" \
        "" "" -h >/dev/null 2>"${stderr_log}" \
        || die "no-sudo path: -h should exit 0"

    assert_no_chown "no-sudo path"
    grep -q 'SUDO_UID' "${stderr_log}" \
        || die "no-sudo path: expected a warning about missing SUDO_UID/SUDO_GID"
    log "ok: trap warns and stays inert without SUDO_UID/SUDO_GID"
}

test_success_restores_ownership_in_every_variant
test_failure_still_restores_ownership_and_keeps_exit_code
test_sigterm_restores_ownership
test_skipped_without_workspace
test_skipped_when_not_root
test_skipped_without_sudo_ids

log "All workspace-ownership tests passed"
