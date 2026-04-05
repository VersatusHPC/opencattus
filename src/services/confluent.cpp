#include <fmt/core.h>
#include <opencattus/functions.h>
#include <opencattus/models/cpu.h>
#include <opencattus/models/node.h>
#include <opencattus/models/os.h>
#include <opencattus/services/confluent.h>
#include <opencattus/services/runner.h>
#include <opencattus/utils/network.h>
#include <opencattus/utils/optional.h>
#include <opencattus/utils/singleton.h>

#ifdef BUILD_TESTING
#include <doctest/doctest.h>
#else
#define DOCTEST_CONFIG_DISABLE
#include <doctest/doctest.h>
#endif

namespace {
using namespace opencattus;
using namespace opencattus::utils;

std::string buildFirewalldTrustedInterfaceCommands(
    std::string_view internalNic)
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

std::string buildNodeDefinitionScript(
    const models::Node& node, std::string_view image)
{
    const auto rootpwd = optional::unwrap(node.getNodeRootPassword(),
        "No root password configured for node {}", node.getHostname());

    std::string script = fmt::format(
        R"(
nodedefine {nodeName}
nodeattrib {nodeName} net.ipv4_address={nodeIp}/{nodeCIDR}
nodeattrib {nodeName} bmcuser={bmcuser} bmcpass={bmcpass} crypted.rootpassword={rootpwd} crypted.grubpassword={grubpwd}
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
        fmt::arg("bmcuser", node.getBMC()->getUsername()),
        fmt::arg("bmcpass", node.getBMC()->getPassword()),
        fmt::arg("rootpwd", rootpwd), fmt::arg("grubpwd", rootpwd));

    if (const auto& macOpt
        = node.getConnection(Network::Profile::Management).getMAC();
        macOpt) {
        script += fmt::format(
            "nodeattrib {nodeName} net.hwaddr={nodeMac}\n",
            fmt::arg("nodeName", node.getHostname()),
            fmt::arg("nodeMac", macOpt.value()));
    }

    script += fmt::format(
        "nodedeploy -p {nodeName} -n {image}-diskless\n",
        fmt::arg("nodeName", node.getHostname()), fmt::arg("image", image));
    script += "confluent2hosts -a everything\n";

    return script;
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

    const auto image = fmt::format("{distro}-{arch}",
        fmt::arg("arch", opencattus::utils::enums::toString(os().getArch())),
        fmt::arg("distro", os().getDistroString()));

    runner::shell::fmt(R"d(
# Add the Confluent repository
rpm -ivh https://hpc.lenovo.com/yum/latest/el{releasever}/{arch}/lenovo-hpc-yum-1-1.{arch}.rpm || :

# Install required packages
dnf install -y lenovo-confluent tftp-server dnsmasq || :
systemctl enable confluent --now
systemctl enable httpd --now 
systemctl enable tftp.socket --now
systemctl enable dnsmasq --now

# Enable the Confluent environment
set +xeu # confluent_env.sh has undefined variables
source /etc/profile.d/confluent_env.sh
set -xeu

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
export scratchdir=/tmp/scratchdir
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
\cp -va /etc/yum.repos.d/{{epel,OpenHPC}}.repo $scratchdir/etc/yum.repos.d/
imgutil exec $scratchdir <<EOF
set -xeu -o pipefail
dnf install -y --nogpg ohpc-base-compute ohpc-slurm-client lmod-ohpc hwloc-libs
sed -e '/^account required pam_slurm.so/d' -i /etc/pam.d/sshd
echo 'account required pam_slurm.so' >> /etc/pam.d/sshd
echo SLURMD_OPTIONS=\"--conf-server {hnIp}\" > /etc/sysconfig/slurmd
chown munge: /etc/munge/munge.key
systemctl enable munge
systemctl enable slurmd
EOF

if {ofedEnabled}; then
    # Grabs the folder from the baseurl= line of doca-kernel-$(uname -r).repo
    export doca_repo_folder=$(cat /etc/yum.repos.d/doca-kernel-$(uname -r).repo | perl -lane 'print $1 if /baseurl=file:\/\/(.*)/')
    \cp -va /etc/yum.repos.d/mlnx-doca.repo $scratchdir/etc/yum.repos.d/
    \cp -va /etc/yum.repos.d/doca-kernel-$(uname -r).repo $scratchdir/etc/yum.repos.d/
    imgutil exec -v $doca_repo_folder:$doca_repo_folder $scratchdir <<EOF
        dnf install --nogpg -y doca-ofed mlnx-ofa_kernel
EOF
fi

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
    export MODULEPATH=/opt/spack/share/spack/lmod/{spackModuleTree}/Core:$MODULEPATH
fi

export LMOD_IGNORE_CACHE=1
END
EOF



# Pack the image from the temporary chroot and give a name
imgutil pack /tmp/scratchdir/ {image}-diskless

# Remove the leftover files from the chroot
rm -rf /tmp/scratchdir || :
)d",

        fmt::arg("domain", cluster()->getDomainName()),
        fmt::arg("releasever", os().getMajorVersion()),
        fmt::arg("spackModuleTree", buildSpackModuleTree(os())),
        fmt::arg("hnIp",
            cluster()
                ->getHeadnode()
                .getConnection(Network::Profile::Management)
                .getAddress()
                .to_string()),
        fmt::arg("arch", opencattus::utils::enums::toString(os().getArch())),
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
        fmt::arg("ofedEnabled", answerfile()->ofed.enabled));

    addNodes(image);
}

}

namespace {
#ifdef BUILD_TESTING
auto makeConfluentTestNode(std::optional<std::string_view> mac = std::nullopt)
    -> opencattus::models::Node
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
    BMC bmc("192.168.30.101", "admin", "pa'ss", 0, 9600, BMC::kind::IPMI);

    Node node("n01", os, cpu, std::move(connections), bmc);
    node.setNodeRootPassword(std::string("labroot"));
    return node;
}
#endif
}

TEST_CASE("buildFirewalldTrustedInterfaceCommands checks firewalld activity before firewall-cmd")
{
    const auto script = buildFirewalldTrustedInterfaceCommands("mgmt0");

    CHECK(script.contains("systemctl is-active --quiet firewalld.service"));
    CHECK(script.contains(
        "firewall-cmd --zone=trusted --change-interface=mgmt0 --permanent"));
    CHECK(script.contains("firewall-cmd --reload"));
    CHECK_FALSE(script.contains("systemctl is-enabled --quiet firewalld.service"));
}

TEST_CASE("buildNodeDefinitionScript emits a MAC assignment only when the management MAC is known")
{
    SUBCASE("with mac")
    {
        const auto script
            = buildNodeDefinitionScript(makeConfluentTestNode("52:54:00:00:20:11"),
                "rocky-9.7-x86_64");

        CHECK(script.contains("nodeattrib n01 net.ipv4_address=192.168.30.11/24"));
        CHECK(script.contains("nodeattrib n01 net.hwaddr=52:54:00:00:20:11"));
        CHECK(script.contains("nodedeploy -p n01 -n rocky-9.7-x86_64-diskless"));
        CHECK(script.contains("confluent2hosts -a everything"));
    }

    SUBCASE("without mac")
    {
        const auto script
            = buildNodeDefinitionScript(makeConfluentTestNode(), "rocky-9.7-x86_64");

        CHECK_FALSE(script.contains("net.hwaddr="));
        CHECK(script.contains("bmcpass=pa'ss"));
    }
}

TEST_CASE("buildSpackModuleTree matches Spack's Enterprise Linux module naming")
{
    using opencattus::models::OS;

    CHECK(buildSpackModuleTree(
              OS(OS::Distro::Rocky, OS::Platform::el9, 7))
        == "linux-rocky9-x86_64");
    CHECK(buildSpackModuleTree(
              OS(OS::Distro::Rocky, OS::Platform::el10, 0))
        == "linux-rocky10-x86_64");
}
