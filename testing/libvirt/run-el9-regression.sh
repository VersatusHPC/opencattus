#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
HARNESS="${SCRIPT_DIR}/opencattus-el9-lab.sh"

readonly SCRIPT_DIR
readonly HARNESS

JOBS=1
LOG_ROOT=${LOG_ROOT:-/var/tmp/opencattus-el9-regression}

usage() {
    cat <<'EOF'
Usage:
  run-el9-regression.sh [-j jobs] ENVFILE [ENVFILE...]

Run one or more prepared EL9 libvirt lab configs through the shared harness and
print a pass/fail summary. Each env file should already point at valid host-side
assets such as BASE_IMAGE, CLUSTER_ISO, and either OPENCATTUS_BINARY or
OPENCATTUS_SOURCE_DIR.

Options:
  -j JOBS  Number of labs to run in parallel. Default: 1
  -h       Show this help text.

Logs:
  The wrapper stores per-lane console logs under
  /var/tmp/opencattus-el9-regression/<timestamp>/ by default. Override
  LOG_ROOT to place them elsewhere.
EOF
}

die() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

lane_name() {
    local envfile=$1
    local name

    name=$(basename -- "${envfile}")
    name=${name%.env}
    name=${name%.example}
    printf '%s' "${name}"
}

run_dir_name() {
    date '+%Y%m%d-%H%M%S'
}

parse_args() {
    while getopts ':j:h' opt; do
        case "${opt}" in
            j)
                JOBS=${OPTARG}
                ;;
            h)
                usage
                exit 0
                ;;
            :)
                die "Option -${OPTARG} requires an argument"
                ;;
            \?)
                die "Unknown option: -${OPTARG}"
                ;;
        esac
    done

    shift $((OPTIND - 1))
    [[ $# -gt 0 ]] || {
        usage
        exit 1
    }

    ENV_FILES=("$@")
}

start_lane() {
    local envfile=$1
    local name=$2
    local logfile=$3

    printf '[%s] starting %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "${name}" >&2
    "${HARNESS}" -c "${envfile}" run >"${logfile}" 2>&1 &
    local pid=$!

    PIDS+=("${pid}")
    PID_TO_ENVFILE["${pid}"]="${envfile}"
    PID_TO_NAME["${pid}"]="${name}"
    PID_TO_LOG["${pid}"]="${logfile}"
}

reap_finished_lanes() {
    local remaining=()
    local pid
    local rc
    local name

    for pid in "${PIDS[@]}"; do
        if kill -0 "${pid}" >/dev/null 2>&1; then
            remaining+=("${pid}")
            continue
        fi

        rc=0
        if ! wait "${pid}"; then
            rc=$?
        fi

        name=${PID_TO_NAME["${pid}"]}
        RESULTS["${name}"]=${rc}

        if [[ "${rc}" -eq 0 ]]; then
            printf '[%s] passed %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "${name}" >&2
        else
            printf '[%s] failed %s (log: %s)\n' \
                "$(date '+%Y-%m-%d %H:%M:%S')" \
                "${name}" \
                "${PID_TO_LOG["${pid}"]}" >&2
        fi
    done

    PIDS=("${remaining[@]}")
}

main() {
    local envfile
    local name
    local logfile
    local run_dir
    local failed=0
    local lane

    parse_args "$@"

    [[ "${JOBS}" =~ ^[1-9][0-9]*$ ]] || die "JOBS must be a positive integer"
    [[ -x "${HARNESS}" ]] || die "Harness is not executable: ${HARNESS}"

    for envfile in "${ENV_FILES[@]}"; do
        [[ -f "${envfile}" ]] || die "Env file not found: ${envfile}"
    done

    run_dir="${LOG_ROOT}/$(run_dir_name)"
    mkdir -p "${run_dir}"

    declare -gA PID_TO_ENVFILE=()
    declare -gA PID_TO_NAME=()
    declare -gA PID_TO_LOG=()
    declare -gA RESULTS=()
    declare -ga PIDS=()

    for envfile in "${ENV_FILES[@]}"; do
        while (( ${#PIDS[@]} >= JOBS )); do
            reap_finished_lanes
            if (( ${#PIDS[@]} >= JOBS )); then
                sleep 1
            fi
        done

        name=$(lane_name "${envfile}")
        logfile="${run_dir}/${name}.log"
        start_lane "${envfile}" "${name}" "${logfile}"
    done

    while (( ${#PIDS[@]} > 0 )); do
        reap_finished_lanes
        if (( ${#PIDS[@]} > 0 )); then
            sleep 1
        fi
    done

    printf '\nEL9 regression summary\n' >&2
    printf 'Logs: %s\n' "${run_dir}" >&2
    for envfile in "${ENV_FILES[@]}"; do
        lane=$(lane_name "${envfile}")
        if [[ "${RESULTS["${lane}"]}" -eq 0 ]]; then
            printf '  PASS  %s\n' "${lane}" >&2
        else
            printf '  FAIL  %s\n' "${lane}" >&2
            failed=1
        fi
    done

    exit "${failed}"
}

main "$@"
