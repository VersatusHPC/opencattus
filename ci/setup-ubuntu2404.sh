#!/usr/bin/env bash
set -euxo pipefail

export DEBIAN_FRONTEND=noninteractive
echo 'Acquire::ForceIPv4 "true";' > /etc/apt/apt.conf.d/99force-ipv4

apt-get update
apt-get install -y \
    ca-certificates curl git dpkg-dev \
    build-essential cmake ninja-build \
    pkg-config python3 python3-pip python3-venv \
    autoconf automake libtool meson perl \
    libglibmm-2.4-dev libnewt-dev libsystemd-dev
