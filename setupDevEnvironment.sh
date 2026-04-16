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
  dnf -y install \
    "https://dl.fedoraproject.org/pub/epel/epel-release-latest-${os_version}.noarch.rpm"
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

install_ninja_user() {
  if command -v ninja >/dev/null 2>&1; then
    return 0
  fi

  if dnf -y install ninja-build; then
    return 0
  fi

  python_bin=$(python_bootstrap_bin)

  if [ ! -x "$python_bin" ]; then
    echo "Unable to locate a supported Python interpreter at $python_bin"
    exit 3
  fi

  echo "ninja-build is not available from the configured repositories; installing ninja via pip"
  "$python_bin" -m pip install --user --upgrade pip
  "$python_bin" -m pip install --user ninja
}

# OS relevant settings
redhat() {
  if [ "$(id -u)" -eq 0 ]; then
    if ! subscription-manager refresh; then
      echo "subscription-manager refresh failed; continuing with existing repository configuration"
    fi
  else
    if ! sudo subscription-manager refresh; then
      echo "subscription-manager refresh failed; continuing with existing repository configuration"
    fi
  fi

  if [ "$os_version" -eq 10 ]; then
    dnf config-manager --set-enabled \
      "codeready-builder-beta-for-rhel-${os_version}-x86_64-rpms" ||
      dnf config-manager --set-enabled \
        "codeready-builder-for-rhel-${os_version}-x86_64-rpms" ||
      echo "CodeReady Builder repo is not available; relying on the currently configured repositories"
  else
    dnf config-manager --set-enabled \
      "codeready-builder-for-rhel-${os_version}-x86_64-rpms" ||
      echo "CodeReady Builder repo is not available; relying on the currently configured repositories"
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
dnf -y install rsync git gcc-c++ gdb cmake ccache llvm-toolset \
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
      glibmm2.68 glibmm2.68-devel \
      perl-File-Copy perl-File-Compare perl-Thread-Queue perl-FindBin
    ;;
esac

# Install Conan as user
install_conan_user
install_ninja_user

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
