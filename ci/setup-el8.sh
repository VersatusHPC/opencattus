#!/usr/bin/env bash
set -euxo pipefail

dnf install -y dnf-plugins-core epel-release
dnf config-manager --set-enabled powertools
sed -i 's|^metalink=|#metalink=|; s|^#baseurl=https\?://download.*/epel/|baseurl=http://mirror.local.versatushpc.com.br/epel/|' /etc/yum.repos.d/epel*.repo
dnf install -y python39 python39-pip
alternatives --set python3 /usr/bin/python3.9

dnf install -y \
    git rpmdevtools rpm-build tar gzip \
    make cmake ninja-build newt-devel systemd-devel \
    autoconf automake libtool meson perl-core \
    gcc-toolset-14 \
    gcc-toolset-14-libubsan-devel \
    gcc-toolset-14-libasan-devel \
    glibmm24-devel

source /opt/rh/gcc-toolset-14/enable
