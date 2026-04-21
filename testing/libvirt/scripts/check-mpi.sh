#!/usr/bin/env bash

set -euo pipefail

node_list=${OPENCATTUS_NODE_LIST:?OPENCATTUS_NODE_LIST is required}
mpi_nodes=${OPENCATTUS_MPI_SMOKE_NODES:?OPENCATTUS_MPI_SMOKE_NODES is required}
mpi_tasks=${OPENCATTUS_MPI_SMOKE_TASKS:?OPENCATTUS_MPI_SMOKE_TASKS is required}
mpi_workdir=${OPENCATTUS_MPI_SMOKE_WORKDIR:-${HOME}/opencattus-mpi-smoke}
mpi_output=${OPENCATTUS_MPI_SMOKE_OUTPUT:-${mpi_workdir}/mpi-hello.out}
mpi_run_id=${OPENCATTUS_MPI_SMOKE_RUN_ID:-$$-$(date +%s)}
mpi_remote_dir=${OPENCATTUS_MPI_SMOKE_REMOTE_DIR:-/tmp/opencattus-mpi-smoke-${mpi_run_id}}
mpi_remote_path=${OPENCATTUS_MPI_SMOKE_REMOTE_PATH:-${mpi_remote_dir}/mpi_hello}
mpi_remote_launcher=${OPENCATTUS_MPI_SMOKE_REMOTE_LAUNCHER:-${mpi_remote_dir}/run-mpi-hello.sh}

read -r -a nodes <<<"${node_list}"
selected_nodes=("${nodes[@]:0:mpi_nodes}")
nodelist_csv=$(IFS=,; printf '%s' "${selected_nodes[*]}")

if [[ ${#nodes[@]} -eq 0 ]]; then
    echo "No compute nodes were provided for the MPI smoke test" >&2
    exit 1
fi

if (( mpi_nodes <= 0 )); then
    echo "OPENCATTUS_MPI_SMOKE_NODES must be greater than zero" >&2
    exit 1
fi

if (( mpi_tasks <= 0 )); then
    echo "OPENCATTUS_MPI_SMOKE_TASKS must be greater than zero" >&2
    exit 1
fi

if (( mpi_nodes > ${#nodes[@]} )); then
    echo "MPI smoke test requested ${mpi_nodes} nodes, but only ${#nodes[@]} were provided" >&2
    exit 1
fi

if (( mpi_tasks < mpi_nodes )); then
    echo "MPI smoke test tasks (${mpi_tasks}) must be at least the node count (${mpi_nodes})" >&2
    exit 1
fi

mkdir -p "${mpi_workdir}"

init_module_command() {
    if [[ -f /etc/profile.d/lmod.sh ]]; then
        # Lmod from OpenHPC or distro packages.
        # shellcheck disable=SC1091
        source /etc/profile.d/lmod.sh
        return 0
    fi

    if [[ -f /etc/profile.d/modules.sh ]]; then
        # Fallback for environments that still provide environment-modules.
        # shellcheck disable=SC1091
        source /etc/profile.d/modules.sh
        return 0
    fi

    echo "Could not find a shell init script for the module command" >&2
    exit 1
}

first_module_match() {
    local prefix=$1

    module -t avail "${prefix}" 2>&1 | awk -v prefix="${prefix}" '
        /:$/ { next }
        NF == 0 { next }
        $0 ~ ("^" prefix "(/|$)") { print; exit }
    '
}

resolve_mpi_modules() {
    local compiler_prefix
    local mpi_prefix
    local compiler_module
    local mpi_module

    for compiler_prefix in gnu15 gnu14 gnu13 gnu12 gnu9; do
        compiler_module=$(first_module_match "${compiler_prefix}") || true
        if [[ -z "${compiler_module}" ]]; then
            continue
        fi

        if ! module load "${compiler_module}" >/dev/null 2>&1; then
            module purge >/dev/null 2>&1 || true
            continue
        fi

        for mpi_prefix in openmpi5-pmix openmpi5 openmpi4; do
            mpi_module=$(first_module_match "${mpi_prefix}") || true
            if [[ -z "${mpi_module}" ]]; then
                continue
            fi

            module purge >/dev/null 2>&1 || true
            if module load "${compiler_module}" "${mpi_module}" >/dev/null 2>&1 &&
                command -v mpicc >/dev/null 2>&1; then
                printf '%s %s\n' "${compiler_module}" "${mpi_module}"
                return 0
            fi
        done

        module purge >/dev/null 2>&1 || true
    done

    echo "Could not find a supported OpenHPC compiler/MPI module pair" >&2
    exit 1
}

encode_base64() {
    local path=$1

    if base64 --help 2>&1 | grep -q -- ' -w,'; then
        base64 -w 0 "${path}"
    else
        base64 "${path}" | tr -d '\n'
    fi
}

set +u
init_module_command
module purge >/dev/null 2>&1 || true
read -r -a mpi_modules <<<"$(resolve_mpi_modules)"
module load "${mpi_modules[@]}"
set -u

cat >"${mpi_workdir}/mpi_hello.c" <<'EOF'
#include <mpi.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    int rank, size;
    char hostname[256];

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    gethostname(hostname, sizeof(hostname));
    printf("Hello from rank %d of %d on %s\n", rank, size, hostname);
    MPI_Finalize();
    return 0;
}
EOF

mpicc "${mpi_workdir}/mpi_hello.c" -o "${mpi_workdir}/mpi_hello"
mpi_binary_base64=$(encode_base64 "${mpi_workdir}/mpi_hello")

{
    echo '#!/usr/bin/env bash'
    echo 'set -euo pipefail'
    echo 'if [[ -f /opt/ohpc/admin/lmod/lmod/init/bash ]]; then'
    echo '    export LMOD_SETTARG_CMD=":"'
    echo '    export LMOD_FULL_SETTARG_SUPPORT=no'
    echo '    export LMOD_COLORIZE=no'
    echo '    export LMOD_PREPEND_BLOCK=normal'
    echo '    export MODULEPATH=/opt/ohpc/pub/modulefiles'
    echo '    . /opt/ohpc/admin/lmod/lmod/init/bash >/dev/null'
    echo 'elif [[ -f /etc/profile.d/lmod.sh ]]; then'
    echo '    source /etc/profile.d/lmod.sh'
    echo 'elif [[ -f /etc/profile.d/modules.sh ]]; then'
    echo '    source /etc/profile.d/modules.sh'
    echo 'else'
    echo '    echo "Could not find a shell init script for the module command" >&2'
    echo '    exit 1'
    echo 'fi'
    echo 'module purge >/dev/null 2>&1 || true'
    printf 'module load'
    for mpi_module in "${mpi_modules[@]}"; do
        printf ' %q' "${mpi_module}"
    done
    echo ' >/dev/null 2>&1'
    printf 'exec %q\n' "${mpi_remote_path}"
} >"${mpi_workdir}/run-mpi-hello.sh"

mpi_launcher_base64=$(encode_base64 "${mpi_workdir}/run-mpi-hello.sh")

srun_args=(
    --mpi=pmix
    --chdir=/tmp
    --nodelist="${nodelist_csv}"
    --nodes="${mpi_nodes}"
    --ntasks="${mpi_tasks}"
)

if (( mpi_tasks == mpi_nodes && mpi_nodes > 1 )); then
    srun_args+=(--ntasks-per-node=1)
fi

srun \
    --chdir=/tmp \
    --nodelist="${nodelist_csv}" \
    --nodes="${mpi_nodes}" \
    --ntasks="${mpi_nodes}" \
    --ntasks-per-node=1 \
    mkdir -p "${mpi_remote_dir}"

srun \
    --chdir=/tmp \
    --nodelist="${nodelist_csv}" \
    --nodes="${mpi_nodes}" \
    --ntasks="${mpi_nodes}" \
    --ntasks-per-node=1 \
    bash -lc "cat <<'EOF' | base64 -d > '${mpi_remote_path}'
${mpi_binary_base64}
EOF
cat <<'EOF' | base64 -d > '${mpi_remote_launcher}'
${mpi_launcher_base64}
EOF
chmod 0755 '${mpi_remote_path}'"

srun \
    --chdir=/tmp \
    --nodelist="${nodelist_csv}" \
    --nodes="${mpi_nodes}" \
    --ntasks="${mpi_nodes}" \
    --ntasks-per-node=1 \
    chmod 0755 "${mpi_remote_path}" "${mpi_remote_launcher}"

srun "${srun_args[@]}" "${mpi_remote_launcher}" | tee "${mpi_output}"

hello_lines=$(grep -c '^Hello from rank ' "${mpi_output}" || true)
if (( hello_lines != mpi_tasks )); then
    echo "Expected ${mpi_tasks} MPI hello lines, got ${hello_lines}" >&2
    exit 1
fi

for (( index = 0; index < mpi_nodes; index++ )); do
    node=${nodes[$index]}
    if ! grep -Eq " on ${node}(\\.|$)" "${mpi_output}"; then
        echo "MPI smoke test did not report output from ${node}" >&2
        exit 1
    fi
done
