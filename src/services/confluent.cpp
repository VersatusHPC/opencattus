#include <fmt/core.h>
#include <opencattus/functions.h>
#include <opencattus/models/cpu.h>
#include <opencattus/models/node.h>
#include <opencattus/models/os.h>
#include <opencattus/ofed.h>
#include <opencattus/services/confluent.h>
#include <opencattus/services/runner.h>
#include <opencattus/utils/enums.h>
#include <opencattus/utils/network.h>
#include <opencattus/utils/optional.h>
#include <opencattus/utils/singleton.h>
#include <opencattus/utils/string.h>
#include <optional>
#include <stdexcept>

#ifdef BUILD_TESTING
#include <doctest/doctest.h>
#else
#define DOCTEST_CONFIG_DISABLE
#include <doctest/doctest.h>
#endif

namespace {
using namespace opencattus;
using namespace opencattus::utils;

std::string buildFirewalldTrustedInterfaceCommands(std::string_view internalNic)
{
    return fmt::format(R"(
# Configure the internal network interfaces as trusted on FirewallD
if systemctl is-active --quiet firewalld.service; then
    firewall-cmd --zone=trusted --change-interface={internalNic} --permanent
    firewall-cmd --reload
fi
)",
        fmt::arg("internalNic", internalNic));
}

std::string buildHttpdTlsBootstrapCommands()
{
    return R"(
# Some EL9 family lanes can leave zero-length default mod_ssl assets behind.
if [ ! -s /etc/pki/tls/certs/localhost.crt ] || [ ! -s /etc/pki/tls/private/localhost.key ]; then
    rm -f /etc/pki/tls/certs/localhost.crt /etc/pki/tls/private/localhost.key
    openssl req -x509 -nodes -newkey rsa:2048 -sha256 -days 3650 \
        -subj '/CN=localhost' \
        -keyout /etc/pki/tls/private/localhost.key \
        -out /etc/pki/tls/certs/localhost.crt
    chmod 0600 /etc/pki/tls/private/localhost.key /etc/pki/tls/certs/localhost.crt
    restorecon /etc/pki/tls/certs/localhost.crt /etc/pki/tls/private/localhost.key || :
fi
)";
}

std::string buildNodeDefinitionScript(
    const models::Node& node, std::string_view image)
{
    const auto rootpwd = optional::unwrap(node.getNodeRootPassword(),
        "No root password configured for node {}", node.getHostname());

    std::string script = fmt::format(
        R"(
nodedefine {nodeName}
nodeattrib {nodeName} net.ipv4_address={nodeIp}/{nodeCIDR}
nodeattrib {nodeName} crypted.rootpassword={rootpwd} crypted.grubpassword={grubpwd}
)",
        fmt::arg("nodeName", node.getHostname()),
        fmt::arg("nodeIp",
            node.getConnection(Network::Profile::Management)
                .getAddress()
                .to_string()),
        fmt::arg("nodeCIDR",
            network::subnetMaskToCIDR(
                node.getConnection(Network::Profile::Management)
                    .getNetwork()
                    ->getSubnetMask())),
        fmt::arg("rootpwd", rootpwd), fmt::arg("grubpwd", rootpwd));

    if (const auto& bmc = node.getBMC(); bmc) {
        script += fmt::format("nodeattrib {nodeName} bmcuser={bmcuser} "
                              "bmcpass={bmcpass}\n",
            fmt::arg("nodeName", node.getHostname()),
            fmt::arg("bmcuser", bmc->getUsername()),
            fmt::arg("bmcpass", bmc->getPassword()));
    }

    if (const auto& macOpt
        = node.getConnection(Network::Profile::Management).getMAC();
        macOpt) {
        script += fmt::format("nodeattrib {nodeName} net.hwaddr={nodeMac}\n",
            fmt::arg("nodeName", node.getHostname()),
            fmt::arg("nodeMac", macOpt.value()));
    }

    script += fmt::format("nodedeploy -p {nodeName} -n {image}-diskless\n",
        fmt::arg("nodeName", node.getHostname()), fmt::arg("image", image));
    script += "confluent2hosts -a everything\n";

    return script;
}

std::string buildHeadnodeHostsRepairScript(std::string_view managementIp,
    std::string_view fqdn, std::string_view hostname)
{
    return fmt::format(
        R"(
# confluent2hosts rewrites /etc/hosts; restore the headnode mapping that
# Slurm and other local services expect to resolve through the management IP.
grep -Fqx '{managementIp}	{fqdn} {hostname}' /etc/hosts || \
    printf '{managementIp}\t{fqdn} {hostname}\n' >> /etc/hosts
)",
        fmt::arg("managementIp", managementIp), fmt::arg("fqdn", fqdn),
        fmt::arg("hostname", hostname));
}

std::string buildConfluentRepoRpmUrl(const models::OS& os)
{
    const auto arch = opencattus::utils::enums::toString(os.getArch());
    const auto relativePath = fmt::format(
        "lenovo-hpc/rpm/latest/el{releasever}/{arch}/"
        "lenovo-hpc-yum-1-1.{arch}.rpm",
        fmt::arg("releasever", os.getMajorVersion()), fmt::arg("arch", arch));
    const auto opts = opencattus::utils::singleton::options();

    if (opts->enableMirrors) {
        return fmt::format("{mirrorUrl}/{path}",
            fmt::arg("mirrorUrl", string::rstrip(opts->mirrorBaseUrl, "/")),
            fmt::arg("path", relativePath));
    }

    return fmt::format("https://hpc.lenovo.com/yum/latest/el{releasever}/"
                       "{arch}/lenovo-hpc-yum-1-1.{arch}.rpm",
        fmt::arg("releasever", os.getMajorVersion()), fmt::arg("arch", arch));
}

std::string buildConfluentBootstrapCommands(const models::OS& os)
{
    return fmt::format(R"(
# Add the Confluent repository
rpm -q lenovo-hpc-yum >/dev/null 2>&1 || rpm -Uvh {repoRpmUrl}

# Install required packages
dnf install -y lenovo-confluent tftp-server dnsmasq
{httpdTlsBootstrapCommands}
systemctl enable confluent --now
systemctl enable httpd --now
systemctl enable tftp.socket --now
systemctl enable dnsmasq --now

# Enable the Confluent environment
test -f /etc/profile.d/confluent_env.sh
set +xeu # confluent_env.sh has undefined variables
source /etc/profile.d/confluent_env.sh
set -xeu
command -v confluent2hosts >/dev/null
command -v osdeploy >/dev/null
command -v imgutil >/dev/null
)",
        fmt::arg("repoRpmUrl", buildConfluentRepoRpmUrl(os)),
        fmt::arg(
            "httpdTlsBootstrapCommands", buildHttpdTlsBootstrapCommands()));
}

std::string buildConfluentImageName(const models::OS& os)
{
    std::string distro;
    switch (os.getDistro()) {
        case models::OS::Distro::Rocky:
            distro = "rocky";
            break;
        case models::OS::Distro::AlmaLinux:
            distro = "alma";
            break;
        case models::OS::Distro::RHEL:
            distro = "rhel";
            break;
        case models::OS::Distro::OL:
            distro = "ol";
            break;
        default:
            std::unreachable();
    }

    return fmt::format("{}-{}-{}", distro, os.getVersion(),
        opencattus::utils::enums::toString(os.getArch()));
}

std::string buildSpackModuleTree(const models::OS& os)
{
    std::string distro;
    switch (os.getDistro()) {
        case models::OS::Distro::RHEL:
            distro = "rhel";
            break;
        case models::OS::Distro::AlmaLinux:
            distro = "almalinux";
            break;
        case models::OS::Distro::Rocky:
            distro = "rocky";
            break;
        case models::OS::Distro::OL:
            distro = "ol";
            break;
        default:
            std::unreachable();
    }

    return fmt::format("linux-{}{}-{}", distro, os.getMajorVersion(),
        opencattus::utils::enums::toString(os.getArch()));
}

std::string buildUserSpackModulePathExport(const models::OS& os)
{
    return fmt::format("export "
                       "MODULEPATH=/opt/spack/share/spack/lmod/{}/"
                       "Core${{MODULEPATH:+:$MODULEPATH}}",
        buildSpackModuleTree(os));
}

std::string buildNodeImageRepoFiles(const models::OS& os)
{
    switch (os.getDistro()) {
        case models::OS::Distro::Rocky:
            return "{epel,OpenHPC,rocky}.repo";
        case models::OS::Distro::AlmaLinux:
            return "{epel,OpenHPC,almalinux}.repo";
        case models::OS::Distro::RHEL:
            return "{epel,OpenHPC,rhel}.repo";
        case models::OS::Distro::OL:
            return "{epel,OpenHPC,oracle}.repo";
        default:
            std::unreachable();
    }
}

std::string buildNodeImagePackages(const models::OS& os)
{
    switch (os.getPlatform()) {
        case models::OS::Platform::el8:
            return "ohpc-base-compute ohpc-slurm-client lmod-ohpc hwloc-libs "
                   "lua-filesystem lua-posix";
        case models::OS::Platform::el9:
            return "ohpc-base-compute ohpc-slurm-client lmod-ohpc hwloc-libs";
        case models::OS::Platform::el10:
            return "ohpc-base-compute ohpc-slurm-client lmod-ohpc hwloc-libs";
        default:
            std::unreachable();
    }
}

std::string buildNodeImageInstallCommand(const models::OS& os)
{
    switch (os.getPlatform()) {
        case models::OS::Platform::el8:
            return "dnf install -y --nogpg --enablerepo=powertools "
                   "ohpc-base-compute ohpc-slurm-client lmod-ohpc hwloc-libs "
                   "lua-filesystem lua-posix";
        case models::OS::Platform::el9:
            return "dnf install -y --nogpg "
                   "ohpc-base-compute ohpc-slurm-client lmod-ohpc hwloc-libs";
        case models::OS::Platform::el10:
            return "dnf install -y --nogpg "
                   "ohpc-base-compute ohpc-slurm-client lmod-ohpc hwloc-libs";
        default:
            std::unreachable();
    }
}

std::optional<std::string> selectConfluentImageKernelVersion(
    const std::optional<OFED>& ofed,
    std::optional<std::string_view> configuredKernel,
    std::string_view runningKernel)
{
    if (configuredKernel.has_value()) {
        return std::string(configuredKernel.value());
    }

    if (!ofed.has_value() || ofed->getKind() != OFED::Kind::Doca) {
        return std::nullopt;
    }

    return std::string(runningKernel);
}

std::string buildConfluentKernelPackages(
    const models::OS& os, std::string_view kernelVersion)
{
    switch (os.getPlatform()) {
        case models::OS::Platform::el8:
            return fmt::format("kernel-{0} "
                               "kernel-core-{0} "
                               "kernel-modules-{0} "
                               "kernel-modules-extra-{0}",
                kernelVersion);
        case models::OS::Platform::el9:
        case models::OS::Platform::el10:
            return fmt::format("kernel-{0} "
                               "kernel-core-{0} "
                               "kernel-modules-{0} "
                               "kernel-modules-core-{0} "
                               "kernel-modules-extra-{0}",
                kernelVersion);
        default:
            std::unreachable();
    }
}

std::optional<std::string> buildRockyKernelImageFallbackCommands(
    const models::OS& os, std::string_view kernelVersionExpr)
{
    if (os.getDistro() != models::OS::Distro::Rocky) {
        return std::nullopt;
    }

    const auto arch = opencattus::utils::enums::toString(os.getArch());
    const auto packageBase = fmt::format(
        "https://download.rockylinux.org/pub/rocky/{}/{}/{}/os/Packages/k",
        os.getVersion(), "BaseOS", arch);

    switch (os.getPlatform()) {
        case models::OS::Platform::el8:
            return fmt::format(R"(if ! dnf install --nogpg -y \
    kernel-{kernelVersion} \
    kernel-core-{kernelVersion} \
    kernel-modules-{kernelVersion} \
    kernel-modules-extra-{kernelVersion}; then
    dnf install --nogpg -y \
        {packageBase}/kernel-{kernelVersion}.rpm \
        {packageBase}/kernel-core-{kernelVersion}.rpm \
        {packageBase}/kernel-modules-{kernelVersion}.rpm \
        {packageBase}/kernel-modules-extra-{kernelVersion}.rpm
fi
)",
                fmt::arg("kernelVersion", kernelVersionExpr),
                fmt::arg("packageBase", packageBase));
        case models::OS::Platform::el9:
        case models::OS::Platform::el10:
            return fmt::format(R"(if ! dnf install --nogpg -y \
    kernel-{kernelVersion} \
    kernel-core-{kernelVersion} \
    kernel-modules-{kernelVersion} \
    kernel-modules-core-{kernelVersion} \
    kernel-modules-extra-{kernelVersion}; then
    dnf install --nogpg -y \
        {packageBase}/kernel-{kernelVersion}.rpm \
        {packageBase}/kernel-core-{kernelVersion}.rpm \
        {packageBase}/kernel-modules-{kernelVersion}.rpm \
        {packageBase}/kernel-modules-core-{kernelVersion}.rpm \
        {packageBase}/kernel-modules-extra-{kernelVersion}.rpm
fi
)",
                fmt::arg("kernelVersion", kernelVersionExpr),
                fmt::arg("packageBase", packageBase));
        default:
            std::unreachable();
    }
}

std::optional<std::string> buildRockyKernelSupportFallbackCommands(
    const models::OS& os, std::string_view kernelVersionExpr)
{
    if (os.getDistro() != models::OS::Distro::Rocky) {
        return std::nullopt;
    }

    const auto arch = opencattus::utils::enums::toString(os.getArch());
    const auto repoComponent = [&]() -> std::string_view {
        switch (os.getPlatform()) {
            case models::OS::Platform::el8:
                return "BaseOS";
            case models::OS::Platform::el9:
            case models::OS::Platform::el10:
                return "AppStream";
            default:
                std::unreachable();
        }
    }();
    const auto packageBase = fmt::format(
        "https://download.rockylinux.org/pub/rocky/{}/{}/{}/os/Packages/k",
        os.getVersion(), repoComponent, arch);

    return fmt::format(
        R"(if ! dnf install --nogpg -y kernel-devel-{kernelVersion} kernel-headers-{kernelVersion}; then
    dnf install --nogpg -y \
        {packageBase}/kernel-devel-{kernelVersion}.rpm \
        {packageBase}/kernel-headers-{kernelVersion}.rpm
fi
)",
        fmt::arg("kernelVersion", kernelVersionExpr),
        fmt::arg("packageBase", packageBase));
}

std::string buildNodeImageOFEDCommands(const models::OS& os,
    bool applicationNetworkConfigured, const std::optional<OFED>& ofed,
    std::optional<std::string_view> kernelVersion)
{
    if (!applicationNetworkConfigured || !ofed.has_value()) {
        return "";
    }

    switch (ofed->getKind()) {
        case OFED::Kind::Inbox:
            return R"(
imgutil exec $scratchdir <<EOF
dnf group install -y --nogpg "Infiniband Support"
EOF
)";

        case OFED::Kind::Doca: {
            const auto kernelImageInstallCommands
                = buildRockyKernelImageFallbackCommands(
                    os, "${doca_image_kernel}")
                      .value_or(fmt::format(R"(
dnf install --nogpg -y \
    {kernelPackages}
)",
                          fmt::arg("kernelPackages",
                              buildConfluentKernelPackages(
                                  os, "${doca_image_kernel}"))));
            const auto kernelSupportInstallCommands
                = buildRockyKernelSupportFallbackCommands(
                    os, "${doca_image_kernel}")
                      .value_or(std::string(R"(
dnf install --nogpg -y kernel-devel-${doca_image_kernel} kernel-headers-${doca_image_kernel}
)"));
            const auto kernelSelectionCommands = kernelVersion.has_value()
                ? fmt::format(
                      R"(export doca_image_kernel={kernelVersion}
imgutil exec $scratchdir <<EOF
set -xeu -o pipefail
{kernelImageInstallCommands}
EOF
)",
                      fmt::arg("kernelVersion", kernelVersion.value()),
                      fmt::arg("kernelImageInstallCommands",
                          kernelImageInstallCommands))
                : R"(export doca_image_kernel=$(rpm --root "$scratchdir" -q kernel-core --qf '%{INSTALLTIME} %{VERSION}-%{RELEASE}.%{ARCH}\n' | sort -n | tail -1 | cut -d' ' -f2-)
)";

            return std::string(R"(
if ! test -f /etc/yum.repos.d/mlnx-doca.repo; then
    echo "Expected /etc/yum.repos.d/mlnx-doca.repo for DOCA node image staging" >&2
    exit 1
fi
)") + kernelSelectionCommands
                + R"(
if test -z "$doca_image_kernel"; then
    echo "Could not determine the Confluent node image kernel for DOCA staging" >&2
    exit 1
fi
if ! rpm --root "$scratchdir" -q )"
                + buildConfluentKernelPackages(os, "${doca_image_kernel}")
                + R"( >/dev/null 2>&1; then
    echo "Failed to stage the exact kernel payload ${doca_image_kernel} into the Confluent node image" >&2
    exit 1
fi
if ! test -f /etc/yum.repos.d/doca-kernel-${doca_image_kernel}.repo; then
)" + kernelSupportInstallCommands
                + R"(
    /opt/mellanox/doca/tools/doca-kernel-support -k "${doca_image_kernel}"
    export doca_repo_rpm=$(bash -c "find /tmp/DOCA*/ -name 'doca-kernel-repo-*.rpm' -printf '%T@ %p\n' | sort -nk1 | tail -1 | awk '{print \$2}'")
    if test -z "$doca_repo_rpm"; then
        echo "Failed to locate a generated DOCA kernel repo RPM for ${doca_image_kernel}" >&2
        exit 1
    fi
    dnf install --nogpg -y "$doca_repo_rpm"
fi
export doca_repo_folder=$(perl -lane 'print $1 if /baseurl=file:\/\/(.*)/' /etc/yum.repos.d/doca-kernel-${doca_image_kernel}.repo)
if test -z "$doca_repo_folder"; then
    echo "Could not determine the DOCA repo folder for image kernel ${doca_image_kernel}" >&2
    exit 1
fi
\cp -va /etc/yum.repos.d/mlnx-doca.repo $scratchdir/etc/yum.repos.d/
\cp -va /etc/yum.repos.d/doca-kernel-${doca_image_kernel}.repo $scratchdir/etc/yum.repos.d/
imgutil exec -v $doca_repo_folder:$doca_repo_folder $scratchdir <<EOF
set -xeu -o pipefail
)" + kernelSupportInstallCommands
                + R"(
dnf install --nogpg -y \
    --exclude=kernel \
    --exclude=kernel-core \
    --exclude=kernel-modules \
    --exclude=kernel-modules-core \
    --exclude=kernel-devel\* \
    --exclude=kernel-headers\* \
    doca-extra doca-ofed mlnx-ofa_kernel
EOF
if ! rpm --root "$scratchdir" -q doca-extra doca-ofed mlnx-ofa_kernel >/dev/null 2>&1; then
    echo "Confluent DOCA image staging did not install doca-extra, doca-ofed, and mlnx-ofa_kernel into the scratch image" >&2
    exit 1
fi
)";
        }

        case OFED::Kind::Oracle:
            throw std::logic_error("Oracle RDMA is not yet supported in the "
                                   "Confluent node image path");
    }

    std::unreachable();
}

void addNode(const models::Node& node, std::string_view image)
{
    services::runner::shell::cmd(buildNodeDefinitionScript(node, image));
}

void addNodes(std::string_view image)
{
    for (const auto& node : singleton::cluster()->getNodes()) {
        addNode(node, image);
    }
}
}

namespace opencattus::services {

void Confluent::install()
{
    using namespace utils::singleton;

    const auto configuredKernel = answerfile()->system.kernel;
    const auto nodeImageKernel
        = selectConfluentImageKernelVersion(cluster()->getOFED(),
            configuredKernel
                ? std::optional<std::string_view>(configuredKernel.value())
                : std::nullopt,
            utils::singleton::osservice()->getKernelRunning());

    if (configuredKernel.has_value()) {
        LOG_WARN("Using kernel version from the answerfile: {} at "
                 "[system].kernel in {}",
            configuredKernel.value(), answerfile()->path());
    } else if (nodeImageKernel.has_value()) {
        LOG_WARN("Using the DOCA compute-node kernel version {} for "
                 "Confluent node-image staging to keep the image aligned "
                 "with the validated running kernel",
            nodeImageKernel.value());
    }

    const auto image = buildConfluentImageName(os());

    runner::shell::fmt(R"d(
{confluentBootstrapCommands}

# Configure SELinux to allow httpd to make connections
setsebool -P httpd_can_network_connect=on

{trustedNetworkCommands}

# Add basic settings to allow a minimalist boot environment
nodegroupattrib everything \
    dns.servers={hnIp} \
    dns.domain={domain} \
    net.ipv4_gateway={hnIp} \
    deployment.useinsecureprotocols=always

# Generate a keypair for internal cluster usage
test -f ~/.ssh/id_ed25519 || ssh-keygen -f ~/.ssh/id_ed25519 -t ed25519 -N ""

# Configure the osdeploy parameters
osdeploy initialize -u -s -k -l -p -a -t -g

# Import the OS ISO file.
osdeploy import {isoPath} || :

# Create a temporary chroot to work as basis for the boot image
export scratchdir=/var/tmp/opencattus-scratchdir
rm -rf $scratchdir || :
rm -rf /var/lib/confluent/public/os/{image}-diskless || :
imgutil build -y -s {image} $scratchdir

\install -vD -m 0644 -o root  -g root  /etc/hosts                 $scratchdir/etc/hosts
\install -vD -m 0644 -o root  -g root  /etc/passwd                $scratchdir/etc/passwd
\install -vD -m 0644 -o root  -g root  /etc/group                 $scratchdir/etc/group
\install -vD -m 0000 -o root  -g root  /etc/shadow                $scratchdir/etc/shadow
\install -vD -m 0644 -o root  -g root  /etc/security/limits.conf  $scratchdir/etc/security/limits.conf

#
# Customize the image
#

# Install and configure chrony
imgutil exec $scratchdir <<EOF
dnf install -y chrony
sed -e '/^server .* iburst$/d' -i /etc/chrony.conf
echo "server {hnIp} iburst" >> /etc/chrony.conf
grep -HE '^server' /etc/chrony.conf
systemctl enable chronyd
EOF

# Install and configure nfs and autofs
imgutil exec $scratchdir <<EOF
dnf install -y autofs
echo "/opt/ohpc/pub /etc/auto.ohpc" > /etc/auto.master.d/ohpc.autofs
echo "* -nfsvers=4 {hnIp}:/opt/ohpc/pub/&" > /etc/auto.ohpc

echo "/home /etc/auto.home" > /etc/auto.master.d/home.autofs
echo "* -nfsvers=4 {hnIp}:/home/&" > /etc/auto.home
 
echo "/opt/spack /etc/auto.spack" > /etc/auto.master.d/spack.autofs
echo "* -nfsvers=4 {hnIp}:/opt/spack/&" > /etc/auto.spack

echo "/opt/intel /etc/auto.intel" > /etc/auto.master.d/intel.autofs
echo "* -nfsvers=4 {hnIp}:/opt/intel/&" > /etc/auto.intel
 
echo "/opt/nvidia /etc/auto.nvidia" > /etc/auto.master.d/nvidia.autofs
echo "* -nfsvers=4 {hnIp}:/opt/nvidia/&" > /etc/auto.nvidia

# Configure scratch area 
echo "/scratch /etc/auto.scratch" > /etc/auto.master.d/scratch.autofs
echo "local -fstype=xfs,rw :/dev/sda1" > /etc/auto.scratch

# Check that the mounts are correct
grep -H "{hnIp}" /etc/auto.{{home,ohpc,spack,intel,nvidia}} 2> /dev/null
 
systemctl enable autofs
EOF

# Slurm node configuration
\install -vD -m 0400 -o munge -g munge /etc/munge/munge.key       $scratchdir/etc/munge/munge.key
\install -vD -m 0644 -o root  -g root  /etc/slurm/slurm.conf      $scratchdir/etc/slurm/slurm.conf
# Install the GPG keys & repos
\cp -va /etc/yum.repos.d/{nodeImageRepoFiles} $scratchdir/etc/yum.repos.d/
imgutil exec $scratchdir <<EOF
set -xeu -o pipefail
{nodeImageInstallCommand}
sed -e '/^account required pam_slurm.so/d' -i /etc/pam.d/sshd
echo 'account required pam_slurm.so' >> /etc/pam.d/sshd
echo SLURMD_OPTIONS=\"--conf-server {hnIp}\" > /etc/sysconfig/slurmd
chown munge: /etc/munge/munge.key
systemctl enable munge
systemctl enable slurmd
EOF

{nodeImageOFEDCommands}

# Spack node configuration
imgutil exec $scratchdir <<EOF
cat <<'END' > /etc/profile.d/spack-env.sh
#!/bin/bash

if [ $EUID -eq 0 ] ; then
    # Root gets full Spack functionality
    source /opt/spack/share/spack/setup-env.sh
    source /opt/spack/share/spack/spack-completion.bash
else
    # Normal users get access to prebuilt Spack modules
    {spackModulePathExport}
fi

export LMOD_IGNORE_CACHE=1
END
EOF



# Pack the image from the temporary chroot and give a name
imgutil pack $scratchdir/ {image}-diskless
if test -n "${{doca_image_kernel:-}}"; then
    osdeploy updateboot {image}-diskless
    if ! file /var/lib/confluent/public/os/{image}-diskless/boot/kernel | grep -F "$doca_image_kernel" >/dev/null; then
        echo "Published Confluent boot kernel for {image}-diskless does not match ${{doca_image_kernel}}" >&2
        exit 1
    fi
fi

# Remove the leftover files from the chroot
rm -rf $scratchdir || :
)d",

        fmt::arg("domain", cluster()->getDomainName()),
        fmt::arg("confluentBootstrapCommands",
            buildConfluentBootstrapCommands(os())),
        fmt::arg("spackModulePathExport", buildUserSpackModulePathExport(os())),
        fmt::arg("nodeImageInstallCommand", buildNodeImageInstallCommand(os())),
        fmt::arg("nodeImageRepoFiles", buildNodeImageRepoFiles(os())),
        fmt::arg("hnIp",
            cluster()
                ->getHeadnode()
                .getConnection(Network::Profile::Management)
                .getAddress()
                .to_string()),
        fmt::arg("distro", os().getDistroString()),
        fmt::arg("osversion", os().getVersion()),
        fmt::arg("isoPath", answerfile()->system.disk_image.string()),
        fmt::arg("internalNic",
            utils::optional::unwrap(answerfile()->management.con_interface,
                "Internal interface not found in [network_management]")),
        fmt::arg("trustedNetworkCommands",
            buildFirewalldTrustedInterfaceCommands(
                utils::optional::unwrap(answerfile()->management.con_interface,
                    "Internal interface not found in [network_management]"))),
        fmt::arg("image", image),
        fmt::arg("nodeImageOFEDCommands",
            buildNodeImageOFEDCommands(os(),
                answerfile()->application.con_interface.has_value(),
                cluster()->getOFED(),
                nodeImageKernel
                    ? std::optional<std::string_view>(nodeImageKernel.value())
                    : std::nullopt)));

    addNodes(image);

    runner::shell::fmt("{}",
        buildHeadnodeHostsRepairScript(
            cluster()
                ->getHeadnode()
                .getConnection(Network::Profile::Management)
                .getAddress()
                .to_string(),
            cluster()->getHeadnode().getFQDN(),
            cluster()->getHeadnode().getHostname()));
}

}

namespace {
auto makeConfluentTestNode(std::optional<std::string_view> mac = std::nullopt,
    bool includeBMC = true) -> opencattus::models::Node
{
    using opencattus::models::CPU;
    using opencattus::models::Node;
    using opencattus::models::OS;

    auto* managementNetwork
        = new Network(Network::Profile::Management, Network::Type::Ethernet);
    managementNetwork->setSubnetMask("255.255.255.0");

    Connection managementConnection(managementNetwork);
    managementConnection.setAddress("192.168.30.11");
    if (mac.has_value()) {
        managementConnection.setMAC(mac.value());
    }

    std::list<Connection> connections;
    connections.emplace_back(std::move(managementConnection));

    OS os(OS::Distro::Rocky, OS::Platform::el9, 7);
    CPU cpu(1, 2, 1);
    std::optional<BMC> bmc = std::nullopt;
    if (includeBMC) {
        bmc.emplace(
            "192.168.30.101", "admin", "pa'ss", 0, 9600, BMC::kind::IPMI);
    }

    Node node("n01", os, cpu, std::move(connections), bmc);
    node.setNodeRootPassword(std::string("labroot"));
    return node;
}
}

TEST_CASE("buildFirewalldTrustedInterfaceCommands checks firewalld activity "
          "before firewall-cmd")
{
    const auto script = buildFirewalldTrustedInterfaceCommands("mgmt0");

    CHECK(script.contains("systemctl is-active --quiet firewalld.service"));
    CHECK(script.contains(
        "firewall-cmd --zone=trusted --change-interface=mgmt0 --permanent"));
    CHECK(script.contains("firewall-cmd --reload"));
    CHECK_FALSE(
        script.contains("systemctl is-enabled --quiet firewalld.service"));
}

TEST_CASE("buildHttpdTlsBootstrapCommands repairs missing or empty mod_ssl "
          "localhost assets")
{
    const auto script = buildHttpdTlsBootstrapCommands();

    CHECK(script.contains("if [ ! -s /etc/pki/tls/certs/localhost.crt ] || [ ! "
                          "-s /etc/pki/tls/private/localhost.key ]; then"));
    CHECK(script.contains(
        "openssl req -x509 -nodes -newkey rsa:2048 -sha256 -days 3650"));
    CHECK(script.contains("-subj '/CN=localhost'"));
    CHECK(script.contains("chmod 0600 /etc/pki/tls/private/localhost.key "
                          "/etc/pki/tls/certs/localhost.crt"));
    CHECK(script.contains("restorecon /etc/pki/tls/certs/localhost.crt "
                          "/etc/pki/tls/private/localhost.key || :"));
}

TEST_CASE("buildNodeDefinitionScript emits a MAC assignment only when the "
          "management MAC is known")
{
    SUBCASE("with mac")
    {
        const auto script = buildNodeDefinitionScript(
            makeConfluentTestNode("52:54:00:00:20:11"), "rocky-9.7-x86_64");

        CHECK(script.contains(
            "nodeattrib n01 net.ipv4_address=192.168.30.11/24"));
        CHECK(script.contains("nodeattrib n01 net.hwaddr=52:54:00:00:20:11"));
        CHECK(
            script.contains("nodedeploy -p n01 -n rocky-9.7-x86_64-diskless"));
        CHECK(script.contains("confluent2hosts -a everything"));
    }

    SUBCASE("without mac")
    {
        const auto script = buildNodeDefinitionScript(
            makeConfluentTestNode(), "rocky-9.7-x86_64");

        CHECK_FALSE(script.contains("net.hwaddr="));
        CHECK(script.contains("bmcpass=pa'ss"));
    }

    SUBCASE("without bmc")
    {
        const auto script = buildNodeDefinitionScript(
            makeConfluentTestNode(std::nullopt, false), "rocky-9.7-x86_64");

        CHECK(script.contains("nodeattrib n01 "
                              "crypted.rootpassword=labroot "
                              "crypted.grubpassword=labroot"));
        CHECK_FALSE(script.contains("bmcuser="));
        CHECK_FALSE(script.contains("bmcpass="));
        CHECK(
            script.contains("nodedeploy -p n01 -n rocky-9.7-x86_64-diskless"));
    }
}

TEST_CASE(
    "buildHeadnodeHostsRepairScript restores the headnode management mapping")
{
    const auto script = buildHeadnodeHostsRepairScript(
        "192.168.33.1", "opencattus.cluster.example.com", "opencattus");

    CHECK(script.contains("confluent2hosts rewrites /etc/hosts"));
    CHECK(script.contains(
        "grep -Fqx '192.168.33.1\topencattus.cluster.example.com opencattus' "
        "/etc/hosts"));
    CHECK(
        script.contains("printf '192.168.33.1\\topencattus.cluster.example.com "
                        "opencattus\\n' >> /etc/hosts"));
}

TEST_CASE("buildConfluentRepoRpmUrl uses upstream by default")
{
    using opencattus::models::OS;
    using opencattus::services::Options;

    opencattus::Singleton<const Options>::init(
        std::make_unique<const Options>(Options {}));

    CHECK(buildConfluentRepoRpmUrl(OS(OS::Distro::Rocky, OS::Platform::el9, 7))
        == "https://hpc.lenovo.com/yum/latest/el9/x86_64/"
           "lenovo-hpc-yum-1-1.x86_64.rpm");
}

TEST_CASE("buildConfluentRepoRpmUrl uses the configured mirror when enabled")
{
    using opencattus::models::OS;
    using opencattus::services::Options;

    opencattus::Singleton<const Options>::init(
        std::make_unique<const Options>(Options {
            .enableMirrors = true,
            .mirrorBaseUrl = "http://mirror.local.versatushpc.com.br/",
        }));

    CHECK(buildConfluentRepoRpmUrl(OS(OS::Distro::Rocky, OS::Platform::el10, 1))
        == "http://mirror.local.versatushpc.com.br/lenovo-hpc/rpm/latest/"
           "el10/x86_64/lenovo-hpc-yum-1-1.x86_64.rpm");
}

TEST_CASE("buildConfluentBootstrapCommands validates Confluent tools before "
          "image work")
{
    using opencattus::models::OS;
    using opencattus::services::Options;

    opencattus::Singleton<const Options>::init(
        std::make_unique<const Options>(Options {
            .enableMirrors = true,
            .mirrorBaseUrl = "http://mirror.local.versatushpc.com.br",
        }));

    const auto script = buildConfluentBootstrapCommands(
        OS(OS::Distro::Rocky, OS::Platform::el9, 7));

    CHECK(
        script.contains("rpm -q lenovo-hpc-yum >/dev/null 2>&1 || rpm -Uvh "
                        "http://mirror.local.versatushpc.com.br/lenovo-hpc/"
                        "rpm/latest/el9/x86_64/lenovo-hpc-yum-1-1.x86_64.rpm"));
    CHECK(
        script.contains("dnf install -y lenovo-confluent tftp-server dnsmasq"));
    CHECK(script.contains("if [ ! -s /etc/pki/tls/certs/localhost.crt ] || [ ! "
                          "-s /etc/pki/tls/private/localhost.key ]; then"));
    CHECK(script.contains("test -f /etc/profile.d/confluent_env.sh"));
    CHECK(script.contains("command -v confluent2hosts >/dev/null"));
    CHECK(script.contains("command -v osdeploy >/dev/null"));
    CHECK(script.contains("command -v imgutil >/dev/null"));
    CHECK_FALSE(script.contains("lenovo-confluent tftp-server dnsmasq || :"));
}

TEST_CASE("buildSpackModuleTree matches Spack's Enterprise Linux module naming")
{
    using opencattus::models::OS;

    CHECK(buildSpackModuleTree(OS(OS::Distro::Rocky, OS::Platform::el8, 10))
        == "linux-rocky8-x86_64");
    CHECK(buildSpackModuleTree(OS(OS::Distro::Rocky, OS::Platform::el9, 7))
        == "linux-rocky9-x86_64");
    CHECK(buildSpackModuleTree(OS(OS::Distro::Rocky, OS::Platform::el10, 0))
        == "linux-rocky10-x86_64");
}

TEST_CASE("buildConfluentImageName matches osdeploy distribution ids")
{
    using opencattus::models::OS;

    CHECK(buildConfluentImageName(OS(OS::Distro::Rocky, OS::Platform::el10, 1))
        == "rocky-10.1-x86_64");
    CHECK(buildConfluentImageName(
              OS(OS::Distro::AlmaLinux, OS::Platform::el10, 1))
        == "alma-10.1-x86_64");
    CHECK(buildConfluentImageName(OS(OS::Distro::RHEL, OS::Platform::el9, 7))
        == "rhel-9.7-x86_64");
    CHECK(buildConfluentImageName(OS(OS::Distro::OL, OS::Platform::el9, 5))
        == "ol-9.5-x86_64");
}

TEST_CASE("buildUserSpackModulePathExport tolerates an unset MODULEPATH")
{
    using opencattus::models::OS;

    CHECK(buildUserSpackModulePathExport(
              OS(OS::Distro::Rocky, OS::Platform::el8, 10))
        == "export "
           "MODULEPATH=/opt/spack/share/spack/lmod/linux-rocky8-x86_64/"
           "Core${MODULEPATH:+:$MODULEPATH}");
    CHECK(buildUserSpackModulePathExport(
              OS(OS::Distro::Rocky, OS::Platform::el10, 0))
        == "export "
           "MODULEPATH=/opt/spack/share/spack/lmod/linux-rocky10-x86_64/"
           "Core${MODULEPATH:+:$MODULEPATH}");
}

TEST_CASE("buildNodeImageRepoFiles keeps distro repo filenames explicit")
{
    using opencattus::models::OS;

    CHECK(buildNodeImageRepoFiles(OS(OS::Distro::Rocky, OS::Platform::el8, 10))
        == "{epel,OpenHPC,rocky}.repo");
    CHECK(buildNodeImageRepoFiles(
              OS(OS::Distro::AlmaLinux, OS::Platform::el8, 10))
        == "{epel,OpenHPC,almalinux}.repo");
    CHECK(buildNodeImageRepoFiles(OS(OS::Distro::RHEL, OS::Platform::el9, 7))
        == "{epel,OpenHPC,rhel}.repo");
    CHECK(buildNodeImageRepoFiles(OS(OS::Distro::OL, OS::Platform::el9, 5))
        == "{epel,OpenHPC,oracle}.repo");
}

TEST_CASE("buildNodeImagePackages keeps EL8 and newer node images explicit")
{
    using opencattus::models::OS;

    const auto el8Packages
        = buildNodeImagePackages(OS(OS::Distro::Rocky, OS::Platform::el8, 10));
    CHECK(el8Packages
        == "ohpc-base-compute ohpc-slurm-client lmod-ohpc hwloc-libs "
           "lua-filesystem lua-posix");

    const auto el9Packages
        = buildNodeImagePackages(OS(OS::Distro::Rocky, OS::Platform::el9, 7));
    CHECK(el9Packages
        == "ohpc-base-compute ohpc-slurm-client lmod-ohpc hwloc-libs");

    const auto el10Packages
        = buildNodeImagePackages(OS(OS::Distro::Rocky, OS::Platform::el10, 1));
    CHECK(el10Packages
        == "ohpc-base-compute ohpc-slurm-client lmod-ohpc hwloc-libs");
}

TEST_CASE("buildNodeImageInstallCommand keeps EL8, EL9, and EL10 explicit")
{
    using opencattus::models::OS;

    CHECK(buildNodeImageInstallCommand(
              OS(OS::Distro::Rocky, OS::Platform::el8, 10))
        == "dnf install -y --nogpg --enablerepo=powertools "
           "ohpc-base-compute ohpc-slurm-client lmod-ohpc hwloc-libs "
           "lua-filesystem lua-posix");

    CHECK(buildNodeImageInstallCommand(
              OS(OS::Distro::Rocky, OS::Platform::el9, 7))
        == "dnf install -y --nogpg "
           "ohpc-base-compute ohpc-slurm-client lmod-ohpc hwloc-libs");

    CHECK(buildNodeImageInstallCommand(
              OS(OS::Distro::Rocky, OS::Platform::el10, 1))
        == "dnf install -y --nogpg "
           "ohpc-base-compute ohpc-slurm-client lmod-ohpc hwloc-libs");
}

TEST_CASE("buildNodeImageOFEDCommands skips OFED runtime staging without an "
          "application network")
{
    using opencattus::models::OS;

    CHECK(buildNodeImageOFEDCommands(
        OS(OS::Distro::Rocky, OS::Platform::el9, 7), false,
        std::optional<OFED>(OFED(OFED::Kind::Doca, "latest")), std::nullopt)
            .empty());
    CHECK(buildNodeImageOFEDCommands(
        OS(OS::Distro::Rocky, OS::Platform::el9, 7), false,
        std::optional<OFED>(OFED(OFED::Kind::Inbox, "")), std::nullopt)
            .empty());
}

TEST_CASE("buildNodeImageOFEDCommands uses inbox packages for inbox OFED")
{
    using opencattus::models::OS;

    const auto script = buildNodeImageOFEDCommands(
        OS(OS::Distro::Rocky, OS::Platform::el9, 7), true,
        std::optional<OFED>(OFED(OFED::Kind::Inbox, "")), std::nullopt);

    CHECK(
        script.contains("dnf group install -y --nogpg \"Infiniband Support\""));
    CHECK_FALSE(script.contains("mlnx-doca.repo"));
    CHECK_FALSE(script.contains("doca-ofed"));
}

TEST_CASE(
    "selectConfluentImageKernelVersion keeps DOCA images on the staged kernel")
{
    const auto kernelVersion = selectConfluentImageKernelVersion(
        std::optional<OFED>(OFED(OFED::Kind::Doca, "latest")), std::nullopt,
        "5.14.0-611.41.1.el9_7.x86_64");

    REQUIRE(kernelVersion.has_value());
    CHECK(kernelVersion.value() == "5.14.0-611.41.1.el9_7.x86_64");
}

TEST_CASE("selectConfluentImageKernelVersion stays empty without OFED or a pin")
{
    const auto kernelVersion = selectConfluentImageKernelVersion(
        std::nullopt, std::nullopt, "5.14.0-611.41.1.el9_7.x86_64");

    CHECK_FALSE(kernelVersion.has_value());
}

TEST_CASE("buildConfluentKernelPackages includes kernel-modules-extra")
{
    using opencattus::models::OS;

    CHECK(buildConfluentKernelPackages(
              OS(OS::Distro::Rocky, OS::Platform::el9, 7),
              "5.14.0-611.41.1.el9_7.x86_64")
        == "kernel-5.14.0-611.41.1.el9_7.x86_64 "
           "kernel-core-5.14.0-611.41.1.el9_7.x86_64 "
           "kernel-modules-5.14.0-611.41.1.el9_7.x86_64 "
           "kernel-modules-core-5.14.0-611.41.1.el9_7.x86_64 "
           "kernel-modules-extra-5.14.0-611.41.1.el9_7.x86_64");
}

TEST_CASE("buildConfluentKernelPackages omits kernel-modules-core on EL8")
{
    using opencattus::models::OS;

    CHECK(buildConfluentKernelPackages(
              OS(OS::Distro::Rocky, OS::Platform::el8, 10),
              "4.18.0-553.75.1.el8_10.x86_64")
        == "kernel-4.18.0-553.75.1.el8_10.x86_64 "
           "kernel-core-4.18.0-553.75.1.el8_10.x86_64 "
           "kernel-modules-4.18.0-553.75.1.el8_10.x86_64 "
           "kernel-modules-extra-4.18.0-553.75.1.el8_10.x86_64");
}

TEST_CASE(
    "buildNodeImageOFEDCommands stages DOCA repos for a selected DOCA kernel")
{
    using opencattus::models::OS;

    const auto script = buildNodeImageOFEDCommands(
        OS(OS::Distro::Rocky, OS::Platform::el9, 7), true,
        std::optional<OFED>(OFED(OFED::Kind::Doca, "latest")),
        std::optional<std::string_view>("5.14.0-611.41.1.el9_7.x86_64"));

    CHECK(script.contains("/etc/yum.repos.d/mlnx-doca.repo"));
    CHECK(script.contains(
        "export doca_image_kernel=5.14.0-611.41.1.el9_7.x86_64"));
    CHECK(script.contains("kernel-${doca_image_kernel}"));
    CHECK(script.contains("kernel-core-${doca_image_kernel}"));
    CHECK(script.contains("kernel-modules-${doca_image_kernel}"));
    CHECK(script.contains("kernel-modules-core-${doca_image_kernel}"));
    CHECK(script.contains("https://download.rockylinux.org/pub/rocky/9.7/"
                          "BaseOS/x86_64/os/Packages/k/"
                          "kernel-${doca_image_kernel}.rpm"));
    CHECK(script.contains(
        "/etc/yum.repos.d/doca-kernel-${doca_image_kernel}.repo"));
    CHECK(script.contains("/opt/mellanox/doca/tools/doca-kernel-support -k "
                          "\"${doca_image_kernel}\""));
    CHECK(script.contains("kernel-devel-${doca_image_kernel}"));
    CHECK(script.contains("kernel-headers-${doca_image_kernel}"));
    CHECK(script.contains("https://download.rockylinux.org/pub/rocky/9.7/"
                          "AppStream/x86_64/os/Packages/k/"
                          "kernel-devel-${doca_image_kernel}.rpm"));
    CHECK(
        script.contains("Failed to stage the exact kernel payload "
                        "${doca_image_kernel} into the Confluent node image"));
    CHECK_FALSE(script.contains("--exclude=kernel-modules-extra"));
    CHECK(script.contains("doca-extra doca-ofed mlnx-ofa_kernel"));
    CHECK_FALSE(script.contains("Infiniband Support"));
}

TEST_CASE(
    "buildNodeImageOFEDCommands can fall back to the detected image kernel")
{
    using opencattus::models::OS;

    const auto script = buildNodeImageOFEDCommands(
        OS(OS::Distro::Rocky, OS::Platform::el9, 7), true,
        std::optional<OFED>(OFED(OFED::Kind::Doca, "latest")), std::nullopt);

    CHECK(script.contains("rpm --root \"$scratchdir\" -q kernel-core --qf "
                          "'%{INSTALLTIME} %{VERSION}-%{RELEASE}.%{ARCH}\\n'"));
}
