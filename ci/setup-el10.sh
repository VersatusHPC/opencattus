#!/usr/bin/env bash
set -euxo pipefail

dnf install -y https://dl.fedoraproject.org/pub/epel/epel-release-latest-10.noarch.rpm
sed -i 's|^metalink=|#metalink=|; s|^#baseurl=https\?://download.*/epel/|baseurl=https://mirror.local.versatushpc.com.br/epel/|' /etc/yum.repos.d/epel*.repo

dnf install -y \
    git rpmdevtools rpm-build tar gzip \
    make cmake ninja-build newt-devel systemd-devel \
    python3 python3-pip \
    autoconf automake libtool meson perl-core \
    gcc-c++ \
    glibmm2.4-devel
