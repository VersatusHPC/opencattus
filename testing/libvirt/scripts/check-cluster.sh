#!/usr/bin/env bash

set -euo pipefail

node_list=${OPENCATTUS_NODE_LIST:?OPENCATTUS_NODE_LIST is required}
node_ip_list=${OPENCATTUS_NODE_IP_LIST:?OPENCATTUS_NODE_IP_LIST is required}
verify_timeout=${OPENCATTUS_VERIFY_TIMEOUT_SECONDS:-1800}

read -r -a nodes <<<"${node_list}"
read -r -a node_ips <<<"${node_ip_list}"

if [[ ${#nodes[@]} -eq 0 ]]; then
    echo "No nodes were provided for verification" >&2
    exit 1
fi

if [[ ${#nodes[@]} -ne ${#node_ips[@]} ]]; then
    echo "Node names and node IPs count mismatch" >&2
    exit 1
fi

deadline=$((SECONDS + verify_timeout))

while (( SECONDS < deadline )); do
    sinfo_output=$(sinfo -N -h -o '%N %T' 2>/dev/null || true)
    all_ping=true
    all_ready=true

    for index in "${!nodes[@]}"; do
        node=${nodes[$index]}
        node_ip=${node_ips[$index]}
        node_ping=true

        if ! ping -c 1 -W 1 "${node_ip}" >/dev/null 2>&1; then
            node_ping=false
            all_ping=false
        fi

        state=$(awk -v wanted="${node}" '$1 == wanted { print $2 }' <<<"${sinfo_output}" | tail -n1)

        if [[ "${node_ping}" == true && -n "${state}" ]]; then
            case "${state^^}" in
                DOWN|DOWN*|DRAIN|DRAIN*|FAIL|FAIL*|INVAL|INVAL*)
                    scontrol update nodename="${node}" state=resume >/dev/null 2>&1 || true
                    state=$(sinfo -N -h -o '%N %T' 2>/dev/null | awk -v wanted="${node}" '$1 == wanted { print $2 }' | tail -n1)
                    ;;
            esac
        fi

        case "${state^^}" in
            ALLOCATED|COMPLETING|IDLE|MIXED|POWERING_UP)
                ;;
            *)
                all_ready=false
                ;;
        esac
    done

    if [[ "${all_ping}" == true && "${all_ready}" == true ]]; then
        printf '%s\n' "${sinfo_output}"
        exit 0
    fi

    sleep 10
done

printf 'Timed out waiting for compute nodes to join the cluster.\n' >&2
printf 'Last sinfo output:\n%s\n' "${sinfo_output}" >&2
exit 1
