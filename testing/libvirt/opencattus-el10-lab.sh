#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

: "${DISTRO_ID:=rocky}"
: "${DISTRO_VERSION:=10.0}"
: "${DISTRO_MAJOR:=10}"
: "${PROVISIONER:=confluent}"

export DISTRO_ID
export DISTRO_VERSION
export DISTRO_MAJOR
export PROVISIONER

exec "${SCRIPT_DIR}/opencattus-el9-lab.sh" "$@"
