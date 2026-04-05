#!/usr/bin/env bash

set -euo pipefail

node_list=${OPENCATTUS_NODE_LIST:?OPENCATTUS_NODE_LIST is required}
mpi_nodes=${OPENCATTUS_MPI_SMOKE_NODES:?OPENCATTUS_MPI_SMOKE_NODES is required}
mpi_tasks=${OPENCATTUS_MPI_SMOKE_TASKS:?OPENCATTUS_MPI_SMOKE_TASKS is required}
mpi_workdir=${OPENCATTUS_MPI_SMOKE_WORKDIR:-${HOME}/opencattus-mpi-smoke}
mpi_output=${OPENCATTUS_MPI_SMOKE_OUTPUT:-${mpi_workdir}/mpi-hello.out}

read -r -a nodes <<<"${node_list}"

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

set +u
source /etc/profile.d/lmod.sh
module purge >/dev/null 2>&1 || true
module load gnu12 openmpi4
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

srun_args=(
    --mpi=pmix
    --nodes="${mpi_nodes}"
    --ntasks="${mpi_tasks}"
)

if (( mpi_tasks == mpi_nodes && mpi_nodes > 1 )); then
    srun_args+=(--ntasks-per-node=1)
fi

srun "${srun_args[@]}" "${mpi_workdir}/mpi_hello" | tee "${mpi_output}"

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
