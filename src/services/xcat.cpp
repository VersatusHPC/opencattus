/*
 * Copyright 2021 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <algorithm>
#include <cstdlib> // setenv / getenv
#include <optional>
#include <ranges>

#include <filesystem>
#include <fmt/format.h>

#include <opencattus/NFS.h>
#include <opencattus/functions.h>
#include <opencattus/models/cluster.h>
#include <opencattus/models/os.h>
#include <opencattus/services/options.h>
#include <opencattus/services/osservice.h>
#include <opencattus/services/repos.h>
#include <opencattus/services/runner.h>
#include <opencattus/services/xcat.h>
#include <opencattus/utils/singleton.h>

#ifdef BUILD_TESTING
#include <doctest/doctest.h>
#else
#define DOCTEST_CONFIG_DISABLE
#include <doctest/doctest.h>
#endif

namespace {
using opencattus::models::Cluster;
using opencattus::models::OS;

using namespace opencattus::utils::singleton;

// Returns the distribution name with the version, e.g., rocky9.5
std::string getOSImageDistroVersion(const OS& nodeOS)
{
    std::string osimage;

    switch (nodeOS.getDistro()) {
        case OS::Distro::RHEL:
            osimage += "rhels";
            osimage += nodeOS.getVersion();
            break;
        case OS::Distro::OL:
            osimage += "ol";
            osimage += nodeOS.getVersion();
            osimage += ".0";
            break;
        case OS::Distro::Rocky:
            osimage += "rocky";
            osimage += nodeOS.getVersion();
            break;
        case OS::Distro::AlmaLinux:
            osimage += "alma";
            osimage += nodeOS.getVersion();
            break;
    }
    return osimage;
}

std::string getEnterpriseLinuxTemplateVersion(const OS& nodeOS)
{
    switch (nodeOS.getPlatform()) {
        case OS::Platform::el8:
            return "rhels8";
        case OS::Platform::el9:
            return "rhels9";
        case OS::Platform::el10:
            throw std::invalid_argument(
                "xCAT template aliases are not supported on EL10");
        default:
            std::unreachable();
    }
}

std::vector<std::pair<std::string, std::string>> getEnterpriseLinuxCloneAliases(
    const OS& nodeOS)
{
    switch (nodeOS.getPlatform()) {
        case OS::Platform::el8:
            return { { "rocky", "rocky8" }, { "ol", "ol8" },
                { "alma", "alma8" } };
        case OS::Platform::el9:
            return { { "rocky", "rocky9" }, { "ol", "ol9" },
                { "alma", "alma9" } };
        case OS::Platform::el10:
            throw std::invalid_argument(
                "xCAT distro aliases are not supported on EL10");
        default:
            std::unreachable();
    }
}

std::vector<std::string> buildEnterpriseLinuxTemplateAliasCommands(
    const OS& nodeOS)
{
    const auto templateVersion = getEnterpriseLinuxTemplateVersion(nodeOS);
    const auto cloneAliases = getEnterpriseLinuxCloneAliases(nodeOS);
    std::vector<std::string> commands;

    auto createSymlinkCommand = [&](const std::string& folder,
                                     const std::string& version) {
        return fmt::format(
            "ln -sf "
            "/opt/xcat/share/xcat/netboot/rh/compute.{2}.x86_64.exlist "
            "/opt/xcat/share/xcat/netboot/{0}/compute.{1}.x86_64.exlist,"
            "ln -sf "
            "/opt/xcat/share/xcat/netboot/rh/compute.{2}.x86_64.pkglist "
            "/opt/xcat/share/xcat/netboot/{0}/compute.{1}.x86_64.pkglist,"
            "ln -sf "
            "/opt/xcat/share/xcat/netboot/rh/compute.{2}.x86_64.postinstall "
            "/opt/xcat/share/xcat/netboot/{0}/compute.{1}.x86_64.postinstall,"
            "ln -sf "
            "/opt/xcat/share/xcat/netboot/rh/service.{2}.x86_64.exlist "
            "/opt/xcat/share/xcat/netboot/{0}/service.{1}.x86_64.exlist,"
            "ln -sf "
            "/opt/xcat/share/xcat/netboot/rh/"
            "service.{2}.x86_64.otherpkgs.pkglist "
            "/opt/xcat/share/xcat/netboot/{0}/"
            "service.{1}.x86_64.otherpkgs.pkglist,"
            "ln -sf "
            "/opt/xcat/share/xcat/netboot/rh/service.{2}.x86_64.pkglist "
            "/opt/xcat/share/xcat/netboot/{0}/service.{1}.x86_64.pkglist,"
            "ln -sf "
            "/opt/xcat/share/xcat/netboot/rh/service.{2}.x86_64.postinstall "
            "/opt/xcat/share/xcat/netboot/{0}/service.{1}.x86_64.postinstall,"
            "ln -sf /opt/xcat/share/xcat/install/rh/compute.{2}.pkglist "
            "/opt/xcat/share/xcat/install/{0}/compute.{1}.pkglist,"
            "ln -sf /opt/xcat/share/xcat/install/rh/compute.{2}.tmpl "
            "/opt/xcat/share/xcat/install/{0}/compute.{1}.tmpl,"
            "ln -sf /opt/xcat/share/xcat/install/rh/service.{2}.pkglist "
            "/opt/xcat/share/xcat/install/{0}/service.{1}.pkglist,"
            "ln -sf /opt/xcat/share/xcat/install/rh/service.{2}.tmpl "
            "/opt/xcat/share/xcat/install/{0}/service.{1}.tmpl,"
            "ln -sf "
            "/opt/xcat/share/xcat/install/rh/"
            "service.{2}.x86_64.otherpkgs.pkglist "
            "/opt/xcat/share/xcat/install/{0}/"
            "service.{1}.x86_64.otherpkgs.pkglist",
            folder, version, templateVersion);
    };

    for (const auto& [folder, version] : cloneAliases) {
        std::vector<std::string> temp;
        boost::split(temp, createSymlinkCommand(folder, version),
            boost::is_any_of(","));
        commands.insert(commands.end(), temp.begin(), temp.end());
    }

    return commands;
}

std::filesystem::path getLocalOtherPkgRepoPath(const OS& nodeOS)
{
    return std::filesystem::path("/install/post/otherpkgs")
        / getOSImageDistroVersion(nodeOS)
        / opencattus::utils::enums::toString(nodeOS.getArch());
}

std::vector<std::string> getKernelPackagesForGenimage(
    const OS& nodeOS, std::string_view kernelVersion)
{
    switch (nodeOS.getPlatform()) {
        case OS::Platform::el8:
            return {
                fmt::format("kernel-{}", kernelVersion),
                fmt::format("kernel-devel-{}", kernelVersion),
                fmt::format("kernel-core-{}", kernelVersion),
                fmt::format("kernel-modules-{}", kernelVersion),
            };
        case OS::Platform::el9:
            return {
                fmt::format("kernel-{}", kernelVersion),
                fmt::format("kernel-devel-{}", kernelVersion),
                fmt::format("kernel-core-{}", kernelVersion),
                fmt::format("kernel-modules-{}", kernelVersion),
                fmt::format("kernel-modules-core-{}", kernelVersion),
            };
        case OS::Platform::el10:
            return {
                fmt::format("kernel-{}", kernelVersion),
                fmt::format("kernel-devel-{}", kernelVersion),
                fmt::format("kernel-core-{}", kernelVersion),
                fmt::format("kernel-modules-{}", kernelVersion),
                fmt::format("kernel-modules-core-{}", kernelVersion),
            };
        default:
            std::unreachable();
    }
}

struct XcatInfinibandPlan final {
    std::vector<std::string> otherPackages;
    std::optional<std::string> kernelVersion;
    std::optional<std::string> localRepoName;
};

XcatInfinibandPlan buildXcatInfinibandPlan(const OFED& ofed, const OS& nodeOS,
    std::optional<std::string_view> configuredKernel,
    std::string_view runningKernel)
{
    switch (nodeOS.getPlatform()) {
        case OS::Platform::el8:
        case OS::Platform::el9:
            break;
        case OS::Platform::el10:
            throw std::invalid_argument(
                "xCAT compute-node OFED staging is unsupported on EL10");
        default:
            std::unreachable();
    }

    switch (ofed.getKind()) {
        case OFED::Kind::Inbox:
            return XcatInfinibandPlan {
                .otherPackages = { "@infiniband" },
                .kernelVersion = std::nullopt,
                .localRepoName = std::nullopt,
            };

        case OFED::Kind::Doca: {
            const auto kernelVersion = configuredKernel.has_value()
                ? std::string(configuredKernel.value())
                : std::string(runningKernel);
            return XcatInfinibandPlan {
                .otherPackages = { "mlnx-ofa_kernel", "doca-ofed" },
                .kernelVersion = kernelVersion,
                .localRepoName = fmt::format("doca-kernel-{}", kernelVersion),
            };
        }

        case OFED::Kind::Oracle:
            throw std::logic_error(
                "Oracle RDMA release is not yet supported");
    }

    std::unreachable();
}

std::string formatDHCPInterfaces(std::string_view interface)
{
    // xCAT expects either a plain interface list or a real hostname selector
    // in the form "<hostname>|<iface>". Using the literal "xcatmn" does not
    // match this management node, so makedhcp emits no subnet declarations.
    return std::string(interface);
}

std::string buildDHCPInterfacesCommand(std::string_view interface)
{
    // `chdef` expects a raw key=value token here. Wrapping the whole
    // assignment in quotes makes xCAT store the literal key `'dhcpinterfaces`
    // and value `oc-mgmt0'`, which breaks `makedhcp -n`.
    return fmt::format(
        "chdef -t site dhcpinterfaces={}", formatDHCPInterfaces(interface));
}

std::string buildPrecreateMyPostscriptsCommand(bool enabled)
{
    return fmt::format("chdef -t site precreatemypostscripts={}",
        enabled ? "YES" : "NO");
}

std::string shellSingleQuote(std::string_view value)
{
    std::string quoted = "'";
    for (const char chr : value) {
        if (chr == '\'') {
            quoted += "'\"'\"'";
            continue;
        }
        if (chr == '\n' || chr == '\r') {
            throw std::invalid_argument(
                "Shell-quoted strings cannot contain newlines");
        }
        quoted += chr;
    }
    quoted += "'";
    return quoted;
}

std::string getStatelessNodeRootPassword()
{
    const auto& nodes = cluster()->getNodes();
    LOG_ASSERT(!nodes.empty(), "xCAT image generation requires compute nodes");

    const auto& firstNode = nodes.front();
    const auto firstPassword = firstNode.getNodeRootPassword();
    opencattus::functions::abortif(!firstPassword.has_value(),
        "Node {} is missing a root password", firstNode.getHostname());

    for (const auto& node : nodes) {
        const auto nodePassword = node.getNodeRootPassword();
        opencattus::functions::abortif(!nodePassword.has_value(),
            "Node {} is missing a root password", node.getHostname());
        opencattus::functions::abortif(nodePassword.value()
                != firstPassword.value(),
            "xCAT stateless images require the same node_root_password for "
            "all compute nodes; {} differs",
            node.getHostname());
    }

    return firstPassword.value();
}

std::string buildIpmitoolCommand(std::string_view address,
    std::string_view username, std::string_view password,
    std::string_view subcommand)
{
    return fmt::format("ipmitool -I lanplus -H {} -U {} -P {} {}",
        shellSingleQuote(address), shellSingleQuote(username),
        shellSingleQuote(password), subcommand);
}

int runDirectIpmiCommand(std::string_view description,
    std::string_view subcommand)
{
    std::string script = "rc=0\n";
    bool hasBMC = false;

    for (const auto& node : cluster()->getNodes()) {
        const auto& bmc = node.getBMC();
        if (!bmc.has_value()) {
            continue;
        }

        hasBMC = true;
        script += fmt::format("echo {}\n",
            shellSingleQuote(fmt::format(
                "Trying direct IPMI fallback for {} on {}",
                description, node.getHostname())));
        script += fmt::format("{} || rc=$?\n",
            buildIpmitoolCommand(bmc->getAddress(), bmc->getUsername(),
                bmc->getPassword(), subcommand));
    }

    if (!hasBMC) {
        return 1;
    }

    script += "exit $rc\n";
    return opencattus::services::runner::shell::unsafe::cmd(script);
}

void runIpmiCommandWithFallback(
    std::string_view xcatCommand, std::string_view subcommand,
    std::string_view description)
{
    auto runner = opencattus::utils::singleton::runner();
    const auto exitCode = runner->executeCommand(std::string(xcatCommand));
    if (exitCode == 0) {
        return;
    }

    LOG_WARN("xCAT command `{}` failed with exit code {}, retrying via direct "
             "IPMI for {}",
        xcatCommand, exitCode, description);

    const auto directExitCode = runDirectIpmiCommand(description, subcommand);
    if (directExitCode != 0) {
        LOG_WARN("Direct IPMI fallback for {} failed with exit code {}",
            description, directExitCode);
    }
}
}; // namespace{}

namespace opencattus::services {

using opencattus::models::Node;
using opencattus::services::repos::RepoManager;

XCAT::XCAT()
{

    // Initialize some environment variables needed by proper xCAT execution
    // TODO: Look for a better way to do this
    std::string oldPath = std::getenv("PATH");
    std::string newPath = "/opt/xcat/bin:"
                          "/opt/xcat/sbin:"
                          "/opt/xcat/share/xcat/tools:"
        + oldPath;
    setenv("PATH", newPath.c_str(), true);
    setenv("XCATROOT", "/opt/xcat", false);

    // TODO: Hacky, we should properly set environment variable on locale
    setenv("PERL_BADLANG", "0", false);

    // Ensure image name is setted
    generateOSImageName(ImageType::Netboot, NodeType::Compute);
}

XCAT::Image XCAT::getImage() const { return m_stateless; }

void XCAT::installPackages()
{
    auto osservice = opencattus::utils::singleton::osservice();
    osservice->install("xCAT");
    // xCAT's embedded Perl IPMI stack does not interoperate cleanly with
    // VirtualBMC on EL9, so keep ipmitool available as a fallback path.
    osservice->install("ipmitool");
    // xCAT always prepends a local file:// otherpkgdir for osimages; ensure we
    // can publish metadata there even when we do not ship custom RPMs.
    osservice->install("createrepo_c");
}

void XCAT::patchInstall()
{
    /* Required for EL 9.5
     * Upstream PR: https://github.com/xcat2/xcat-core/pull/7489
     */

    const auto opts = opencattus::utils::singleton::options();
    auto runner = opencattus::utils::singleton::runner();
    if (opts->shouldForce("xcat-patch")
        || runner->executeCommand(
               "grep -q \"extensions usr_cert\" "
               "/opt/xcat/share/xcat/scripts/setup-local-client.sh")
            == 0) {
        runner->executeCommand(
            "sed -i \"s/-extensions usr_cert //g\" "
            "/opt/xcat/share/xcat/scripts/setup-local-client.sh");
        runner->executeCommand(
            "sed -i \"s/-extensions server //g\" "
            "/opt/xcat/share/xcat/scripts/setup-server-cert.sh");

        opencattus::services::runner::shell::cmd(
            R"del((cd / && patch --forward --batch -p0 <<'EOF'
--- opt/xcat/lib/perl/xCAT_plugin/ddns.pm.orig	2025-07-16 09:53:20.546246189 -0300
+++ opt/xcat/lib/perl/xCAT_plugin/ddns.pm	2025-07-16 09:53:36.614512354 -0300
@@ -1286,8 +1286,8 @@ sub update_namedconf {
             my @bind_version =xCAT::Utils->runcmd($bind_version_cmd, 0);
             # Turn off DNSSEC if running with bind vers 9.16.6 or higher
             if ((scalar @bind_version > 0) && (xCAT::Utils::CheckVersion($bind_version[0], "9.16.6") >= 0)) {
-                push @newnamed, "\tdnssec-enable no;\n";
-                push @newnamed, "\tdnssec-validation no;\n";
+                push @newnamed, "\t#dnssec-enable no;\n";
+                push @newnamed, "\t#dnssec-validation no;\n";
             }
         }
 
EOF
))del");
        opts->maybeStopAfterStep("xcat-patch");

        // `dnf install xCAT` already runs `xcatconfig -i` from the RPM
        // scriptlets. Re-running the full interactive reconfiguration here is
        // not idempotent and can stall unattended EL9 installs after the
        // certificate prompts. Restart the provisioner services instead so the
        // patched helpers and plugin code are picked up without replaying the
        // full bootstrap workflow.
        runner->executeCommand("systemctl enable --now xcatd httpd");
        runner->executeCommand("systemctl restart xcatd httpd");
    } else {
        LOG_WARN("xCAT Already patched, skipping");
    }
}

void XCAT::setup() const
{
    setDHCPInterfaces(cluster()
            ->getHeadnode()
            .getConnection(Network::Profile::Management)
            .getInterface()
            .value());
    setPrecreateMyPostscripts(true);
    setDomain(cluster()->getDomainName());
}

/* TODO: Maybe create a chdef method to do it cleaner? */
void XCAT::setDHCPInterfaces(std::string_view interface)
{
    auto runner = opencattus::utils::singleton::runner();
    runner->checkCommand(buildDHCPInterfacesCommand(interface));
}

void XCAT::setPrecreateMyPostscripts(bool enabled)
{
    auto runner = opencattus::utils::singleton::runner();
    runner->checkCommand(buildPrecreateMyPostscriptsCommand(enabled));
}

void XCAT::setDomain(std::string_view domain)
{
    auto runner = opencattus::utils::singleton::runner();
    runner->checkCommand(fmt::format("chdef -t site domain={}", domain));
}

namespace {
    constexpr bool imageExists(const std::string& image)
    {
        LOG_ASSERT(
            image.size() > 0, "Trying to generate an image with empty name");

        auto opts = opencattus::utils::singleton::options();
        if (opts->dryRun) {
            LOG_WARN(
                "Dry-Run: skipping image check, assuming it doesn't exists");
            return false;
        }

        auto runner = opencattus::utils::singleton::runner();
        if (opts->shouldForce("genimage")) {
            runner->executeCommand(
                fmt::format("rmdef -t osimage -o {}", image));
        }

        std::list<std::string> output;
        auto exitCode = runner->executeCommand(
            fmt::format("lsdef -t osimage {}", image), output);
        if (exitCode == 0) { // image exists
            LOG_WARN(
                "Skipping image generation {}, use --force=genimage to force",
                image, image);
            LOG_DEBUG("Command output: {}", fmt::join(output, "\n"));
            return true;
        }

        return false;
    }

}; // anonymous namespace

void XCAT::copycds(const std::filesystem::path& diskImage)
{
    opencattus::utils::singleton::runner()->checkCommand(
        fmt::format("copycds {}", diskImage.string()));
}

void XCAT::genimage() const
{
    using namespace runner;
    const auto kernelVersionOpt = answerfile()->system.kernel;
    if (!kernelVersionOpt) {
        shell::fmt("genimage {} ", m_stateless.osimage);
        return;
    }
    const auto& kernelVersion = kernelVersionOpt.value();
    const auto& nodeOS = cluster()->getNodes().front().getOS();

    LOG_WARN(
        "Using kernel version from the answerfile: {} at [system].kernel in {}",
        kernelVersion, answerfile()->path());
    LOG_INFO("Customizing the kernel image");
    const auto kernelPackages = fmt::format("{}",
        fmt::join(getKernelPackagesForGenimage(nodeOS, kernelVersion), " "));

    shell::fmt("mkdir -p /install/kernels/{}", kernelVersion);
    shell::fmt("dnf download {} --destdir /install/kernels/{}", kernelPackages,
        kernelVersion);
    shell::fmt("createrepo /install/kernels/{}", kernelVersion);
    shell::fmt("chdef -t osimage {} -p pkgdir=/install/kernels/{}",
        m_stateless.osimage, kernelVersion);
    shell::fmt("genimage {} -k {}", m_stateless.osimage, kernelVersion);
}

void XCAT::packimage() const
{
    opencattus::utils::singleton::runner()->checkCommand(
        fmt::format("packimage {}", m_stateless.osimage));
}

void XCAT::nodeset(std::string_view nodes) const
{
    opencattus::utils::singleton::runner()->checkCommand(
        fmt::format("nodeset {} osimage={} -g", nodes, m_stateless.osimage));
}

void XCAT::createDirectoryTree()
{
    functions::createDirectory(CHROOT "/install/custom/netboot");
    functions::createDirectory(
        getLocalOtherPkgRepoPath(cluster()->getNodes().front().getOS()));
}

void XCAT::configureSELinux()
{
    m_stateless.postinstall.emplace_back(
        fmt::format("echo \"SELINUX=disabled\nSELINUXTYPE=targeted\" > "
                    "$IMG_ROOTIMGDIR/etc/selinux/config\n\n"));
}

void XCAT::configureOpenHPC()
{
    const auto packages = { "ohpc-base-compute", "lmod-ohpc", "lua" };

    m_stateless.otherpkgs.reserve(packages.size());
    for (const auto& package : std::as_const(packages)) {
        m_stateless.otherpkgs.emplace_back(package);
    }

    // We always sync local Unix files to keep services consistent, even with
    // external directory services
    m_stateless.synclists.emplace_back("/etc/passwd -> /etc/passwd\n"
                                       "/etc/group -> /etc/group\n");
}

void XCAT::configureTimeService()
{
    m_stateless.otherpkgs.emplace_back("chrony");

    m_stateless.postinstall.emplace_back(fmt::format(
        "echo \"server {} iburst\" >> $IMG_ROOTIMGDIR/etc/chrony.conf\n\n",
        cluster()
            ->getHeadnode()
            .getConnection(Network::Profile::Management)
            .getAddress()
            .to_string()));
}

void XCAT::configureRemoteAccess()
{
    const auto nodeRootPassword
        = shellSingleQuote(getStatelessNodeRootPassword());

    // EL9 diskless nodes can reach `multi-user.target` before xCAT's
    // postbootscripts populate SSH material. Seed the authorized key and host
    // keys into the image so sshd can come up on the first boot.
    m_stateless.postinstall.emplace_back(
        fmt::format("printf 'root:%s\\n' {} | chroot $IMG_ROOTIMGDIR chpasswd\n",
            nodeRootPassword)
        +
        "install -d -m 0700 $IMG_ROOTIMGDIR/root/.ssh\n"
        "if [ -f /install/postscripts/_ssh/authorized_keys ]; then\n"
        "  install -m 0600 /install/postscripts/_ssh/authorized_keys "
        "$IMG_ROOTIMGDIR/root/.ssh/authorized_keys\n"
        "fi\n"
        "ssh_group=root\n"
        "if getent group ssh_keys >/dev/null 2>&1; then\n"
        "  ssh_group=ssh_keys\n"
        "fi\n"
        "for key_type in rsa ecdsa ed25519; do\n"
        "  key_path=$IMG_ROOTIMGDIR/etc/ssh/ssh_host_${key_type}_key\n"
        "  if [ ! -s $key_path ]; then\n"
        "    ssh-keygen -q -t ${key_type} -N '' -f $key_path\n"
        "  fi\n"
        "  chown root:${ssh_group} $key_path $key_path.pub\n"
        "  chmod 0640 $key_path\n"
        "  chmod 0644 $key_path.pub\n"
        "done\n"
        "install -d $IMG_ROOTIMGDIR/usr/lib/tmpfiles.d\n"
        "cat > $IMG_ROOTIMGDIR/usr/lib/tmpfiles.d/opencattus-sshd.conf "
        "<<'EOF'\n"
        "d /run/sshd 0755 root root -\n"
        "EOF\n"
        "\n");
}

void XCAT::configureInfiniband()
{
    const auto osinfo
        = opencattus::utils::singleton::cluster()->getNodes()[0].getOS();
    LOG_INFO("[xCAT] Configuring infiniband");
    if (const auto& ofed = cluster()->getOFED()) {
        auto runner = opencattus::utils::singleton::runner();
        auto osservice = opencattus::utils::singleton::osservice();
        const auto configuredKernel = osinfo.getKernel();
        const auto runningKernel = osservice->getKernelRunning();
        if (!configuredKernel.has_value()) {
            LOG_WARN("Kernel version ommited in configuration, using "
                     "the running kernel {}",
                runningKernel);
        }
        const auto plan = buildXcatInfinibandPlan(*ofed, osinfo,
            configuredKernel
                ? std::optional<std::string_view>(configuredKernel.value())
                : std::nullopt,
            runningKernel);

        for (const auto& package : plan.otherPackages) {
            m_stateless.otherpkgs.emplace_back(package);
        }

        if (!plan.localRepoName.has_value()) {
            return;
        }

        auto repoManager = opencattus::utils::singleton::repos();
        auto opts = opencattus::utils::singleton::options();

        // Configure Apache to serve the staged DOCA kernel repository.
        const auto localRepo = functions::createHTTPRepo(plan.localRepoName.value());

        // Create the RPM repository.
        runner->checkCommand(
            fmt::format("bash -c \"cp -v "
                        "/usr/share/doca-host-*/Modules/{}/*.rpm {}\"",
                plan.kernelVersion.value(), localRepo.directory.string()));
        runner->checkCommand(
            fmt::format("createrepo {}", localRepo.directory.string()));

        // dryRun does not initialize the repositories
        if (!opts->dryRun) {
            auto docaUrl = repoManager->repo("doca")->uri().value();
            runner->checkCommand(fmt::format(
                "bash -c \"chdef -t osimage {} --plus otherpkgdir={}\"",
                m_stateless.osimage, docaUrl));
        }

        // Add the local repository to the stateless image
        runner->checkCommand(fmt::format(
            "bash -c \"chdef -t osimage {} --plus otherpkgdir={}\"",
            m_stateless.osimage, localRepo.url));
    }
}

void XCAT::configureSLURM()
{
    // NOTE: hwloc-libs required to fix slurmd
    m_stateless.otherpkgs.emplace_back("ohpc-slurm-client");
    m_stateless.otherpkgs.emplace_back("hwloc-libs");

    // TODO: Deprecate this for SRV entries on DNS: _slurmctld._tcp 0 100 6817
    m_stateless.postinstall.emplace_back(
        fmt::format("echo SLURMD_OPTIONS=\\\"--conf-server {}\\\" > "
                    "$IMG_ROOTIMGDIR/etc/sysconfig/slurmd\n\n",
            cluster()
                ->getHeadnode()
                .getConnection(Network::Profile::Management)
                .getAddress()
                .to_string()));

    // Diskless nodes need SSH reachable before they have joined SLURM. A
    // blanket pam_slurm gate locks xCAT and root out during first boot and
    // leaves the node impossible to debug or finish configuring.

    // Enable services on image
    m_stateless.postinstall.emplace_back(
        "install -d -m 0755 $IMG_ROOTIMGDIR/usr/lib/tmpfiles.d\n"
        "cat > $IMG_ROOTIMGDIR/usr/lib/tmpfiles.d/opencattus-slurm.conf "
        "<<'EOF'\n"
        "d /run/slurm 0755 slurm slurm -\n"
        "d /run/slurmd 0755 slurm slurm -\n"
        "d /var/log/slurm 0755 slurm slurm -\n"
        "d /var/spool/slurmd 0755 slurm slurm -\n"
        "f /var/log/slurm/slurmd.log 0640 slurm slurm -\n"
        "EOF\n"
        "install -d -o slurm -g slurm -m 0755 "
        "$IMG_ROOTIMGDIR/var/log/slurm "
        "$IMG_ROOTIMGDIR/var/spool/slurmd\n"
        "install -m 0640 -o slurm -g slurm /dev/null "
        "$IMG_ROOTIMGDIR/var/log/slurm/slurmd.log\n"
        "chroot $IMG_ROOTIMGDIR systemctl enable munge\n"
        "chroot $IMG_ROOTIMGDIR systemctl enable slurmd\n"
        "\n");

    m_stateless.synclists.emplace_back(
        "/etc/slurm/slurm.conf -> /etc/slurm/slurm.conf\n"
        "/etc/munge/munge.key -> /etc/munge/munge.key\n"
        "\n");
}

void XCAT::generateOtherPkgListFile() const
{
    std::string_view filename
        = CHROOT "/install/custom/netboot/compute.otherpkglist";

    functions::removeFile(filename);
    functions::addStringToFile(
        filename, fmt::format("{}\n", fmt::join(m_stateless.otherpkgs, "\n")));
}

void XCAT::generatePostinstallFile()
{
    std::string_view filename
        = CHROOT "/install/custom/netboot/compute.postinstall";

    functions::removeFile(filename);

    m_stateless.postinstall.emplace_back(
        "install -d $IMG_ROOTIMGDIR/etc/rc.d/init.d "
        "$IMG_ROOTIMGDIR/etc/udev/rules.d\n"
        "if [ ! -e $IMG_ROOTIMGDIR/etc/init.d ]; then\n"
        "  ln -s rc.d/init.d $IMG_ROOTIMGDIR/etc/init.d\n"
        "fi\n"
        "cat > $IMG_ROOTIMGDIR/etc/udev/rules.d/99-opencattus-placeholder.rules "
        "<<'EOF'\n"
        "# Placeholder file for xCAT EL9 image generation.\n"
        "EOF\n"
        "\n");

    m_stateless.postinstall.emplace_back(
        "perl -pi -e 's/# End of file/\\* soft memlock unlimited\\n$&/s' "
        "$IMG_ROOTIMGDIR/etc/security/limits.conf\n"
        "perl -pi -e 's/# End of file/\\* hard memlock unlimited\\n$&/s' "
        "$IMG_ROOTIMGDIR/etc/security/limits.conf\n"
        "\n");

    m_stateless.postinstall.emplace_back(
        "chroot $IMG_ROOTIMGDIR systemctl disable firewalld\n");

    for (const auto& entries : std::as_const(m_stateless.postinstall)) {
        functions::addStringToFile(filename, entries);
    }

    auto opts = opencattus::utils::singleton::options();

    if (opts->dryRun) {
        LOG_INFO("Dry Run: Would change file {} permissions", filename)
        return;
    }
    std::filesystem::permissions(filename,
        std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec
            | std::filesystem::perms::others_exec,
        std::filesystem::perm_options::add);
}

void XCAT::generateSynclistsFile()
{
    std::string_view filename
        = CHROOT "/install/custom/netboot/compute.synclists";

    functions::removeFile(filename);
    functions::addStringToFile(filename,
        "/etc/passwd -> /etc/passwd\n"
        "/etc/group -> /etc/group\n"
        "/etc/slurm/slurm.conf -> /etc/slurm/slurm.conf\n"
        "/etc/munge/munge.key -> /etc/munge/munge.key\n");
}

void XCAT::configureOSImageDefinition() const
{
    auto opts = opencattus::utils::singleton::options();
    auto runner = opencattus::utils::singleton::runner();
    const auto localOtherPkgDir
        = getLocalOtherPkgRepoPath(cluster()->getNodes().front().getOS());
    opencattus::services::runner::shell::cmd(fmt::format(
        "mkdir -p {} && createrepo_c --update {}",
        shellSingleQuote(localOtherPkgDir.string()),
        shellSingleQuote(localOtherPkgDir.string())));

    runner->executeCommand(
        fmt::format("chdef -t osimage {} --plus otherpkglist="
                    "/install/custom/netboot/compute.otherpkglist",
            m_stateless.osimage));

    runner->executeCommand(
        fmt::format("chdef -t osimage {} --plus postinstall="
                    "/install/custom/netboot/compute.postinstall",
            m_stateless.osimage));

    runner->executeCommand(
        fmt::format("chdef -t osimage {} --plus synclists="
                    "/install/custom/netboot/compute.synclists",
            m_stateless.osimage));

    /* Add external repositories to otherpkgdir */
    if (!opts->dryRun) {
        std::vector<std::string> repos = getxCATOSImageRepos();
        runner->executeCommand(
            fmt::format("chdef -t osimage {} --plus otherpkgdir={}",
                m_stateless.osimage, fmt::join(repos, ",")));
    }
}

void XCAT::customizeImage(
    const std::vector<ScriptBuilder>& customizations) const
{
    auto runner = opencattus::utils::singleton::runner();
    // @TODO: Extract the munge fixes to its own customization script
    // Permission fixes for munge
    if (cluster()->getQueueSystem().value()->getKind()
        == models::QueueSystem::Kind::SLURM) {
        opencattus::functions::createDirectory(m_stateless.chroot / "etc");
        runner->executeCommand(
            fmt::format("cp -f /etc/passwd /etc/group /etc/shadow {}/etc",
                m_stateless.chroot.string()));
        runner->executeCommand(
            fmt::format("mkdir -p {0}/var/lib/munge {0}/var/log/munge "
                        "{0}/etc/munge {0}/run/munge",
                m_stateless.chroot.string()));
        runner->executeCommand(fmt::format(
            "chown munge:munge {}/var/lib/munge", m_stateless.chroot.string()));
        runner->executeCommand(fmt::format(
            "chown munge:munge {}/var/log/munge", m_stateless.chroot.string()));
        runner->executeCommand(fmt::format(
            "chown munge:munge {}/etc/munge", m_stateless.chroot.string()));
        runner->executeCommand(fmt::format(
            "chown munge:munge {}/run/munge", m_stateless.chroot.string()));
    }

    for (const auto& script : customizations) {
        runner->run(script);
    };
}

void XCAT::configureEL8()
{
    auto runner = opencattus::utils::singleton::runner();
    const auto nodeOS = cluster()->getNodes().front().getOS();
    for (const auto& command : buildEnterpriseLinuxTemplateAliasCommands(
             nodeOS)) {
        runner->executeCommand(command);
    }
}

/* This is necessary to avoid problems with EL9-based distros.
 * The xCAT team has discontinued the project and distros based on EL9 are not
 * officially supported by default.
 */
void XCAT::configureEL9()
{
    auto runner = opencattus::utils::singleton::runner();
    const auto nodeOS = cluster()->getNodes().front().getOS();
    for (const auto& command : buildEnterpriseLinuxTemplateAliasCommands(
             nodeOS)) {
        runner->executeCommand(command);
    }
}

opencattus::services::XCAT::ImageInstallArgs XCAT::getImageInstallArgs(
    ImageType imageType, NodeType nodeType)
{
    generateOSImageName(imageType, nodeType);
    generateOSImagePath(imageType, nodeType);
    LOG_ASSERT(!m_stateless.osimage.empty(), "Empty osimage name");
    return ImageInstallArgs { .imageName = m_stateless.osimage,
        .rootfs = m_stateless.chroot,
        .postinstall = "/install/custom/netboot/compute.postinstall",
        .pkglist = "/install/custom/netboot/compute.otherpkglist" };
}

/* This method will create an image for compute nodes, by default it will be a
 * stateless image with default services.
 */
void XCAT::createImage(ImageType imageType, NodeType nodeType,
    const std::vector<ScriptBuilder>& customizations)
{
    switch (cluster()->getNodes().front().getOS().getPlatform()) {
        case OS::Platform::el8:
            configureEL8();
            break;
        case OS::Platform::el9:
            configureEL9();
            break;
        case OS::Platform::el10:
            throw std::logic_error("xCAT image generation is unsupported on EL10");
        default:
            std::unreachable();
    }

    generateOSImageName(imageType, nodeType);

    const auto opts = opencattus::utils::singleton::options();
    const auto imageExists_ = imageExists(m_stateless.osimage);
    const auto runner = opencattus::utils::singleton::runner();
    if (!imageExists_ || opts->shouldSkip("copycds")) {
        if (opts->shouldSkip("copycds")) {
            // Remove rootfs and cleanup otherpkgs and postinstall scripts
            runner->executeCommand(
                fmt::format("bash -c \"rm -rf {} && echo > {} && echo > {}\"",
                    m_stateless.chroot,
                    "/install/custom/netboot/compute.otherpkglist",
                    "/install/custom/netboot/compute.postinstall"));
        } else {
            copycds(cluster()->getDiskImage().getPath());
        }
        generateOSImagePath(imageType, nodeType);

        createDirectoryTree();
        configureSELinux();
        configureOpenHPC();
        configureTimeService();
        configureRemoteAccess();
        configureInfiniband();
        configureSLURM();

        generateOtherPkgListFile();
        generatePostinstallFile();
        generateSynclistsFile();

        configureOSImageDefinition();

        customizeImage(customizations);
        genimage();
        packimage();
    }
}

void XCAT::addNode(const Node& node)
{
    LOG_DEBUG("Adding node {} to xCAT", node.getHostname())

    std::string command = fmt::format(
        "mkdef -f -t node {} arch={} ip={} mac={} groups=compute,all "
        "netboot=xnba ",
        node.getHostname(),
        opencattus::utils::enums::toString(node.getOS().getArch()),
        node.getConnection(Network::Profile::Management)
            .getAddress()
            .to_string(),
        node.getConnection(Network::Profile::Management).getMAC().value());

    if (const auto& bmc = node.getBMC())
        command += fmt::format("bmc={} bmcusername={} bmcpassword={} mgt=ipmi "
                               "cons=ipmi serialport={} serialspeed={} ",
            bmc->m_address, bmc->m_username, bmc->m_password, bmc->m_serialPort,
            bmc->m_serialSpeed);

    // FIXME:
    //  *********************************************************************
    //  * This is __BAD__ implementation. We cannot use try/catch as return *
    //  *********************************************************************
    try {
        command += fmt::format(
            "nicips.ib0={} nictypes.ib0=\"InfiniBand\" nicnetworks.ib0=ib0 ",
            node.getConnection(Network::Profile::Application)
                .getAddress()
                .to_string());
    } catch (...) {
    }

    opencattus::utils::singleton::runner()->executeCommand(command);
}

void XCAT::addNodes() const
{
    for (const auto& node : cluster()->getNodes()) {
        addNode(node);
    }

    auto runner = opencattus::utils::singleton::runner();

    // TODO: Create separate functions
    runner->executeCommand("makehosts");
    runner->executeCommand("makedhcp -n");
    // xCAT updates node DHCP state through OMAPI during `nodeset`; that fails
    // unless dhcpd is already running with the regenerated configuration.
    runner->checkCommand("systemctl restart dhcpd");
    runner->executeCommand("makedns -n");
    runner->executeCommand("makegocons");
    setNodesImage();
}

void XCAT::setNodesImage() const
{
    // TODO: For now we always run nodeset for all computes
    nodeset("compute");
}

void XCAT::setNodesBoot()
{
    // TODO: Do proper checking if a given node have BMC support, and then issue
    //  rsetboot only on the compatible machines instead of running in compute.
    runIpmiCommandWithFallback(
        "rsetboot compute net", "chassis bootdev pxe", "PXE boot configuration");
}

void XCAT::resetNodes()
{
    runIpmiCommandWithFallback(
        "rpower compute reset", "chassis power reset", "node reset");
}

void XCAT::generateOSImageName(ImageType imageType, NodeType nodeType)
{
    const auto& nodeOS = cluster()->getNodes()[0].getOS();
    std::string osimage = getOSImageDistroVersion(nodeOS);
    osimage += "-";

    switch (nodeOS.getArch()) {
        case OS::Arch::x86_64:
            osimage += "x86_64";
            break;
        case OS::Arch::ppc64le:
            osimage += "ppc64le";
            break;
    }
    osimage += "-";

    switch (imageType) {
        case ImageType::Install:
            osimage += "install";
            break;
        case ImageType::Netboot:
            osimage += "netboot";
            break;
    }
    osimage += "-";

    switch (nodeType) {
        case NodeType::Compute:
            osimage += "compute";
            break;
        case NodeType::Service:
            osimage += "service";
            break;
    }

    m_stateless.osimage = osimage;
}

void XCAT::generateOSImagePath(ImageType imageType, NodeType nodeType)
{

    if (imageType != XCAT::ImageType::Netboot) {
        throw std::logic_error(
            "Image path is only available on Netboot (Stateless) images");
    }

    std::filesystem::path chroot = "/install/netboot/";
    chroot += getOSImageDistroVersion(cluster()->getNodes()[0].getOS());
    chroot += "/";

    switch (cluster()->getNodes()[0].getOS().getArch()) {
        case OS::Arch::x86_64:
            chroot += "x86_64";
            break;
        case OS::Arch::ppc64le:
            chroot += "ppc64le";
            break;
    }

    chroot += "/compute/rootimg";
    m_stateless.chroot = chroot;
}

std::vector<std::string> XCAT::getxCATOSImageRepos()
{
    const auto osinfo = cluster()->getHeadnode().getOS();
    const auto repoManager = opencattus::utils::singleton::repos();
    std::vector<std::string> repos;
    const auto addReposFromFile = [&](const std::string& filename) {
        for (auto& repo : repoManager->repoFile(filename)) {
            if (repo->enabled()) {
                repos.emplace_back(utils::optional::unwrap(repo->uri(),
                    "Expecting value for repository URI {}, found None, check "
                    "{}",
                    repo->id(), repo->source()));
            }
        }
    };

    switch (osinfo.getDistro()) {
        case OS::Distro::RHEL:
            addReposFromFile("rhel.repo");
            break;
        case OS::Distro::OL:
            addReposFromFile("oracle.repo");
            break;
        case OS::Distro::Rocky:
            RockyLinux::shouldUseVault(osinfo)
                ? addReposFromFile("rocky-vault.repo")
                : addReposFromFile("rocky.repo");
            break;
        case OS::Distro::AlmaLinux:
            addReposFromFile("almalinux.repo");
            break;
    }

    addReposFromFile("epel.repo");
    addReposFromFile("OpenHPC.repo");

    return repos;
}

void XCAT::install()
{
    using namespace opencattus::utils;
    LOG_INFO("Setting up compute node images... This may take a while");
    constexpr auto provisionerName = "xCAT";
    const auto opts = singleton::options();
    const auto osinfo = singleton::os();
    auto repos = singleton::repos();
    repos->enable("xcat-core");
    repos->enable("xcat-dep");

    NFS networkFileSystem = NFS("pub", "/opt/ohpc",
        cluster()
            ->getHeadnode()
            .getConnection(Network::Profile::Management)
            .getAddress(),
        "ro,no_subtree_check");
    const auto nfsInstallScript
        = networkFileSystem.installScript(cluster()->getHeadnode().getOS());

    installPackages();

    // TODO: CFL nfsInstallScript depends on provisioner here, double check
    // NFS requires /install and /tftpboot folders
    singleton::runner()->run(nfsInstallScript);

    LOG_INFO("[{}] Patching the provisioner", provisionerName)
    patchInstall();

    LOG_INFO("[{}] Setting up the provisioner", provisionerName)
    setup();
    const auto imageType = XCAT::ImageType::Netboot;
    const auto nodeType = XCAT::NodeType::Compute;

    opts->maybeStopAfterStep("provisioner-setup");
    const auto imageInstallArgs = getImageInstallArgs(imageType, nodeType);

    // Customizations to the image
    const auto nfsImageInstallScript
        = networkFileSystem.imageInstallScript(osinfo, imageInstallArgs);

    // Image role
    LOG_INFO("[{}] Creating node images", provisionerName);
    createImage(imageType, nodeType,
        { // Customizations to the image
            nfsImageInstallScript });
    opts->maybeStopAfterStep("provisioner-create-image");

    // nodes role
    LOG_INFO("[{}] Adding compute nodes", provisionerName)
    addNodes();

    LOG_INFO("[{}] Setting up image on nodes", provisionerName)
    setNodesImage();

    LOG_INFO("[{}] Setting up boot settings via IPMI, if available",
        provisionerName);
    setNodesBoot();
    resetNodes();

    // Fix slurmctld: error: Check for out of sync clocks
    LOG_INFO("Synchronizing clocks");
    singleton::osservice()->restartService("chronyd");
}

};

TEST_CASE("getOSImageDistroVersion uses node OS metadata")
{
    CHECK(getOSImageDistroVersion(OS(OS::Distro::Rocky, OS::Platform::el8, 10))
        == "rocky8.10");
    CHECK(getOSImageDistroVersion(OS(OS::Distro::RHEL, OS::Platform::el8, 10))
        == "rhels8.10");
    CHECK(getOSImageDistroVersion(OS(OS::Distro::OL, OS::Platform::el8, 10))
        == "ol8.10.0");
    CHECK(getOSImageDistroVersion(OS(OS::Distro::Rocky, OS::Platform::el9, 7))
        == "rocky9.7");
    CHECK(getOSImageDistroVersion(OS(OS::Distro::RHEL, OS::Platform::el9, 7))
        == "rhels9.7");
    CHECK(getOSImageDistroVersion(OS(OS::Distro::OL, OS::Platform::el9, 7))
        == "ol9.7.0");
}

TEST_CASE("buildEnterpriseLinuxTemplateAliasCommands uses explicit EL releases")
{
    const auto el8Commands = buildEnterpriseLinuxTemplateAliasCommands(
        OS(OS::Distro::Rocky, OS::Platform::el8, 10));
    const auto el9Commands = buildEnterpriseLinuxTemplateAliasCommands(
        OS(OS::Distro::Rocky, OS::Platform::el9, 7));

    CHECK_FALSE(el8Commands.empty());
    CHECK_FALSE(el9Commands.empty());
    CHECK(std::ranges::any_of(el8Commands, [](const auto& command) {
        return command.contains("compute.rhels8.x86_64.pkglist")
            && command.contains("compute.rocky8.x86_64.pkglist");
    }));
    CHECK(std::ranges::any_of(el9Commands, [](const auto& command) {
        return command.contains("compute.rhels9.x86_64.pkglist")
            && command.contains("compute.rocky9.x86_64.pkglist");
    }));
}

TEST_CASE("getKernelPackagesForGenimage uses explicit EL releases")
{
    const auto el8Packages = getKernelPackagesForGenimage(
        OS(OS::Distro::Rocky, OS::Platform::el8, 10), "4.18.0-553.el8_10");
    const auto el9Packages = getKernelPackagesForGenimage(
        OS(OS::Distro::Rocky, OS::Platform::el9, 7), "5.14.0-611.el9_7");
    const auto el10Packages = getKernelPackagesForGenimage(
        OS(OS::Distro::Rocky, OS::Platform::el10, 1), "6.12.0-124.el10_1");

    CHECK(el8Packages == std::vector<std::string> {
                             "kernel-4.18.0-553.el8_10",
                             "kernel-devel-4.18.0-553.el8_10",
                             "kernel-core-4.18.0-553.el8_10",
                             "kernel-modules-4.18.0-553.el8_10",
                         });
    CHECK(el9Packages == std::vector<std::string> {
                             "kernel-5.14.0-611.el9_7",
                             "kernel-devel-5.14.0-611.el9_7",
                             "kernel-core-5.14.0-611.el9_7",
                             "kernel-modules-5.14.0-611.el9_7",
                             "kernel-modules-core-5.14.0-611.el9_7",
                         });
    CHECK(el10Packages == std::vector<std::string> {
                              "kernel-6.12.0-124.el10_1",
                              "kernel-devel-6.12.0-124.el10_1",
                              "kernel-core-6.12.0-124.el10_1",
                              "kernel-modules-6.12.0-124.el10_1",
                              "kernel-modules-core-6.12.0-124.el10_1",
                          });
}

TEST_CASE("formatDHCPInterfaces uses a plain interface selector")
{
    CHECK(formatDHCPInterfaces("oc-mgmt0") == "oc-mgmt0");
}

TEST_CASE("buildDHCPInterfacesCommand passes a raw site assignment")
{
    CHECK(buildDHCPInterfacesCommand("oc-mgmt0")
        == "chdef -t site dhcpinterfaces=oc-mgmt0");
}

TEST_CASE("buildPrecreateMyPostscriptsCommand enables pre-generated postscripts")
{
    CHECK(buildPrecreateMyPostscriptsCommand(true)
        == "chdef -t site precreatemypostscripts=YES");
    CHECK(buildPrecreateMyPostscriptsCommand(false)
        == "chdef -t site precreatemypostscripts=NO");
}

TEST_CASE("buildIpmitoolCommand quotes BMC credentials")
{
    CHECK(buildIpmitoolCommand("192.0.2.10", "admin", "pa'ss",
              "chassis power reset")
        == "ipmitool -I lanplus -H '192.0.2.10' -U 'admin' -P "
           "'pa'\"'\"'ss' chassis power reset");
}

TEST_CASE("getLocalOtherPkgRepoPath matches xCAT local repo layout")
{
    const OS el8NodeOS(
        OS::Distro::Rocky, OS::Platform::el8, 10, OS::Arch::x86_64);
    CHECK(getLocalOtherPkgRepoPath(el8NodeOS)
        == "/install/post/otherpkgs/rocky8.10/x86_64");

    const OS nodeOS(OS::Distro::Rocky, OS::Platform::el9, 7, OS::Arch::x86_64);
    CHECK(getLocalOtherPkgRepoPath(nodeOS)
        == "/install/post/otherpkgs/rocky9.7/x86_64");
}

TEST_CASE("buildXcatInfinibandPlan uses inbox packages for EL8 compute nodes")
{
    const auto plan = buildXcatInfinibandPlan(
        OFED(OFED::Kind::Inbox, ""),
        OS(OS::Distro::Rocky, OS::Platform::el8, 10, OS::Arch::x86_64),
        std::nullopt, "4.18.0-553.75.1.el8_10.x86_64");
    const std::vector<std::string> expectedPackages { "@infiniband" };

    CHECK(plan.otherPackages == expectedPackages);
    CHECK_FALSE(plan.kernelVersion.has_value());
    CHECK_FALSE(plan.localRepoName.has_value());
}

TEST_CASE("buildXcatInfinibandPlan stages DOCA packages for EL8 compute nodes")
{
    const auto plan = buildXcatInfinibandPlan(
        OFED(OFED::Kind::Doca, "latest-2.9-LTS"),
        OS(OS::Distro::Rocky, OS::Platform::el8, 10, OS::Arch::x86_64),
        std::nullopt, "4.18.0-553.75.1.el8_10.x86_64");
    const std::vector<std::string> expectedPackages {
        "mlnx-ofa_kernel",
        "doca-ofed",
    };

    CHECK(plan.otherPackages == expectedPackages);
    REQUIRE(plan.kernelVersion.has_value());
    CHECK(plan.kernelVersion.value() == "4.18.0-553.75.1.el8_10.x86_64");
    REQUIRE(plan.localRepoName.has_value());
    CHECK(plan.localRepoName.value()
        == "doca-kernel-4.18.0-553.75.1.el8_10.x86_64");
}

TEST_CASE("buildXcatInfinibandPlan keeps explicit EL9 kernel pinning")
{
    const auto plan = buildXcatInfinibandPlan(
        OFED(OFED::Kind::Doca, "latest"),
        OS(OS::Distro::Rocky, OS::Platform::el9, 7, OS::Arch::x86_64),
        std::optional<std::string_view>("5.14.0-570.28.1.el9_6.x86_64"),
        "5.14.0-570.24.1.el9_6.x86_64");

    REQUIRE(plan.kernelVersion.has_value());
    CHECK(plan.kernelVersion.value() == "5.14.0-570.28.1.el9_6.x86_64");
    REQUIRE(plan.localRepoName.has_value());
    CHECK(plan.localRepoName.value()
        == "doca-kernel-5.14.0-570.28.1.el9_6.x86_64");
}

TEST_CASE("buildXcatInfinibandPlan rejects EL10 compute-node staging")
{
    CHECK_THROWS_WITH_AS(
        [&] {
            buildXcatInfinibandPlan(
                OFED(OFED::Kind::Doca, "latest"),
                OS(OS::Distro::Rocky, OS::Platform::el10, 1,
                    OS::Arch::x86_64),
                std::nullopt, "6.12.0-65.el10_1.x86_64");
        }(),
        doctest::Contains("EL10"), std::invalid_argument);
}

TEST_CASE("shellSingleQuote escapes single quotes")
{
    CHECK(shellSingleQuote("labroot") == "'labroot'");
    CHECK(shellSingleQuote("O'Hara") == "'O'\"'\"'Hara'");
}
