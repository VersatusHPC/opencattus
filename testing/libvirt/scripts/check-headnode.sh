#!/usr/bin/env bash

set -euo pipefail

provisioner=${OPENCATTUS_PROVISIONER:-xcat}
command_timeout=${OPENCATTUS_VERIFY_COMMAND_TIMEOUT_SECONDS:-60}
command_retries=${OPENCATTUS_VERIFY_COMMAND_RETRIES:-3}
command_retry_delay_seconds=${OPENCATTUS_VERIFY_COMMAND_RETRY_DELAY_SECONDS:-5}

run_check() {
    local description=$1
    shift
    local attempt=1

    while (( attempt <= command_retries )); do
        if timeout "${command_timeout}" "$@" >/dev/null; then
            return 0
        fi

        if (( attempt == command_retries )); then
            echo "Verification command failed: ${description}" >&2
            exit 1
        fi

        sleep "${command_retry_delay_seconds}"
        attempt=$((attempt + 1))
    done
}

warn_check() {
    local description=$1
    shift
    local attempt=1

    while (( attempt <= command_retries )); do
        if timeout "${command_timeout}" "$@" >/dev/null; then
            return 0
        fi

        if (( attempt == command_retries )); then
            echo "Non-fatal verification command failed: ${description}" >&2
            return 1
        fi

        sleep "${command_retry_delay_seconds}"
        attempt=$((attempt + 1))
    done
}

common_services=(
    chronyd
    mariadb
    munge
    nfs-server
    rpcbind
    slurmctld
    slurmdbd
)

case "${provisioner}" in
    xcat)
        export PATH="/opt/xcat/bin:/opt/xcat/sbin:${PATH}"
        provisioner_services=(
            dhcpd
            xcatd
        )
        ;;
    confluent)
        provisioner_services=(
            confluent
            dnsmasq
            httpd
        )
        ;;
    *)
        echo "Unsupported provisioner: ${provisioner}" >&2
        exit 1
        ;;
esac

for service in "${common_services[@]}" "${provisioner_services[@]}"; do
    if ! systemctl is-active --quiet "${service}"; then
        echo "Required service is not active: ${service}" >&2
        exit 1
    fi
done

run_check "showmount -e localhost" showmount -e localhost
run_check "sinfo -N -h" sinfo -N -h
warn_check "sacct" sacct || true

if [[ "${provisioner}" == "xcat" ]]; then
    run_check "lsdef -t osimage" lsdef -t osimage
fi
