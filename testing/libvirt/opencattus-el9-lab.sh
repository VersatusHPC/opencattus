#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd -- "$SCRIPT_DIR/../.." && pwd)

readonly SCRIPT_DIR
readonly REPO_ROOT

CONFIG_FILE=""

usage() {
    cat <<'EOF'
Usage:
  opencattus-el9-lab.sh [-c config.env] <command>

Commands:
  create    Define libvirt networks, create the headnode VM, and define compute VMs.
  install   Copy the binary, ISO, and answerfile to the headnode and run OpenCATTUS.
  boot      Start or restart the compute VMs so they PXE-boot from the headnode.
  verify    Run guest-side verification for the headnode, cluster, and MPI smoke test.
  collect   Gather host-side and guest-side logs into the lab state directory.
  destroy   Destroy the lab VMs, undefine the networks, and remove the state directory.
  status    Show the current libvirt state for the lab.
  run       Destroy any existing lab, then create, install, boot, verify, and collect logs.

Options:
  -c FILE   Source configuration values from FILE before applying defaults.
  -h        Show this help text.

The harness currently serves two purposes:
  * the validated EL9 recovery paths for xCAT and Confluent
  * the Rocky Linux 10 + Confluent bootstrap path for EL10 porting work
EOF
}

log() {
    printf '[%s] %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*" >&2
}

die() {
    log "ERROR: $*"
    exit 1
}

need_cmd() {
    command -v "$1" >/dev/null 2>&1 || die "Required command not found: $1"
}

need_exec() {
    [[ -x "$1" ]] || die "Required executable not found: $1"
}

as_root() {
    if [[ $(id -u) -eq 0 ]]; then
        "$@"
    else
        sudo "$@"
    fi
}

short_token() {
    local sanitized
    sanitized=$(printf '%s' "$1" | tr -cd '[:alnum:]' | tr '[:upper:]' '[:lower:]')

    if [[ -z "${sanitized}" ]]; then
        die "LAB_NAME must contain at least one alphanumeric character"
    fi

    # Bridge names are capped at 15 characters, so keep the token short while
    # still deriving it from the full lab name to avoid collisions between
    # similarly prefixed labs (for example "...97" vs "...97b").
    printf '%s' "${sanitized}" | sha256sum | cut -c1-8
}

token_hex_byte() {
    local start=$1
    printf '%s' "${BRIDGE_TOKEN:${start}:2}"
}

default_headnode_ext_mac() {
    printf '52:54:%s:%s:10:01' \
        "$(token_hex_byte 0)" \
        "$(token_hex_byte 2)"
}

default_headnode_mgmt_mac() {
    printf '52:54:%s:%s:20:01' \
        "$(token_hex_byte 0)" \
        "$(token_hex_byte 2)"
}

default_remote_build_preset() {
    if (( DISTRO_MAJOR >= 10 )); then
        printf 'el10-gcc-release'
    else
        printf 'rhel9-gcc-toolset-14-release'
    fi
}

default_remote_build_preset_build() {
    if (( DISTRO_MAJOR >= 10 )); then
        printf 'el10-gcc-release-build'
    else
        printf 'rhel9-gcc-toolset-14-release-build'
    fi
}

headnode_glibmm_package() {
    if (( DISTRO_MAJOR >= 10 )); then
        printf 'glibmm2.68'
    else
        printf 'glibmm24'
    fi
}

virt_install_osinfo_name() {
    if (( DISTRO_MAJOR >= 10 )); then
        printf 'generic'
    else
        printf 'rocky9'
    fi
}

vbmc_required() {
    [[ "${PROVISIONER}" == "xcat" ]]
}

load_defaults() {
    LAB_NAME=${LAB_NAME:-opencattus-el9}

    DISTRO_ID=${DISTRO_ID:-rocky}
    DISTRO_VERSION=${DISTRO_VERSION:-9.6}
    DISTRO_MAJOR=${DISTRO_MAJOR:-${DISTRO_VERSION%%.*}}

    if [[ -z "${PROVISIONER+x}" ]]; then
        if (( DISTRO_MAJOR >= 10 )); then
            PROVISIONER=confluent
        else
            PROVISIONER=xcat
        fi
    fi

    BASE_IMAGE=${BASE_IMAGE:-}
    CLUSTER_ISO=${CLUSTER_ISO:-}
    OPENCATTUS_BINARY=${OPENCATTUS_BINARY:-}
    OPENCATTUS_SOURCE_DIR=${OPENCATTUS_SOURCE_DIR:-${REPO_ROOT}}

    STATE_ROOT=${STATE_ROOT:-/var/tmp/opencattus-lab}
    LAB_DIR=${LAB_DIR:-${STATE_ROOT}/${LAB_NAME}}
    LOG_DIR=${LOG_DIR:-${LAB_DIR}/logs}
    IMAGE_ROOT=${IMAGE_ROOT:-/var/lib/libvirt/images/opencattus-lab}
    IMAGE_DIR=${IMAGE_DIR:-${IMAGE_ROOT}/${LAB_NAME}}

    SSH_USER=${SSH_USER:-opencattus}
    SSH_KEY_DIR=${SSH_KEY_DIR:-${LAB_DIR}/ssh}
    SSH_PRIVATE_KEY=${SSH_PRIVATE_KEY:-${SSH_KEY_DIR}/id_ed25519}
    SSH_PUBLIC_KEY=${SSH_PUBLIC_KEY:-${SSH_PRIVATE_KEY}.pub}
    SSH_OPTIONS=(
        -i "${SSH_PRIVATE_KEY}"
        -o StrictHostKeyChecking=no
        -o UserKnownHostsFile=/dev/null
        -o LogLevel=ERROR
        -o ConnectTimeout=10
    )

    BRIDGE_TOKEN=${BRIDGE_TOKEN:-$(short_token "${LAB_NAME}")}
    EXTERNAL_NETWORK_NAME=${EXTERNAL_NETWORK_NAME:-${LAB_NAME}-ext}
    EXTERNAL_BRIDGE=${EXTERNAL_BRIDGE:-oc${BRIDGE_TOKEN}e0}
    EXTERNAL_GATEWAY_IP=${EXTERNAL_GATEWAY_IP:-192.168.124.1}
    EXTERNAL_NETMASK=${EXTERNAL_NETMASK:-255.255.255.0}
    EXTERNAL_DHCP_START=${EXTERNAL_DHCP_START:-192.168.124.100}
    EXTERNAL_DHCP_END=${EXTERNAL_DHCP_END:-192.168.124.199}
    EXTERNAL_HEADNODE_IP=${EXTERNAL_HEADNODE_IP:-192.168.124.10}

    MANAGEMENT_NETWORK_NAME=${MANAGEMENT_NETWORK_NAME:-${LAB_NAME}-mgmt}
    MANAGEMENT_BRIDGE=${MANAGEMENT_BRIDGE:-oc${BRIDGE_TOKEN}m0}
    MANAGEMENT_GATEWAY_IP=${MANAGEMENT_GATEWAY_IP:-192.168.30.254}
    MANAGEMENT_NETMASK=${MANAGEMENT_NETMASK:-255.255.255.0}
    MANAGEMENT_PREFIX=${MANAGEMENT_PREFIX:-24}
    MANAGEMENT_HOST_IP=${MANAGEMENT_HOST_IP:-192.168.30.253}

    HEADNODE_NAME=${HEADNODE_NAME:-${LAB_NAME}-headnode}
    HEADNODE_EXT_MAC=${HEADNODE_EXT_MAC:-$(default_headnode_ext_mac)}
    HEADNODE_MGMT_MAC=${HEADNODE_MGMT_MAC:-$(default_headnode_mgmt_mac)}
    HEADNODE_MEMORY_MB=${HEADNODE_MEMORY_MB:-16384}
    HEADNODE_VCPUS=${HEADNODE_VCPUS:-4}
    HEADNODE_DISK_GB=${HEADNODE_DISK_GB:-220}
    HEADNODE_DISK=${HEADNODE_DISK:-${IMAGE_DIR}/${HEADNODE_NAME}.qcow2}
    HEADNODE_SEED_ISO=${HEADNODE_SEED_ISO:-${IMAGE_DIR}/${HEADNODE_NAME}-seed.iso}
    HEADNODE_DATA_DIR=${HEADNODE_DATA_DIR:-/home/${SSH_USER}/opencattus-lab}
    REMOTE_SOURCE_DIR=${REMOTE_SOURCE_DIR:-${HEADNODE_DATA_DIR}/source/opencattus}

    COMPUTE_COUNT=${COMPUTE_COUNT:-1}
    COMPUTE_MEMORY_MB=${COMPUTE_MEMORY_MB:-8192}
    COMPUTE_VCPUS=${COMPUTE_VCPUS:-2}

    CLUSTER_NAME=${CLUSTER_NAME:-opencattus}
    COMPANY_NAME=${COMPANY_NAME:-opencattus-enterprises}
    ADMIN_EMAIL=${ADMIN_EMAIL:-foo@example.com}
    CLUSTER_HOSTNAME=${CLUSTER_HOSTNAME:-opencattus}
    CLUSTER_DOMAIN=${CLUSTER_DOMAIN:-cluster.example.com}
    EXTERNAL_DOMAIN=${EXTERNAL_DOMAIN:-cluster.external.example.com}
    MANAGEMENT_DOMAIN=${MANAGEMENT_DOMAIN:-cluster.management.example.com}

    TIMEZONE=${TIMEZONE:-UTC}
    TIMESERVER=${TIMESERVER:-pool.ntp.org}
    LOCALE=${LOCALE:-en_US.utf8}

    NODE_PREFIX=${NODE_PREFIX:-n}
    NODE_PADDING=${NODE_PADDING:-2}
    NODE_ROOT_PASSWORD=${NODE_ROOT_PASSWORD:-labroot}
    NODE_SOCKETS=${NODE_SOCKETS:-1}
    NODE_CORES_PER_SOCKET=${NODE_CORES_PER_SOCKET:-${COMPUTE_VCPUS}}
    NODE_THREADS_PER_CORE=${NODE_THREADS_PER_CORE:-1}
    NODE_CPUS_PER_NODE=${NODE_CPUS_PER_NODE:-${COMPUTE_VCPUS}}
    NODE_REAL_MEMORY_MB=${NODE_REAL_MEMORY_MB:-4096}
    NODE_BMC_USERNAME=${NODE_BMC_USERNAME:-admin}
    NODE_BMC_PASSWORD=${NODE_BMC_PASSWORD:-admin}
    NODE_BMC_SERIALPORT=${NODE_BMC_SERIALPORT:-0}
    NODE_BMC_SERIALSPEED=${NODE_BMC_SERIALSPEED:-9600}
    NODE_IP_BASE_OCTET=${NODE_IP_BASE_OCTET:-1}
    NODE_BMC_IP_BASE_OCTET=${NODE_BMC_IP_BASE_OCTET:-101}

    PARTITION_NAME=${PARTITION_NAME:-batch}
    MARIADB_ROOT_PASSWORD=${MARIADB_ROOT_PASSWORD:-LabMariadbRoot!23}
    SLURMDB_PASSWORD=${SLURMDB_PASSWORD:-LabSlurmDb!23}
    STORAGE_PASSWORD=${STORAGE_PASSWORD:-LabStorage!23}

    BUILD_TIMEOUT_SECONDS=${BUILD_TIMEOUT_SECONDS:-10800}
    INSTALL_TIMEOUT_SECONDS=${INSTALL_TIMEOUT_SECONDS:-10800}
    VERIFY_TIMEOUT_SECONDS=${VERIFY_TIMEOUT_SECONDS:-1800}

    ANSWERFILE_PATH=${ANSWERFILE_PATH:-${LAB_DIR}/answerfile.ini}
    REMOTE_BINARY_PATH=${REMOTE_BINARY_PATH:-${HEADNODE_DATA_DIR}/opencattus}
    REMOTE_BUILD_PRESET=${REMOTE_BUILD_PRESET:-$(default_remote_build_preset)}
    REMOTE_BUILD_PRESET_BUILD=${REMOTE_BUILD_PRESET_BUILD:-$(default_remote_build_preset_build)}
    if [[ -z "${REMOTE_BUILD_JOBS:-}" ]]; then
        if (( HEADNODE_VCPUS > 4 )); then
            REMOTE_BUILD_JOBS=4
        else
            REMOTE_BUILD_JOBS=${HEADNODE_VCPUS}
        fi
    fi
    REMOTE_BUILD_BINARY=${REMOTE_BUILD_BINARY:-${REMOTE_SOURCE_DIR}/out/build/${REMOTE_BUILD_PRESET}/src/opencattus}
    REMOTE_ISO_PATH=${REMOTE_ISO_PATH:-${HEADNODE_DATA_DIR}/cluster.iso}
    REMOTE_ANSWERFILE_PATH=${REMOTE_ANSWERFILE_PATH:-${HEADNODE_DATA_DIR}/answerfile.ini}
    REMOTE_CHECK_HEADNODE=${REMOTE_CHECK_HEADNODE:-${HEADNODE_DATA_DIR}/check-headnode.sh}
    REMOTE_CHECK_CLUSTER=${REMOTE_CHECK_CLUSTER:-${HEADNODE_DATA_DIR}/check-cluster.sh}
    REMOTE_CHECK_MPI=${REMOTE_CHECK_MPI:-${HEADNODE_DATA_DIR}/check-mpi.sh}
    LOCAL_REPO_CONFIG_DIR=${LOCAL_REPO_CONFIG_DIR:-${REPO_ROOT}/repos}
    REMOTE_REPO_CONFIG_STAGING=${REMOTE_REPO_CONFIG_STAGING:-${HEADNODE_DATA_DIR}/repos}
    REMOTE_INSTALL_ROOT=${REMOTE_INSTALL_ROOT:-/opt/opencattus}
    REMOTE_INSTALL_CONF_DIR=${REMOTE_INSTALL_CONF_DIR:-${REMOTE_INSTALL_ROOT}/conf}
    REMOTE_INSTALL_REPO_DIR=${REMOTE_INSTALL_REPO_DIR:-${REMOTE_INSTALL_CONF_DIR}/repos}

    MPI_SMOKE_TEST=${MPI_SMOKE_TEST:-1}
    MPI_SMOKE_NODES=${MPI_SMOKE_NODES:-${COMPUTE_COUNT}}
    MPI_SMOKE_TASKS=${MPI_SMOKE_TASKS:-${COMPUTE_COUNT}}
    MPI_SMOKE_WORKDIR=${MPI_SMOKE_WORKDIR:-/home/${SSH_USER}/opencattus-mpi-smoke}
    MPI_SMOKE_OUTPUT=${MPI_SMOKE_OUTPUT:-${MPI_SMOKE_WORKDIR}/mpi-hello.out}

    VBMC_BIN=${VBMC_BIN:-/usr/local/bin/vbmc}
    VBMCD_BIN=${VBMCD_BIN:-/usr/local/bin/vbmcd}
    VBMC_CONFIG_ROOT=${VBMC_CONFIG_ROOT:-${LAB_DIR}/vbmc-root}
    VBMC_CONFIG_DIR=${VBMC_CONFIG_DIR:-${VBMC_CONFIG_ROOT}/config}
    VBMC_CONFIG_FILE=${VBMC_CONFIG_FILE:-${VBMC_CONFIG_ROOT}/virtualbmc.conf}
    VBMC_LOG_FILE=${VBMC_LOG_FILE:-${VBMC_CONFIG_ROOT}/vbmcd.log}
    VBMC_PID_FILE=${VBMC_PID_FILE:-${VBMC_CONFIG_ROOT}/master.pid}
    VBMC_SERVER_PORT=${VBMC_SERVER_PORT:-50892}
}

remote_host() {
    printf '%s@%s' "${SSH_USER}" "${EXTERNAL_HEADNODE_IP}"
}

node_domain_name() {
    local index=$1
    printf '%s-compute%02d' "${LAB_NAME}" "${index}"
}

node_name() {
    local index=$1
    printf '%s%0*d' "${NODE_PREFIX}" "${NODE_PADDING}" "${index}"
}

management_subnet_prefix() {
    local octet1 octet2 octet3 octet4
    IFS=. read -r octet1 octet2 octet3 octet4 <<<"${MANAGEMENT_GATEWAY_IP}"
    printf '%s.%s.%s' "${octet1}" "${octet2}" "${octet3}"
}

node_ip() {
    local index=$1
    printf '%s.%d' "$(management_subnet_prefix)" \
        $((NODE_IP_BASE_OCTET + index - 1))
}

node_bmc_ip() {
    local index=$1
    printf '%s.%d' "$(management_subnet_prefix)" \
        $((NODE_BMC_IP_BASE_OCTET + index - 1))
}

node_mac() {
    local index=$1
    printf '52:54:%s:%s:20:%02x' \
        "$(token_hex_byte 0)" \
        "$(token_hex_byte 2)" \
        $((0x10 + index))
}

headnode_user_data_path() {
    printf '%s/%s-user-data.yml' "${LAB_DIR}" "${HEADNODE_NAME}"
}

headnode_meta_data_path() {
    printf '%s/%s-meta-data.yml' "${LAB_DIR}" "${HEADNODE_NAME}"
}

headnode_network_config_path() {
    printf '%s/%s-network-config.yml' "${LAB_DIR}" "${HEADNODE_NAME}"
}

ensure_lab_dirs() {
    mkdir -p "${LAB_DIR}" "${LOG_DIR}"
    as_root mkdir -p "${IMAGE_DIR}"
}

restore_image_labels() {
    if command -v restorecon >/dev/null 2>&1; then
        as_root restorecon -RF "${IMAGE_DIR}" >/dev/null 2>&1 || true
    fi
}

vbmc_cli() {
    [[ -f "${VBMC_CONFIG_FILE}" ]] || die \
        "Expected lab VirtualBMC config at ${VBMC_CONFIG_FILE}"
    as_root env \
        VIRTUALBMC_CONFIG="${VBMC_CONFIG_FILE}" \
        PATH="/usr/local/bin:/usr/bin:/usr/sbin:/bin:/sbin" \
        "${VBMC_BIN}" "$@"
}

write_vbmc_config() {
    mkdir -p "${VBMC_CONFIG_ROOT}" "${VBMC_CONFIG_DIR}"
    cat >"${VBMC_CONFIG_FILE}" <<EOF
[default]
config_dir = ${VBMC_CONFIG_DIR}
pid_file = ${VBMC_PID_FILE}
server_port = ${VBMC_SERVER_PORT}
server_spawn_wait = 10000
server_response_timeout = 10000

[log]
debug = true
logfile = ${VBMC_LOG_FILE}
EOF
}

management_firewall_zone() {
    as_root firewall-cmd --get-zone-of-interface="${MANAGEMENT_BRIDGE}" 2>/dev/null \
        || printf 'libvirt'
}

vbmc_firewall_rule() {
    printf 'rule family="ipv4" source address="%s/32" port protocol="udp" port="623" accept' \
        "${MANAGEMENT_GATEWAY_IP}"
}

is_real_pid() {
    local pid=${1:-}
    [[ "${pid}" =~ ^[0-9]+$ ]] && (( pid > 1 ))
}

ensure_bridge_ip() {
    local address=$1
    local prefix=$2

    if ! ip -4 -o addr show dev "${MANAGEMENT_BRIDGE}" | grep -Fq " ${address}/${prefix} "; then
        as_root ip addr add "${address}/${prefix}" dev "${MANAGEMENT_BRIDGE}"
    fi
}

drop_bridge_ip() {
    local address=$1
    local prefix=$2

    as_root ip addr del "${address}/${prefix}" dev "${MANAGEMENT_BRIDGE}" >/dev/null 2>&1 || true
}

allow_vbmc_firewall() {
    local zone
    zone=$(management_firewall_zone)
    as_root firewall-cmd --zone="${zone}" \
        --add-rich-rule="$(vbmc_firewall_rule)" >/dev/null 2>&1 || true
}

remove_vbmc_firewall() {
    local zone
    zone=$(management_firewall_zone)
    as_root firewall-cmd --zone="${zone}" \
        --remove-rich-rule="$(vbmc_firewall_rule)" >/dev/null 2>&1 || true
}

start_vbmcd() {
    local pid=""
    local deadline=$((SECONDS + 15))

    write_vbmc_config

    if [[ -f "${VBMC_PID_FILE}" ]]; then
        pid=$(as_root cat "${VBMC_PID_FILE}" 2>/dev/null || true)
        if is_real_pid "${pid}" && as_root kill -0 "${pid}" >/dev/null 2>&1; then
            return 0
        fi
        as_root rm -f "${VBMC_PID_FILE}"
    fi

    as_root env \
        VIRTUALBMC_CONFIG="${VBMC_CONFIG_FILE}" \
        PATH="/usr/local/bin:/usr/bin:/usr/sbin:/bin:/sbin" \
        "${VBMCD_BIN}" >/dev/null 2>&1

    while (( SECONDS < deadline )); do
        if vbmc_cli list >/dev/null 2>&1; then
            return 0
        fi
        sleep 1
    done

    die "Timed out waiting for the lab VirtualBMC daemon to start"
}

stop_vbmcd() {
    local pid=""

    if [[ -f "${VBMC_PID_FILE}" ]]; then
        pid=$(as_root cat "${VBMC_PID_FILE}" 2>/dev/null || true)
        if is_real_pid "${pid}"; then
            as_root kill "${pid}" >/dev/null 2>&1 || true
        fi
        as_root rm -f "${VBMC_PID_FILE}"
    fi
}

configure_vbmc() {
    local index
    local domain

    vbmc_required || return 0

    ensure_bridge_ip "${MANAGEMENT_HOST_IP}" "${MANAGEMENT_PREFIX}"
    allow_vbmc_firewall
    start_vbmcd

    for (( index = 1; index <= COMPUTE_COUNT; index++ )); do
        domain=$(node_domain_name "${index}")
        ensure_bridge_ip "$(node_bmc_ip "${index}")" 32
        vbmc_cli stop "${domain}" >/dev/null 2>&1 || true
        vbmc_cli delete "${domain}" >/dev/null 2>&1 || true
        vbmc_cli add \
            --username "${NODE_BMC_USERNAME}" \
            --password "${NODE_BMC_PASSWORD}" \
            --address "$(node_bmc_ip "${index}")" \
            --libvirt-uri qemu:///system \
            "${domain}"
        vbmc_cli start "${domain}"
    done
}

destroy_vbmc() {
    local index
    local domain

    vbmc_required || return 0
    [[ -f "${VBMC_CONFIG_FILE}" ]] || return 0

    for (( index = 1; index <= COMPUTE_COUNT; index++ )); do
        domain=$(node_domain_name "${index}")
        vbmc_cli stop "${domain}" >/dev/null 2>&1 || true
        vbmc_cli delete "${domain}" >/dev/null 2>&1 || true
        drop_bridge_ip "$(node_bmc_ip "${index}")" 32
    done

    stop_vbmcd
    remove_vbmc_firewall
    drop_bridge_ip "${MANAGEMENT_HOST_IP}" "${MANAGEMENT_PREFIX}"
    as_root rm -rf "${VBMC_CONFIG_ROOT}"
}

render_headnode_cloud_init() {
    local pubkey
    pubkey=$(<"${SSH_PUBLIC_KEY}")

    cat >"$(headnode_user_data_path)" <<EOF
#cloud-config
hostname: ${HEADNODE_NAME}
fqdn: ${HEADNODE_NAME}.${CLUSTER_DOMAIN}
manage_etc_hosts: true
package_update: true
packages:
  - dnf-plugins-core
  - $(headnode_glibmm_package)
  - newt
  - qemu-guest-agent
  - rsync
  - tar
  - wget
users:
  - default
  - name: ${SSH_USER}
    gecos: OpenCATTUS Lab
    groups: [wheel]
    lock_passwd: true
    shell: /bin/bash
    sudo: ALL=(ALL) NOPASSWD:ALL
    ssh_authorized_keys:
      - ${pubkey}
runcmd:
  - [systemctl, enable, --now, qemu-guest-agent]
EOF

    cat >"$(headnode_meta_data_path)" <<EOF
instance-id: ${HEADNODE_NAME}
local-hostname: ${HEADNODE_NAME}
EOF

    cat >"$(headnode_network_config_path)" <<EOF
version: 2
ethernets:
  external:
    match:
      macaddress: "${HEADNODE_EXT_MAC}"
    set-name: "oc-ext0"
    dhcp4: true
    dhcp6: false
  management:
    match:
      macaddress: "${HEADNODE_MGMT_MAC}"
    set-name: "oc-mgmt0"
    dhcp4: false
    dhcp6: false
    optional: true
EOF
}

ensure_ssh_key() {
    ensure_lab_dirs
    mkdir -p "${SSH_KEY_DIR}"
    if [[ ! -f "${SSH_PRIVATE_KEY}" ]]; then
        ssh-keygen -q -t ed25519 -N '' -f "${SSH_PRIVATE_KEY}"
    fi
}

check_host_prereqs() {
    local required=(
        firewall-cmd
        ip
        mkisofs
        qemu-img
        rsync
        scp
        sed
        ssh
        ssh-keygen
        timeout
        virt-install
        virsh
    )

    for cmd in "${required[@]}"; do
        need_cmd "${cmd}"
    done

    if vbmc_required; then
        need_exec "${VBMC_BIN}"
        need_exec "${VBMCD_BIN}"
    fi

    as_root virsh uri >/dev/null
}

check_config() {
    local topology_vcpus

    [[ "${PROVISIONER}" == "xcat" || "${PROVISIONER}" == "confluent" ]] || die \
        "Unsupported provisioner ${PROVISIONER}; expected xcat or confluent"
    if (( DISTRO_MAJOR >= 10 )); then
        [[ "${DISTRO_ID}" == "rocky" ]] || die \
            "The current EL10 bootstrap lab only targets Rocky Linux 10"
        [[ "${PROVISIONER}" == "confluent" ]] || die \
            "EL10 bootstrap is Confluent-only; xCAT remains out of scope for this lab"
    fi
    [[ -n "${BASE_IMAGE}" ]] || die "BASE_IMAGE is required"
    [[ -n "${CLUSTER_ISO}" ]] || die "CLUSTER_ISO is required"
    [[ -f "${BASE_IMAGE}" ]] || die "Base image not found: ${BASE_IMAGE}"
    [[ -f "${CLUSTER_ISO}" ]] || die "Cluster ISO not found: ${CLUSTER_ISO}"

    if [[ -n "${OPENCATTUS_BINARY}" ]]; then
        [[ -f "${OPENCATTUS_BINARY}" ]] || die "OpenCATTUS binary not found: ${OPENCATTUS_BINARY}"
    else
        [[ -d "${OPENCATTUS_SOURCE_DIR}" ]] || die "OpenCATTUS source tree not found: ${OPENCATTUS_SOURCE_DIR}"
    fi

    [[ -d "${LOCAL_REPO_CONFIG_DIR}" ]] || die "Repo config directory not found: ${LOCAL_REPO_CONFIG_DIR}"

    topology_vcpus=$((NODE_SOCKETS * NODE_CORES_PER_SOCKET * NODE_THREADS_PER_CORE))
    (( topology_vcpus > 0 )) || die "Node CPU topology must resolve to at least one vCPU"
    (( COMPUTE_VCPUS == topology_vcpus )) || die \
        "COMPUTE_VCPUS (${COMPUTE_VCPUS}) must match NODE_SOCKETS * NODE_CORES_PER_SOCKET * NODE_THREADS_PER_CORE (${topology_vcpus})"
    (( NODE_CPUS_PER_NODE == topology_vcpus )) || die \
        "NODE_CPUS_PER_NODE (${NODE_CPUS_PER_NODE}) must match the compute VM CPU topology (${topology_vcpus})"

    [[ "${MPI_SMOKE_TEST}" == "0" || "${MPI_SMOKE_TEST}" == "1" ]] || die \
        "MPI_SMOKE_TEST must be 0 or 1"
    (( MPI_SMOKE_NODES > 0 )) || die "MPI_SMOKE_NODES must be greater than zero"
    (( MPI_SMOKE_TASKS > 0 )) || die "MPI_SMOKE_TASKS must be greater than zero"
    (( MPI_SMOKE_NODES <= COMPUTE_COUNT )) || die \
        "MPI_SMOKE_NODES (${MPI_SMOKE_NODES}) cannot exceed COMPUTE_COUNT (${COMPUTE_COUNT})"
    (( MPI_SMOKE_TASKS >= MPI_SMOKE_NODES )) || die \
        "MPI_SMOKE_TASKS (${MPI_SMOKE_TASKS}) must be at least MPI_SMOKE_NODES (${MPI_SMOKE_NODES})"
}

answerfile_template_path() {
    local distro_template="${SCRIPT_DIR}/templates/${DISTRO_ID}${DISTRO_MAJOR}-${PROVISIONER}.answerfile.ini"

    if [[ -f "${distro_template}" ]]; then
        printf '%s' "${distro_template}"
        return 0
    fi

    printf '%s' "${SCRIPT_DIR}/templates/rocky9-${PROVISIONER}.answerfile.ini"
}

network_exists() {
    as_root virsh net-info "$1" >/dev/null 2>&1
}

bridge_exists() {
    ip link show dev "$1" >/dev/null 2>&1
}

network_is_active() {
    as_root virsh net-info "$1" 2>/dev/null | awk '$1 == "Active:" { print $2 }' | grep -qx yes
}

domain_exists() {
    as_root virsh dominfo "$1" >/dev/null 2>&1
}

destroy_network_if_exists() {
    local network=$1
    if network_exists "${network}"; then
        as_root virsh net-destroy "${network}" >/dev/null 2>&1 || true
        as_root virsh net-undefine "${network}" >/dev/null 2>&1 || true
    fi
}

destroy_bridge_if_exists() {
    local bridge=$1
    local -a members=()
    local member

    if bridge_exists "${bridge}"; then
        mapfile -t members < <(ip -o link show master "${bridge}" | awk -F': ' '{print $2}' | cut -d'@' -f1 || true)

        for member in "${members[@]}"; do
            [[ -n "${member}" ]] || continue
            as_root ip link set dev "${member}" nomaster >/dev/null 2>&1 || true
            if [[ "${member}" == vnet* || "${member}" == tap* ]]; then
                as_root ip link delete dev "${member}" >/dev/null 2>&1 || true
            fi
        done

        as_root ip link set dev "${bridge}" down >/dev/null 2>&1 || true
        as_root ip link delete dev "${bridge}" type bridge >/dev/null 2>&1 || true
    fi
}

wait_for_bridge_absent() {
    local bridge=$1
    local deadline=$((SECONDS + 10))

    while (( SECONDS < deadline )); do
        if ! bridge_exists "${bridge}"; then
            return 0
        fi
        sleep 1
    done

    die "Timed out waiting for stale bridge ${bridge} to disappear"
}

start_network_with_recovery() {
    local network=$1
    local bridge=$2

    if ! as_root virsh net-start "${network}"; then
        destroy_bridge_if_exists "${bridge}"
        wait_for_bridge_absent "${bridge}"
        as_root virsh net-start "${network}"
    fi

    network_is_active "${network}" || die "Failed to activate libvirt network ${network}"
    as_root virsh net-autostart "${network}"
}

destroy_domain_if_exists() {
    local domain=$1
    if domain_exists "${domain}"; then
        as_root virsh destroy "${domain}" >/dev/null 2>&1 || true
        as_root virsh undefine "${domain}" --nvram >/dev/null 2>&1 || true
    fi
}

write_external_network_xml() {
    cat >"${LAB_DIR}/external-network.xml" <<EOF
<network>
  <name>${EXTERNAL_NETWORK_NAME}</name>
  <forward mode='nat'/>
  <bridge name='${EXTERNAL_BRIDGE}' stp='on' delay='0'/>
  <ip address='${EXTERNAL_GATEWAY_IP}' netmask='${EXTERNAL_NETMASK}'>
    <dhcp>
      <range start='${EXTERNAL_DHCP_START}' end='${EXTERNAL_DHCP_END}'/>
      <host mac='${HEADNODE_EXT_MAC}' name='${HEADNODE_NAME}' ip='${EXTERNAL_HEADNODE_IP}'/>
    </dhcp>
  </ip>
</network>
EOF
}

write_management_network_xml() {
    cat >"${LAB_DIR}/management-network.xml" <<EOF
<network>
  <name>${MANAGEMENT_NETWORK_NAME}</name>
  <bridge name='${MANAGEMENT_BRIDGE}' stp='on' delay='0'/>
</network>
EOF
}

create_networks() {
    ensure_lab_dirs

    write_external_network_xml
    write_management_network_xml

    destroy_network_if_exists "${EXTERNAL_NETWORK_NAME}"
    destroy_network_if_exists "${MANAGEMENT_NETWORK_NAME}"
    destroy_bridge_if_exists "${EXTERNAL_BRIDGE}"
    destroy_bridge_if_exists "${MANAGEMENT_BRIDGE}"

    as_root virsh net-define "${LAB_DIR}/external-network.xml"
    start_network_with_recovery "${EXTERNAL_NETWORK_NAME}" "${EXTERNAL_BRIDGE}"

    as_root virsh net-define "${LAB_DIR}/management-network.xml"
    start_network_with_recovery "${MANAGEMENT_NETWORK_NAME}" "${MANAGEMENT_BRIDGE}"
}

create_headnode_disk() {
    ensure_lab_dirs
    as_root rm -f "${HEADNODE_DISK}" "${HEADNODE_SEED_ISO}"
    rm -rf "${LAB_DIR}/${HEADNODE_NAME}-seed"
    mkdir -p "${LAB_DIR}/${HEADNODE_NAME}-seed"
    as_root qemu-img create -q -f qcow2 -F qcow2 -b "${BASE_IMAGE}" "${HEADNODE_DISK}" "${HEADNODE_DISK_GB}G"
    render_headnode_cloud_init
    cp "$(headnode_user_data_path)" "${LAB_DIR}/${HEADNODE_NAME}-seed/user-data"
    cp "$(headnode_meta_data_path)" "${LAB_DIR}/${HEADNODE_NAME}-seed/meta-data"
    cp "$(headnode_network_config_path)" "${LAB_DIR}/${HEADNODE_NAME}-seed/network-config"
    as_root bash -lc "cd ${LAB_DIR@Q}/${HEADNODE_NAME@Q}-seed && mkisofs -quiet -output ${HEADNODE_SEED_ISO@Q} -volid cidata -joliet -rock user-data meta-data network-config"
    restore_image_labels
}

create_headnode() {
    destroy_domain_if_exists "${HEADNODE_NAME}"
    create_headnode_disk

    as_root virt-install \
        --name "${HEADNODE_NAME}" \
        --memory "${HEADNODE_MEMORY_MB}" \
        --vcpus "${HEADNODE_VCPUS}" \
        --cpu host-passthrough \
        --machine q35 \
        --osinfo "detect=on,require=off,name=$(virt_install_osinfo_name)" \
        --import \
        --disk "path=${HEADNODE_DISK},format=qcow2,bus=virtio" \
        --disk "path=${HEADNODE_SEED_ISO},device=cdrom" \
        --network "network=${EXTERNAL_NETWORK_NAME},model=virtio,mac=${HEADNODE_EXT_MAC}" \
        --network "network=${MANAGEMENT_NETWORK_NAME},model=virtio,mac=${HEADNODE_MGMT_MAC}" \
        --graphics vnc,listen=127.0.0.1 \
        --console pty,target_type=serial \
        --noautoconsole
}

write_compute_domain_xml() {
    local index=$1
    local domain
    local xml_path

    domain=$(node_domain_name "${index}")
    xml_path="${LAB_DIR}/${domain}.xml"

    cat >"${xml_path}" <<EOF
<domain type='kvm'>
  <name>${domain}</name>
  <memory unit='MiB'>${COMPUTE_MEMORY_MB}</memory>
  <currentMemory unit='MiB'>${COMPUTE_MEMORY_MB}</currentMemory>
  <vcpu placement='static'>${COMPUTE_VCPUS}</vcpu>
  <os>
    <type arch='x86_64'>hvm</type>
    <boot dev='network'/>
  </os>
  <features>
    <acpi/>
    <apic/>
  </features>
  <cpu mode='host-passthrough'>
    <topology sockets='${NODE_SOCKETS}' dies='1' cores='${NODE_CORES_PER_SOCKET}' threads='${NODE_THREADS_PER_CORE}'/>
  </cpu>
  <clock offset='utc'/>
  <on_poweroff>destroy</on_poweroff>
  <on_reboot>restart</on_reboot>
  <on_crash>restart</on_crash>
  <devices>
    <interface type='network'>
      <mac address='$(node_mac "${index}")'/>
      <source network='${MANAGEMENT_NETWORK_NAME}'/>
      <model type='virtio'/>
    </interface>
    <rng model='virtio'>
      <backend model='random'>/dev/urandom</backend>
    </rng>
    <serial type='pty'>
      <target port='0'/>
    </serial>
    <console type='pty'>
      <target type='serial' port='0'/>
    </console>
  </devices>
</domain>
EOF
}

define_compute_nodes() {
    local index
    local domain

    for (( index = 1; index <= COMPUTE_COUNT; index++ )); do
        domain=$(node_domain_name "${index}")
        destroy_domain_if_exists "${domain}"
        write_compute_domain_xml "${index}"
        as_root virsh define "${LAB_DIR}/${domain}.xml"
    done
}

wait_for_headnode_ssh() {
    local deadline=$((SECONDS + 600))

    while (( SECONDS < deadline )); do
        if ssh "${SSH_OPTIONS[@]}" "$(remote_host)" 'true' >/dev/null 2>&1; then
            return 0
        fi
        sleep 5
    done

    die "Timed out waiting for SSH on ${EXTERNAL_HEADNODE_IP}"
}

remote_running_kernel() {
    ssh_remote "uname -r" | tr -d '\r'
}

remote_available_kernel() {
    ssh_remote "sudo dnf -q repoquery kernel --available --qf '%{VERSION}-%{RELEASE}.%{ARCH}' 2>/dev/null | sort -V | tail -1" \
        | tr -d '\r'
}

prepare_headnode() {
    local running_kernel
    local available_kernel

    wait_for_headnode_ssh

    log "Checking headnode repository state"
    ssh_remote "if ! sudo find /etc/yum.repos.d -maxdepth 1 -name '*.repo' | grep -q .; then
        if sudo test -d '${REMOTE_INSTALL_ROOT}/backup/etc/yum.repos.d' &&
            sudo find '${REMOTE_INSTALL_ROOT}/backup/etc/yum.repos.d' -maxdepth 1 -name '*.repo' | grep -q .; then
            sudo mkdir -p /etc/yum.repos.d &&
                sudo rsync -a --delete '${REMOTE_INSTALL_ROOT}/backup/etc/yum.repos.d/' /etc/yum.repos.d/
        else
            echo 'No enabled repositories and no backup repo set to restore' >&2
            exit 1
        fi
    fi"

    log "Ensuring headnode runtime prerequisites are installed"
    ssh_remote "sudo systemctl stop packagekit packagekit-offline-update >/dev/null 2>&1 || true;
        sudo dnf clean all >/dev/null 2>&1 || true;
        sudo rm -rf /var/cache/dnf/*;
        sudo dnf makecache --refresh &&
        sudo dnf install --refresh --setopt=keepcache=0 -y dnf-plugins-core $(headnode_glibmm_package) newt qemu-guest-agent rsync tar wget"
    ssh_remote "sudo systemctl enable --now qemu-guest-agent" >/dev/null 2>&1 || true

    running_kernel=$(remote_running_kernel)
    available_kernel=$(remote_available_kernel)

    [[ -n "${available_kernel}" ]] || die "Failed to detect the latest kernel available on the headnode"

    if [[ "${running_kernel}" != "${available_kernel}" ]]; then
        log "Updating headnode kernel from ${running_kernel} to ${available_kernel}"
        ssh_remote "sudo dnf install -y kernel"
        ssh_remote "sudo reboot" >/dev/null 2>&1 || true
        sleep 10
        wait_for_headnode_ssh

        running_kernel=$(remote_running_kernel)
        [[ "${running_kernel}" == "${available_kernel}" ]] || die \
            "Headnode reboot completed but the running kernel is still ${running_kernel}, expected ${available_kernel}"
    fi
}

render_answerfile() {
    local template
    local workfile
    local index

    template=$(answerfile_template_path)
    workfile="${ANSWERFILE_PATH}"
    mkdir -p "${LAB_DIR}"

    [[ -f "${template}" ]] || die \
        "Answerfile template not found for provisioner ${PROVISIONER}: ${template}"

    sed \
        -e "s|__CLUSTER_NAME__|${CLUSTER_NAME}|g" \
        -e "s|__COMPANY_NAME__|${COMPANY_NAME}|g" \
        -e "s|__ADMIN_EMAIL__|${ADMIN_EMAIL}|g" \
        -e "s|__TIMEZONE__|${TIMEZONE}|g" \
        -e "s|__TIMESERVER__|${TIMESERVER}|g" \
        -e "s|__LOCALE__|${LOCALE}|g" \
        -e "s|__CLUSTER_HOSTNAME__|${CLUSTER_HOSTNAME}|g" \
        -e "s|__CLUSTER_DOMAIN__|${CLUSTER_DOMAIN}|g" \
        -e "s|__EXTERNAL_IFACE__|oc-ext0|g" \
        -e "s|__EXTERNAL_DOMAIN__|${EXTERNAL_DOMAIN}|g" \
        -e "s|__MANAGEMENT_IFACE__|oc-mgmt0|g" \
        -e "s|__HEADNODE_MGMT_IP__|${MANAGEMENT_GATEWAY_IP}|g" \
        -e "s|__MANAGEMENT_NETMASK__|${MANAGEMENT_NETMASK}|g" \
        -e "s|__MANAGEMENT_DOMAIN__|${MANAGEMENT_DOMAIN}|g" \
        -e "s|__REMOTE_ISO_PATH__|${REMOTE_ISO_PATH}|g" \
        -e "s|__DISTRO_ID__|${DISTRO_ID}|g" \
        -e "s|__DISTRO_VERSION__|${DISTRO_VERSION}|g" \
        -e "s|__PROVISIONER__|${PROVISIONER}|g" \
        -e "s|__PARTITION_NAME__|${PARTITION_NAME}|g" \
        -e "s|__MARIADB_ROOT_PASSWORD__|${MARIADB_ROOT_PASSWORD}|g" \
        -e "s|__SLURMDB_PASSWORD__|${SLURMDB_PASSWORD}|g" \
        -e "s|__STORAGE_PASSWORD__|${STORAGE_PASSWORD}|g" \
        -e "s|__NODE_PREFIX__|${NODE_PREFIX}|g" \
        -e "s|__NODE_PADDING__|${NODE_PADDING}|g" \
        -e "s|__NODE_IP_START__|$(node_ip 1)|g" \
        -e "s|__NODE_ROOT_PASSWORD__|${NODE_ROOT_PASSWORD}|g" \
        -e "s|__NODE_SOCKETS__|${NODE_SOCKETS}|g" \
        -e "s|__NODE_CPUS_PER_NODE__|${NODE_CPUS_PER_NODE}|g" \
        -e "s|__NODE_CORES_PER_SOCKET__|${NODE_CORES_PER_SOCKET}|g" \
        -e "s|__NODE_THREADS_PER_CORE__|${NODE_THREADS_PER_CORE}|g" \
        -e "s|__NODE_REAL_MEMORY_MB__|${NODE_REAL_MEMORY_MB}|g" \
        -e "s|__NODE_BMC_USERNAME__|${NODE_BMC_USERNAME}|g" \
        -e "s|__NODE_BMC_PASSWORD__|${NODE_BMC_PASSWORD}|g" \
        -e "s|__NODE_BMC_SERIALPORT__|${NODE_BMC_SERIALPORT}|g" \
        -e "s|__NODE_BMC_SERIALSPEED__|${NODE_BMC_SERIALSPEED}|g" \
        "${template}" >"${workfile}"

    for (( index = 1; index <= COMPUTE_COUNT; index++ )); do
        cat >>"${workfile}" <<EOF

[node.${index}]
hostname=$(node_name "${index}")
mac_address=$(node_mac "${index}")
node_ip=$(node_ip "${index}")
bmc_address=$(node_bmc_ip "${index}")
EOF
    done
}

ssh_remote() {
    ssh "${SSH_OPTIONS[@]}" "$(remote_host)" "$@"
}

scp_to_remote() {
    scp "${SSH_OPTIONS[@]}" "$1" "$(remote_host):$2"
}

probe_remote_binary() {
    local probe_log="${LOG_DIR}/binary-probe.log"

    mkdir -p "${LOG_DIR}"
    ssh_remote "ldd '${REMOTE_BINARY_PATH}'" >"${probe_log}" 2>&1
}

sync_to_remote() {
    rsync -a --info=progress2 -e "ssh ${SSH_OPTIONS[*]}" "$1" "$(remote_host):$2"
}

sync_repo_to_remote() {
    ssh_remote "mkdir -p '${REMOTE_SOURCE_DIR}'"

    rsync -a --delete \
        --exclude .git \
        --exclude .codex \
        --exclude build \
        --exclude out \
        --exclude compile_commands.json \
        --exclude '*.qcow2' \
        --exclude '*.iso' \
        -e "ssh ${SSH_OPTIONS[*]}" \
        "${OPENCATTUS_SOURCE_DIR}/" \
        "$(remote_host):${REMOTE_SOURCE_DIR}/"
}

build_binary_in_guest() {
    local build_log="${LOG_DIR}/build.log"
    local compiler_setup_cmd
    local remote_cmd

    sync_repo_to_remote

    if (( DISTRO_MAJOR >= 10 )); then
        compiler_setup_cmd=$(cat <<'EOF'
chmod +x setupDevEnvironment.sh &&
./setupDevEnvironment.sh &&
EOF
)
    else
        compiler_setup_cmd=$(cat <<'EOF'
chmod +x setupDevEnvironment.sh rhel-gcc-toolset-14.sh &&
./setupDevEnvironment.sh &&
set +u &&
source ./rhel-gcc-toolset-14.sh &&
set -u &&
EOF
)
    fi

    remote_cmd=$(cat <<EOF
export PATH="\$HOME/.local/bin:\$PATH" &&
cd '${REMOTE_SOURCE_DIR}' &&
${compiler_setup_cmd}
conan profile detect --force &&
cmake --preset '${REMOTE_BUILD_PRESET}' &&
cmake --build --preset '${REMOTE_BUILD_PRESET_BUILD}' --target opencattus -j '${REMOTE_BUILD_JOBS}' &&
install -m 0755 '${REMOTE_BUILD_BINARY}' '${REMOTE_BINARY_PATH}'
EOF
)

    timeout "${BUILD_TIMEOUT_SECONDS}" \
        ssh "${SSH_OPTIONS[@]}" "$(remote_host)" "bash -lc ${remote_cmd@Q}" 2>&1 \
        | tee "${build_log}"
}

prepare_opencattus_binary() {
    ssh_remote "mkdir -p '${HEADNODE_DATA_DIR}'"

    if ssh_remote "test -x '${REMOTE_BINARY_PATH}'" >/dev/null 2>&1; then
        if probe_remote_binary; then
            log "Reusing existing OpenCATTUS binary on the headnode"
            return
        fi
    fi

    if [[ -n "${OPENCATTUS_BINARY}" ]]; then
        scp_to_remote "${OPENCATTUS_BINARY}" "${REMOTE_BINARY_PATH}"
        ssh_remote "chmod +x '${REMOTE_BINARY_PATH}'"
        if probe_remote_binary; then
            return
        fi

        log "Provided binary is not usable on the ${DISTRO_ID}${DISTRO_MAJOR} headnode; falling back to in-guest build"
    fi

    build_binary_in_guest
}

copy_lab_assets() {
    ssh_remote "mkdir -p '${HEADNODE_DATA_DIR}' '${REMOTE_REPO_CONFIG_STAGING}'"

    sync_to_remote "${CLUSTER_ISO}" "${REMOTE_ISO_PATH}"
    scp_to_remote "${ANSWERFILE_PATH}" "${REMOTE_ANSWERFILE_PATH}"
    scp_to_remote "${SCRIPT_DIR}/scripts/check-headnode.sh" "${REMOTE_CHECK_HEADNODE}"
    scp_to_remote "${SCRIPT_DIR}/scripts/check-cluster.sh" "${REMOTE_CHECK_CLUSTER}"
    scp_to_remote "${SCRIPT_DIR}/scripts/check-mpi.sh" "${REMOTE_CHECK_MPI}"
    sync_to_remote "${LOCAL_REPO_CONFIG_DIR}/" "${REMOTE_REPO_CONFIG_STAGING}/"

    ssh_remote "chmod +x '${REMOTE_CHECK_HEADNODE}' '${REMOTE_CHECK_CLUSTER}' '${REMOTE_CHECK_MPI}' &&
        sudo mkdir -p '${REMOTE_INSTALL_REPO_DIR}' &&
        sudo rsync -a --delete '${REMOTE_REPO_CONFIG_STAGING}/' '${REMOTE_INSTALL_REPO_DIR}/'"
}

run_installer() {
    local install_log="${LOG_DIR}/install.log"
    local remote_cmd

    remote_cmd=$(cat <<EOF
cd '${HEADNODE_DATA_DIR}' &&
sudo '${REMOTE_BINARY_PATH}' -l 6 -a '${REMOTE_ANSWERFILE_PATH}' -u
EOF
)

    timeout "${INSTALL_TIMEOUT_SECONDS}" \
        ssh "${SSH_OPTIONS[@]}" "$(remote_host)" "bash -lc ${remote_cmd@Q}" 2>&1 \
        | tee "${install_log}"
}

restart_compute_nodes() {
    local index
    local domain

    for (( index = 1; index <= COMPUTE_COUNT; index++ )); do
        domain=$(node_domain_name "${index}")
        if as_root virsh domstate "${domain}" 2>/dev/null | grep -q '^running$'; then
            as_root virsh destroy "${domain}" >/dev/null
        fi
        as_root virsh start "${domain}" >/dev/null
    done
}

join_by_space() {
    local first=1
    local item

    for item in "$@"; do
        if (( first )); then
            printf '%s' "${item}"
            first=0
        else
            printf ' %s' "${item}"
        fi
    done
}

verify_headnode() {
    ssh_remote \
        "sudo env OPENCATTUS_PROVISIONER='${PROVISIONER}' '${REMOTE_CHECK_HEADNODE}'"
}

verify_cluster() {
    local node_names=()
    local node_ips=()
    local index

    for (( index = 1; index <= COMPUTE_COUNT; index++ )); do
        node_names+=("$(node_name "${index}")")
        node_ips+=("$(node_ip "${index}")")
    done

    ssh_remote \
        "sudo env OPENCATTUS_NODE_LIST='$(join_by_space "${node_names[@]}")' OPENCATTUS_NODE_IP_LIST='$(join_by_space "${node_ips[@]}")' OPENCATTUS_VERIFY_TIMEOUT_SECONDS='${VERIFY_TIMEOUT_SECONDS}' '${REMOTE_CHECK_CLUSTER}'"
}

verify_mpi() {
    local node_names=()
    local index

    [[ "${MPI_SMOKE_TEST}" == "1" ]] || return 0

    for (( index = 1; index <= MPI_SMOKE_NODES; index++ )); do
        node_names+=("$(node_name "${index}")")
    done

    ssh_remote \
        "env OPENCATTUS_NODE_LIST='$(join_by_space "${node_names[@]}")' OPENCATTUS_MPI_SMOKE_NODES='${MPI_SMOKE_NODES}' OPENCATTUS_MPI_SMOKE_TASKS='${MPI_SMOKE_TASKS}' OPENCATTUS_MPI_SMOKE_WORKDIR='${MPI_SMOKE_WORKDIR}' OPENCATTUS_MPI_SMOKE_OUTPUT='${MPI_SMOKE_OUTPUT}' '${REMOTE_CHECK_MPI}'"
}

collect_logs() {
    local headnode_log_dir="${LOG_DIR}/headnode"
    local index
    local domain

    mkdir -p "${headnode_log_dir}"

    if [[ -f "${VBMC_LOG_FILE}" ]]; then
        cp "${VBMC_LOG_FILE}" "${LOG_DIR}/vbmcd.log"
    fi

    if [[ -f "${VBMC_CONFIG_FILE}" ]]; then
        cp "${VBMC_CONFIG_FILE}" "${LOG_DIR}/virtualbmc.conf"
    fi

    if domain_exists "${HEADNODE_NAME}"; then
        as_root virsh dumpxml "${HEADNODE_NAME}" >"${LOG_DIR}/${HEADNODE_NAME}.xml"
        as_root virsh dominfo "${HEADNODE_NAME}" >"${LOG_DIR}/${HEADNODE_NAME}.dominfo"
    fi

    for (( index = 1; index <= COMPUTE_COUNT; index++ )); do
        domain=$(node_domain_name "${index}")
        if domain_exists "${domain}"; then
            as_root virsh dumpxml "${domain}" >"${LOG_DIR}/${domain}.xml"
            as_root virsh dominfo "${domain}" >"${LOG_DIR}/${domain}.dominfo"
        fi
    done

    if network_exists "${EXTERNAL_NETWORK_NAME}"; then
        as_root virsh net-dumpxml "${EXTERNAL_NETWORK_NAME}" >"${LOG_DIR}/${EXTERNAL_NETWORK_NAME}.xml"
        as_root virsh net-dhcp-leases "${EXTERNAL_NETWORK_NAME}" >"${LOG_DIR}/${EXTERNAL_NETWORK_NAME}.leases"
    fi

    if network_exists "${MANAGEMENT_NETWORK_NAME}"; then
        as_root virsh net-dumpxml "${MANAGEMENT_NETWORK_NAME}" >"${LOG_DIR}/${MANAGEMENT_NETWORK_NAME}.xml"
    fi

    if ssh "${SSH_OPTIONS[@]}" "$(remote_host)" 'true' >/dev/null 2>&1; then
        ssh_remote "sudo journalctl -b --no-pager" >"${headnode_log_dir}/journal.txt" || true
        ssh_remote "sudo systemctl --failed --no-pager" >"${headnode_log_dir}/failed-units.txt" || true
        ssh_remote "sudo sinfo -N -h -o '%N %T'" >"${headnode_log_dir}/sinfo.txt" || true
        ssh_remote "sudo showmount -e localhost" >"${headnode_log_dir}/showmount.txt" || true
        ssh_remote "test -f '${MPI_SMOKE_OUTPUT}' && cat '${MPI_SMOKE_OUTPUT}'" >"${headnode_log_dir}/mpi-smoke.txt" || true
        ssh_remote "sudo tar -C /var/log -czf - messages secure dnf.log slurm 2>/dev/null" >"${headnode_log_dir}/var-log.tar.gz" || true
    fi
}

destroy_lab() {
    local index

    destroy_vbmc

    destroy_domain_if_exists "${HEADNODE_NAME}"

    for (( index = 1; index <= COMPUTE_COUNT; index++ )); do
        destroy_domain_if_exists "$(node_domain_name "${index}")"
    done

    destroy_network_if_exists "${EXTERNAL_NETWORK_NAME}"
    destroy_network_if_exists "${MANAGEMENT_NETWORK_NAME}"
    destroy_bridge_if_exists "${EXTERNAL_BRIDGE}"
    destroy_bridge_if_exists "${MANAGEMENT_BRIDGE}"

    as_root rm -rf "${LAB_DIR}"
    as_root rm -rf "${IMAGE_DIR}"
}

status_lab() {
    log "Domains:"
    as_root virsh list --all | grep -E "${LAB_NAME}|Name" || true
    log "Networks:"
    as_root virsh net-list --all | grep -E "${LAB_NAME}|Name" || true
}

create_lab() {
    check_host_prereqs
    check_config
    ensure_ssh_key
    create_networks
    create_headnode
    define_compute_nodes
    configure_vbmc
    wait_for_headnode_ssh
}

install_lab() {
    check_host_prereqs
    check_config
    prepare_headnode
    prepare_opencattus_binary
    render_answerfile
    copy_lab_assets
    run_installer
}

boot_lab() {
    check_host_prereqs
    restart_compute_nodes
}

verify_lab() {
    check_host_prereqs
    verify_headnode
    verify_cluster
    verify_mpi
}

run_lab() {
    local rc=0

    destroy_lab || true
    create_lab || rc=$?
    if (( rc == 0 )); then
        install_lab || rc=$?
    fi
    if (( rc == 0 )); then
        boot_lab || rc=$?
    fi
    if (( rc == 0 )); then
        verify_lab || rc=$?
    fi
    collect_logs || true
    return "${rc}"
}

parse_args() {
    while getopts ':c:h' opt; do
        case "${opt}" in
            c)
                CONFIG_FILE=${OPTARG}
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
    COMMAND=${1:-}
    [[ -n "${COMMAND}" ]] || {
        usage
        exit 1
    }
}

main() {
    parse_args "$@"

    if [[ -n "${CONFIG_FILE}" ]]; then
        [[ -f "${CONFIG_FILE}" ]] || die "Config file not found: ${CONFIG_FILE}"
        # shellcheck disable=SC1090
        source "${CONFIG_FILE}"
    fi

    load_defaults

    case "${COMMAND}" in
        create)
            create_lab
            ;;
        install)
            install_lab
            ;;
        boot)
            boot_lab
            ;;
        verify)
            verify_lab
            ;;
        collect)
            collect_logs
            ;;
        destroy)
            destroy_lab
            ;;
        status)
            status_lab
            ;;
        run)
            run_lab
            ;;
        *)
            die "Unknown command: ${COMMAND}"
            ;;
    esac
}

main "$@"
