#!/usr/bin/env bash

set -Eeuo pipefail

binary=${1:?missing OpenCATTUS test binary path}
tmpdir=$(mktemp -d)
input_path="${tmpdir}/input.answerfile.ini"
output_path="${tmpdir}/dumped.answerfile.ini"
disk_image_path="${tmpdir}/cluster.iso"

cleanup() {
    rm -rf "${tmpdir}"
}
trap cleanup EXIT

: > "${disk_image_path}"

default_iface=$(ip -o -4 route show to default | awk '{print $5; exit}')
if [[ -z "${default_iface}" ]]; then
    default_iface=$(ip -o -4 addr show scope global | awk '{print $2; exit}')
fi
[[ -n "${default_iface}" ]]

read -r default_ip default_prefix < <(ip -o -4 addr show dev "${default_iface}" scope global \
    | awk '{split($4, addr, "/"); print addr[1], addr[2]; exit}')
[[ -n "${default_ip}" ]]
[[ -n "${default_prefix}" ]]

prefix_to_netmask() {
    local prefix=${1}
    local mask=""
    local octet
    local bits

    for ((octet = 0; octet < 4; ++octet)); do
        if (( prefix >= 8 )); then
            bits=255
            prefix=$((prefix - 8))
        elif (( prefix > 0 )); then
            bits=$((256 - 2 ** (8 - prefix)))
            prefix=0
        else
            bits=0
        fi

        if [[ -n "${mask}" ]]; then
            mask+="."
        fi
        mask+="${bits}"
    done

    printf '%s\n' "${mask}"
}

default_netmask=$(prefix_to_netmask "${default_prefix}")

cat > "${input_path}" <<EOF
[information]
cluster_name=opencattus
company_name=opencattus-enterprises
administrator_email=foo@example.com

[time]
timezone=America/Sao_Paulo
timeserver=0.br.pool.ntp.org
locale=en_US.utf8

[hostname]
hostname=opencattus
domain_name=cluster.example.com

[network_external]
interface=${default_iface}
ip_address=${default_ip}
subnet_mask=${default_netmask}
domain_name=cluster.external.example.com

[slurm]
mariadb_root_password=pwd
slurmdb_password=pwd
storage_password=pwd
partition_name=normal

[network_management]
interface=${default_iface}
ip_address=172.26.30.10
subnet_mask=255.255.255.0
domain_name=cluster.management.example.com

[system]
disk_image=${disk_image_path}
distro=rocky
version=9.3
provisioner=xcat

[node]
prefix=n
padding=2
node_ip=172.26.30.11
node_root_password=pwd
sockets=1
cpus_per_node=1
cores_per_socket=1
threads_per_core=1
real_memory=4096
bmc_username=admin
bmc_password=admin
bmc_serialport=0
bmc_serialspeed=9600
EOF

"${binary}" -a "${input_path}" \
    --dump-answerfile "${output_path}"

[[ -s "${output_path}" ]]
grep -Fqx "[system]" "${output_path}"
grep -Fq "provisioner=xcat" "${output_path}"
grep -Fq "administrator_email=foo@example.com" "${output_path}"
! grep -Fq "admm_keyfilestrator_email" "${output_path}"
