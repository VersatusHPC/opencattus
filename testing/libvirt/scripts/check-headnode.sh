#!/usr/bin/env bash

set -euo pipefail

provisioner=${OPENCATTUS_PROVISIONER:-xcat}

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
        provisioner_services=(
            dhcpd
            tftp.socket
            xcatd
        )
        ;;
    confluent)
        provisioner_services=(
            confluent
            dnsmasq
            httpd
            tftp.socket
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

showmount -e localhost >/dev/null
sacct >/dev/null
sinfo -N -h >/dev/null

if [[ "${provisioner}" == "xcat" ]]; then
    lsdef -t osimage >/dev/null
fi
