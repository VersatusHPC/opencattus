#!/usr/bin/env bash

set -Eeuo pipefail

build_dir=${1:-build-host}
output_dir=${2:-out/deb}
package_name=${OPENCATTUS_DEB_PACKAGE_NAME:-opencattus-installer-debug}

version=$(awk '/^Version:/ {print $2; exit}' rpmspecs/opencattus.spec)
release=$(awk '/^Release:/ {print $2; exit}' rpmspecs/opencattus.spec)
architecture=$(dpkg --print-architecture)
package_root="${output_dir}/${package_name}_${version}-${release}_${architecture}"
binary_path="${build_dir}/src/opencattus"

[[ -x "${binary_path}" ]] || {
    echo "OpenCATTUS binary not found or not executable: ${binary_path}" >&2
    exit 1
}

rm -rf "${package_root}"
install -d \
    "${package_root}/DEBIAN" \
    "${package_root}/usr/bin" \
    "${package_root}/opt/opencattus/conf/repos"

install -m 755 "${binary_path}" "${package_root}/usr/bin/opencattus"
install -m 644 repos/repos.conf "${package_root}/opt/opencattus/conf/repos/repos.conf"
install -m 644 repos/alma.conf "${package_root}/opt/opencattus/conf/repos/alma.conf"
install -m 644 repos/rhel.conf "${package_root}/opt/opencattus/conf/repos/rhel.conf"
install -m 644 repos/oracle.conf "${package_root}/opt/opencattus/conf/repos/oracle.conf"
install -m 644 repos/rocky-upstream.conf \
    "${package_root}/opt/opencattus/conf/repos/rocky-upstream.conf"
install -m 644 repos/rocky-vault.conf \
    "${package_root}/opt/opencattus/conf/repos/rocky-vault.conf"

depends=""
if command -v dpkg-shlibdeps >/dev/null 2>&1; then
    depends=$(dpkg-shlibdeps -O -e"${package_root}/usr/bin/opencattus" 2>/dev/null \
        | sed -n 's/^shlibs:Depends=//p' || true)
fi

if [[ -z "${depends}" ]]; then
    depends="libnewt0.52"
fi

installed_size=$(du -sk "${package_root}" | awk '{print $1}')
cat >"${package_root}/DEBIAN/control" <<EOF
Package: ${package_name}
Version: ${version}-${release}
Section: admin
Priority: optional
Architecture: ${architecture}
Maintainer: VersatusHPC <vinicius@ferrao.net.br>
Depends: ${depends}
Installed-Size: ${installed_size}
Homepage: https://github.com/versatushpc/opencattus
Description: OpenCATTUS Installer
 OpenCATTUS installs and configures an HPC cluster from a single
 head node.
EOF

dpkg-deb --build --root-owner-group "${package_root}" "${output_dir}"
