#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

: "${DISTRO_ID:=ubuntu}"
: "${DISTRO_VERSION:=24.04}"
: "${DISTRO_MAJOR:=24}"
: "${PROVISIONER:=confluent}"
: "${MPI_SMOKE_NODES:=1}"
: "${MPI_SMOKE_TASKS:=2}"

export DISTRO_ID
export DISTRO_VERSION
export DISTRO_MAJOR
export PROVISIONER
export MPI_SMOKE_NODES
export MPI_SMOKE_TASKS

exec "${SCRIPT_DIR}/opencattus-el9-lab.sh" "$@"
