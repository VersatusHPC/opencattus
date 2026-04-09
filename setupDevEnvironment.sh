#!/bin/sh
#
# Copyright 2022 Vinícius Ferrão <vinicius@ferrao.net.br>
# SPDX-License-Identifier: Apache-2.0
#
# This file is part of OpenCATTUS
# https://github.com/versatushpc/opencattus
#
# Script to ease development environment setup on Enterprise Linux (EL) systems

# Stop execution in case of any error (add x for debugging)
set -e

# Check if it's not running as root
if [ "$(id -u)" -ne 0 ]; then
  # Check if sudo is available
  if command -v sudo >/dev/null; then
    # Override dnf to use sudo
    dnf() {
      sudo /usr/bin/dnf "$@"
    }
  else
    echo \
      "sudo is required but not installed. Please install sudo or run as root."
    exit 1
  fi
fi

# Grab EL version from RPM
os_version=$(rpm -E %rhel)

# Helper functions
add_epel() {
  if [ -n "${OPENCATTUS_MIRROR_URL:-}" ]; then
    dnf -y install \
      "${OPENCATTUS_MIRROR_URL%/}/epel/epel-release-latest-${os_version}.noarch.rpm"
  else
    dnf -y install \
      "https://dl.fedoraproject.org/pub/epel/epel-release-latest-${os_version}.noarch.rpm"
  fi
}

repo_url_exists() {
  if command -v curl >/dev/null 2>&1; then
    curl -fsSLI "$1" >/dev/null 2>&1
  elif command -v wget >/dev/null 2>&1; then
    wget -q --spider "$1"
  else
    return 1
  fi
}

install_repo_file() {
  src="$1"
  dest="$2"

  if [ "$(id -u)" -eq 0 ]; then
    install -m 0644 "$src" "$dest"
  else
    sudo install -m 0644 "$src" "$dest"
  fi
}

configure_redhat_local_mirror() {
  mirror_url=${OPENCATTUS_MIRROR_URL%/}
  repo_file=/etc/yum.repos.d/opencattus-rhel-local.repo
  tmpfile=$(mktemp)

  cat >"$tmpfile" <<EOF
[opencattus-rhel-baseos]
name=OpenCATTUS local RHEL ${os_version} BaseOS
baseurl=${mirror_url}/rhel/rhel-${os_version}-for-\$basearch-baseos-rpms/
enabled=1
gpgcheck=1
repo_gpgcheck=0
gpgkey=file:///etc/pki/rpm-gpg/RPM-GPG-KEY-redhat-release

[opencattus-rhel-appstream]
name=OpenCATTUS local RHEL ${os_version} AppStream
baseurl=${mirror_url}/rhel/rhel-${os_version}-for-\$basearch-appstream-rpms/
enabled=1
gpgcheck=1
repo_gpgcheck=0
gpgkey=file:///etc/pki/rpm-gpg/RPM-GPG-KEY-redhat-release
EOF

  codeready_url="${mirror_url}/rhel/codeready-builder-for-rhel-${os_version}-x86_64-rpms/repodata/repomd.xml"
  if repo_url_exists "$codeready_url"; then
    cat >>"$tmpfile" <<EOF

[opencattus-rhel-codeready-builder]
name=OpenCATTUS local RHEL ${os_version} CodeReady Builder
baseurl=${mirror_url}/rhel/codeready-builder-for-rhel-${os_version}-\$basearch-rpms/
enabled=1
gpgcheck=1
repo_gpgcheck=0
gpgkey=file:///etc/pki/rpm-gpg/RPM-GPG-KEY-redhat-release
EOF
  else
    echo "Local mirror does not expose CodeReady Builder for RHEL ${os_version}; continuing without it."
  fi

  install_repo_file "$tmpfile" "$repo_file"
  rm -f "$tmpfile"
}

python_bootstrap_bin() {
  case "$os_version" in
    8)
      printf '%s\n' "/usr/bin/python3.12"
      ;;
    9|10)
      if command -v python3 >/dev/null 2>&1; then
        command -v python3
      else
        command -v python
      fi
      ;;
    *)
      command -v python3
      ;;
  esac
}

install_conan_user() {
  python_bin=$(python_bootstrap_bin)

  if [ ! -x "$python_bin" ]; then
    echo "Unable to locate a supported Python interpreter at $python_bin"
    exit 3
  fi

  case "$os_version" in
    8)
      "$python_bin" -m ensurepip --upgrade
      mkdir -p "$HOME/.local/bin"
      ln -sf "$python_bin" "$HOME/.local/bin/python3"
      ;;
  esac

  "$python_bin" -m pip install --user --upgrade pip
  "$python_bin" -m pip install --user conan
}

# OS relevant settings
redhat() {
  if [ -n "${OPENCATTUS_MIRROR_URL:-}" ]; then
    configure_redhat_local_mirror
  else
    if [ "$(id -u)" -eq 0 ]; then
      subscription-manager refresh
    else
      sudo subscription-manager refresh
    fi

    dnf config-manager --set-enabled \
      "codeready-builder-for-rhel-${os_version}-x86_64-rpms"
  fi

  add_epel;
}

rocky() {
  case "$os_version" in
    8)
      repo_name="powertools"
      ;;
    9|10)
      repo_name="crb"
      ;;
  esac

  dnf config-manager --set-enabled "$repo_name"
  dnf -y install epel-release
}

almalinux() {
  case "$os_version" in
    8)
      repo_name="powertools"
      ;;
    9|10)
      repo_name="crb"
      ;;
  esac

  dnf config-manager --set-enabled "$repo_name"
  dnf -y install epel-release
}

oracle() {
  dnf config-manager --set-enabled "ol${os_version}_codeready_builder"
  add_epel;
}

#
# Entrypoint
#
echo Setting up development environment for OpenCATTUS
echo

case $(cut -f 3 -d : /etc/system-release-cpe) in
	redhat)
		redhat;
		;;
	rocky)
		rocky;
		;;
	almalinux)
		almalinux;
		;;
	oracle)
		oracle;
		;;
	*)
		echo Unable to properly identify the running OS. Aborting.
		exit 2
		;;
esac

# Build toolset, packages and utils
dnf -y install rsync git gcc-c++ gdb cmake ccache ninja-build llvm-toolset \
  lldb compiler-rt

case "$os_version" in
  8)
    dnf -y install python3.12 python3.12-pip-wheel gcc-toolset-14 \
      gcc-toolset-14-libubsan-devel gcc-toolset-14-libasan-devel cppcheck \
      glibmm24 glibmm24-devel \
      perl-Thread-Queue perl-open
    ;;
  9)
    dnf -y install python pip libasan libubsan gcc-toolset-14 \
      gcc-toolset-14-libubsan-devel gcc-toolset-14-libasan-devel cppcheck \
      glibmm24 glibmm24-devel \
      perl-File-Copy perl-File-Compare perl-Thread-Queue perl-FindBin

    ;;
  10)
    dnf -y install python pip libubsan libasan liblsan libtsan libhwasan \
      glibmm2.4 glibmm2.4-devel \
      perl-File-Copy perl-File-Compare perl-Thread-Queue perl-FindBin
    ;;
esac

# Install Conan as user
install_conan_user

# Required libraries
dnf -y install newt-devel

echo
echo Development tools, packages and libraries were installed on your system.
echo
echo If compiling or running on EL8 or EL9, please remember to source or
echo activate the environment file with the correct toolset compiler:
echo     \"source rhel-gcc-toolset-14.sh\"
echo
echo To proceed with the compilation please refer to the README.md file.
echo
