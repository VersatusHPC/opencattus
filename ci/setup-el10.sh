#!/usr/bin/env bash
set -euxo pipefail

dnf install -y epel-release dnf-plugins-core
/usr/bin/crb enable

dnf install -y \
    git rpmdevtools rpm-build tar gzip \
    make cmake ninja-build newt-devel systemd-devel \
    python3 python3-pip \
    autoconf automake libtool meson perl-core \
    gcc-c++ \
    glibmm2.4-devel
