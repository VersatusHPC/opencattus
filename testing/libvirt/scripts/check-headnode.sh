#!/usr/bin/env bash

set -euo pipefail

provisioner=${OPENCATTUS_PROVISIONER:-xcat}
command_timeout=${OPENCATTUS_VERIFY_COMMAND_TIMEOUT_SECONDS:-60}

run_check() {
    local description=$1
    shift

    if ! timeout "${command_timeout}" "$@" >/dev/null; then
        echo "Verification command failed: ${description}" >&2
        exit 1
    fi
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
run_check "sacct" sacct
run_check "sinfo -N -h" sinfo -N -h

if [[ "${provisioner}" == "xcat" ]]; then
    run_check "lsdef -t osimage" lsdef -t osimage
fi
