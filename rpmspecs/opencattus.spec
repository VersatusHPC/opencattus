Name: opencattus-installer-debug
Version: 1.0
Release: 6
Summary: OpenCATTUS Installer
License: Apache 2.0
URL: https://versatushpc.com.br/opencattus/
Source0: opencattus-%{VERSION}.tar.gz

BuildRequires: make
BuildRequires: cmake
BuildRequires: ninja-build
BuildRequires: newt-devel
BuildRequires: systemd-devel
BuildRequires: git

%if 0%{?el8} || 0%{?el9}
BuildRequires: gcc-toolset-14
BuildRequires: gcc-toolset-14-libubsan-devel
BuildRequires: gcc-toolset-14-libasan-devel
BuildRequires: glibmm24-devel
%endif

%if 0%{?el10}
BuildRequires: gcc-c++
BuildRequires: glibmm2.4-devel
%endif

Requires: newt

# Disable debug package for now
%global _enable_debug_package 0
%global debug_package %{nil}

# Conan libraries are linked from its cache; skip rpath validation
%define __brp_check_rpaths %{nil}

%description
Use OpenCATTUS installer to setup a HPC cluster from scratch.

%prep
echo "PREP: $PWD"
%autosetup -n opencattus-%{VERSION}
%if 0%{?el8} || 0%{?el9}
bash -c '
	source /opt/rh/gcc-toolset-14/enable
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -G Ninja
'
%else
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -G Ninja
%endif

%build
echo "BUILD: $PWD"
%if 0%{?el8} || 0%{?el9}
bash -c '
	source /opt/rh/gcc-toolset-14/enable
	cmake --build build
'
%else
cmake --build build
%endif

%install
echo "INSTALL: $PWD"
mkdir -p %{buildroot}/usr/bin
mkdir -p %{buildroot}/opt/opencattus/conf/repos/
install -m 755 build/src/opencattus %{buildroot}/usr/bin/opencattus
install -m 644 repos/repos.conf %{buildroot}/opt/opencattus/conf/repos/repos.conf
install -m 644 repos/alma.conf %{buildroot}/opt/opencattus/conf/repos/alma.conf
install -m 644 repos/rhel.conf %{buildroot}/opt/opencattus/conf/repos/rhel.conf
install -m 644 repos/oracle.conf %{buildroot}/opt/opencattus/conf/repos/oracle.conf
install -m 644 repos/rocky-upstream.conf %{buildroot}/opt/opencattus/conf/repos/rocky-upstream.conf
install -m 644 repos/rocky-vault.conf %{buildroot}/opt/opencattus/conf/repos/rocky-vault.conf

%files
/usr/bin/opencattus
/opt/opencattus/conf/repos/repos.conf
/opt/opencattus/conf/repos/alma.conf
/opt/opencattus/conf/repos/rhel.conf
/opt/opencattus/conf/repos/oracle.conf
/opt/opencattus/conf/repos/rocky-upstream.conf
/opt/opencattus/conf/repos/rocky-vault.conf

%changelog
* Tue Sep 16 2025  Daniel Hilst <daniel@versatushpc.com.br> - 1.0-6 - xCAT bugfixes
- Fix xCAT installation after adding Confluent
* Tue Sep 16 2025  Daniel Hilst <daniel@versatushpc.com.br> - 1.0-5 - Support Confluent
- Add support to the Lenovo HPC Confluent provisioner
- Add --roles command line
* Thu Aug 14 2025  Daniel Hilst <daniel@versatushpc.com.br> - 1.0-4 - Bugfix
- Update OFED
- Dump configuration
- Add support for Rocky Linux 9.6
* Wed Jul 16 2025  Daniel Hilst <daniel@versatushpc.com.br> - 1.0-3 - Add ansible roles
- Add ansible roles implementation
- Fix dnssec configuration generation in xCAT plugin
- Make ofed group optional in the configuration file
* Tue Jun 10 2025 Daniel Hilst <daniel@versatushpc.com.br> - 1.0-2 - Repositories revamped
- NFS fixed
- Doca OFED support added
* Tue Feb 25 2025 Daniel Hilst <daniel@versatushpc.com.br> - 1.0-1
- Initial release
