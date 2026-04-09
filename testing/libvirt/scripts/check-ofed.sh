#!/usr/bin/env bash

set -euo pipefail

node_list=${OPENCATTUS_NODE_LIST:?OPENCATTUS_NODE_LIST is required}
ofed_kind=${OPENCATTUS_OFED_KIND:?OPENCATTUS_OFED_KIND is required}
ofed_version=${OPENCATTUS_OFED_VERSION:-}
provisioner=${OPENCATTUS_PROVISIONER:-unknown}
verify_timeout_seconds=${OPENCATTUS_VERIFY_TIMEOUT_SECONDS:-600}

read -r -a nodes <<<"${node_list}"

if [[ ${#nodes[@]} -eq 0 ]]; then
    echo "No compute nodes were provided for OFED verification" >&2
    exit 1
fi

run_headnode_check() {
    local description=$1
    shift

    echo "[headnode] ${description}"
    "$@"
}

run_node_check() {
    local node=$1
    local description=$2
    shift 2

    echo "[${node}] ${description}"
    timeout -k 10 "${verify_timeout_seconds}" \
        srun --nodes=1 --ntasks=1 --nodelist="${node}" "$@"
}

check_doca() {
    local node

    run_headnode_check \
        "verifying DOCA packages for ${provisioner} (${ofed_version})" \
        bash -lc 'set -euo pipefail
rpm -q doca-ofed mlnx-fw-updater mlnx-ofa_kernel mlnx-ofa_kernel-dkms
module_path=$(modinfo -F filename mlx5_core)
echo "${module_path}"
[[ "${module_path}" == /lib/modules/*/extra/* ]]'

    for node in "${nodes[@]}"; do
        run_node_check \
            "${node}" \
            "verifying DOCA compute-node packages" \
            bash -lc 'set -euo pipefail
rpm -q doca-ofed mlnx-ofa_kernel
module_path=$(modinfo -F filename mlx5_core)
echo "${module_path}"
[[ "${module_path}" == /lib/modules/*/extra/* ]]'
    done
}

check_inbox() {
    local node

    run_headnode_check \
        "verifying inbox OFED packages for ${provisioner}" \
        rpm -q rdma-core

    for node in "${nodes[@]}"; do
        run_node_check \
            "${node}" \
            "verifying inbox compute-node packages" \
            bash -lc "rpm -q rdma-core"
    done
}

case "${ofed_kind}" in
    doca)
        check_doca
        ;;
    inbox)
        check_inbox
        ;;
    *)
        echo "Unsupported OPENCATTUS_OFED_KIND: ${ofed_kind}" >&2
        exit 1
        ;;
esac
