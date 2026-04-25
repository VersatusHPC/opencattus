/*
 * Copyright 2021 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib> // setenv / getenv
#include <fstream>
#include <iterator>
#include <list>
#include <optional>
#include <ranges>
#include <set>
#include <thread>
#include <vector>

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
#include <opencattus/utils/enums.h>
#include <opencattus/utils/singleton.h>
#include <opencattus/utils/string.h>

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
            if (nodeOS.getPlatform() == OS::Platform::el9) {
                osimage += ".0";
            }
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
        case OS::Distro::Ubuntu:
            osimage += "ubuntu";
            osimage += nodeOS.getVersion();
            break;
    }
    return osimage;
}

bool isUbuntu2404ComputeImage(const OS& nodeOS)
{
    return nodeOS.getPlatform() == OS::Platform::ubuntu2404;
}

std::vector<std::string> ubuntu2404XcatPkgdirEntries()
{
    return {
        "/install/ubuntu24.04/x86_64",
        "http://archive.ubuntu.com/ubuntu noble main restricted universe "
        "multiverse",
        "http://archive.ubuntu.com/ubuntu noble-updates main restricted "
        "universe multiverse",
        "http://security.ubuntu.com/ubuntu noble-security main restricted "
        "universe multiverse",
    };
}

std::string ubuntu2404OpenHpcOtherpkgdirEntry()
{
    return "[trusted=yes] "
           "https://repos.versatushpc.com.br/openhpc/versatushpc-4/"
           "Ubuntu_24.04/ ./";
}

std::vector<std::string_view> ubuntu2404OpenHpcPackages(
    const std::optional<std::vector<std::string>>& enabledBundles)
{
    constexpr auto bundleSerialLibraries = std::string_view("serial-libs");
    constexpr auto bundleParallelLibraries = std::string_view("parallel-libs");
    constexpr auto bundleIntelOneAPI = std::string_view("intel-oneapi");

    auto packages = std::set<std::string_view> {
        "ohpc-base-compute",
        "ohpc-slurm-client",
        "gnu15-compilers-ohpc",
        "openmpi5-gnu15-ohpc",
        "mpich-ucx-gnu15-ohpc",
        "mvapich2-gnu15-ohpc",
        "lmod-ohpc",
        "lmod-defaults-gnu15-openmpi5-ohpc",
        "ohpc-autotools",
        "hwloc-ohpc",
    };

    const auto bundles = enabledBundles.value_or(
        std::vector<std::string> { std::string(bundleSerialLibraries),
            std::string(bundleParallelLibraries) });

    if (std::ranges::find(bundles, bundleSerialLibraries) != bundles.end()) {
        packages.emplace("ohpc-gnu15-serial-libs");
    }

    if (std::ranges::find(bundles, bundleParallelLibraries) != bundles.end()) {
        packages.emplace("ohpc-gnu15-parallel-libs");
    }

    if (std::ranges::find(bundles, bundleIntelOneAPI) != bundles.end()) {
        packages.emplace("intel-oneapi-toolkit-release-ohpc");
        packages.emplace("intel-compilers-devel-ohpc");
        packages.emplace("intel-mpi-devel-ohpc");
        packages.emplace("ohpc-intel-serial-libs");
        packages.emplace("ohpc-intel-impi-parallel-libs");
    }

    return { packages.begin(), packages.end() };
}

std::string buildUbuntu24OSImageDefinitionCommand(
    const opencattus::services::XCAT::Image& image)
{
    return fmt::format(
        "mkdef -f -t osimage {image} "
        "imagetype=linux "
        "osname=Linux "
        "osvers=ubuntu24.04 "
        "osarch=x86_64 "
        "osdistroname=ubuntu24.04-x86_64 "
        "profile=compute "
        "provmethod=netboot "
        "rootimgdir={rootimg} "
        "pkglist=/opt/xcat/share/xcat/netboot/ubuntu/"
        "compute.ubuntu24.04.x86_64.pkglist "
        "otherpkglist=/install/custom/netboot/compute.otherpkglist "
        "postinstall=/install/custom/netboot/compute.postinstall "
        "synclists=/install/custom/netboot/compute.synclists",
        fmt::arg("image", image.osimage),
        fmt::arg("rootimg", image.chroot.string()));
}

std::string buildUbuntu24CopycdsCompatibilityCommand()
{
    return R"(if [ ! -d /install/ubuntu24.04/x86_64 ]; then
  ubuntu_copycds_dir=$(find /install -maxdepth 1 -type d -name 'ubuntu24.04.*' | sort -V | tail -n 1)
  if [ -n "$ubuntu_copycds_dir" ] && [ -d "$ubuntu_copycds_dir/x86_64" ]; then
    ln -sfnT "$ubuntu_copycds_dir" /install/ubuntu24.04
  fi
fi
test -d /install/ubuntu24.04/x86_64)";
}

std::string patchUbuntu24GenimageOtherpkgdirContent(std::string content)
{
    constexpr std::string_view oldBlock
        = R"(        if ($tempdir =~ /^http.*/) {
            $otherpkgsdir_internet .= "deb " . $tempdir . "\n";
        }
        else {)";
    constexpr std::string_view patchedBlock
        = R"(        if ($tempdir =~ /^(\[[^\]]+\]\s*)?http.*/) {
            $otherpkgsdir_internet .= "deb " . $tempdir . "\n";
        }
        else {)";

    if (content.find(patchedBlock) != std::string::npos) {
        return content;
    }

    const auto pos = content.find(oldBlock);
    if (pos == std::string::npos) {
        throw std::runtime_error(
            "Unable to patch xCAT Ubuntu genimage otherpkgdir handling");
    }

    content.replace(pos, oldBlock.size(), patchedBlock);
    return content;
}

void patchUbuntu24GenimageOtherpkgdirHandling()
{
    if (options()->dryRun) {
        LOG_INFO("Dry Run: Would patch xCAT Ubuntu genimage otherpkgdir "
                 "handling");
        return;
    }

    const std::filesystem::path genimage
        = "/opt/xcat/share/xcat/netboot/ubuntu/genimage";
    std::ifstream input(genimage);
    if (!input) {
        throw std::runtime_error(
            fmt::format("Unable to open xCAT Ubuntu genimage script at {}",
                genimage.string()));
    }

    const std::string original((std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    const auto patched = patchUbuntu24GenimageOtherpkgdirContent(original);
    if (patched == original) {
        LOG_DEBUG("xCAT Ubuntu genimage already accepts apt source options in "
                  "otherpkgdir");
        return;
    }

    std::ofstream output(genimage, std::ios::trunc);
    if (!output) {
        throw std::runtime_error(
            fmt::format("Unable to write xCAT Ubuntu genimage script at {}",
                genimage.string()));
    }
    output << patched;
    if (!output) {
        throw std::runtime_error(
            fmt::format("Unable to update xCAT Ubuntu genimage script at {}",
                genimage.string()));
    }

    LOG_INFO("Patched xCAT Ubuntu genimage to accept apt source options in "
             "otherpkgdir");
}

void cleanStatelessRootImage(const std::filesystem::path& chroot)
{
    if (chroot.empty()) {
        throw std::logic_error(
            "Refusing to clean an empty xCAT root image path");
    }

    const auto normalized = chroot.lexically_normal().string();
    constexpr std::string_view installNetbootPrefix = "/install/netboot/";
    if (!normalized.starts_with(installNetbootPrefix)) {
        throw std::logic_error(
            fmt::format("Refusing to clean unexpected xCAT root image path {}",
                normalized));
    }

    if (options()->dryRun) {
        LOG_INFO("Dry Run: Would remove stale xCAT root image directory {}",
            normalized);
        return;
    }

    if (!std::filesystem::exists(chroot)) {
        return;
    }

    LOG_INFO("Removing stale xCAT root image directory {}", normalized);
    std::error_code error;
    std::filesystem::remove_all(chroot, error);
    if (error) {
        throw std::runtime_error(fmt::format(
            "Unable to remove stale xCAT root image directory {}: {}",
            normalized, error.message()));
    }
}

std::string xcatHttpServiceName(const OS& headnodeOS)
{
    return headnodeOS.getPackageType() == OS::PackageType::DEB ? "apache2"
                                                               : "httpd";
}

std::string xcatDhcpServiceName(const OS& headnodeOS)
{
    return headnodeOS.getPackageType() == OS::PackageType::DEB
        ? "isc-dhcp-server"
        : "dhcpd";
}

std::vector<std::string> buildXcatPackageInstallCommands(
    const OS& headnodeOS, const OS& computeOS)
{
    if (headnodeOS.getPackageType() == OS::PackageType::DEB) {
        return {
            "env DEBIAN_FRONTEND=noninteractive apt install -y xcat",
            "env DEBIAN_FRONTEND=noninteractive apt install -y ipmitool",
            "env DEBIAN_FRONTEND=noninteractive apt install -y debootstrap",
        };
    }

    auto commands = std::vector<std::string> {
        "dnf -y install xCAT",
        "dnf -y install ipmitool",
        "dnf -y install createrepo_c",
    };
    if (computeOS.getPackageType() == OS::PackageType::DEB) {
        commands.emplace_back("dnf -y install debootstrap");
    }

    return commands;
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
        boost::split(
            temp, createSymlinkCommand(folder, version), boost::is_any_of(","));
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
    std::vector<std::string_view> otherPackages;
    std::optional<std::string> kernelVersion;
    std::optional<std::string> localRepoName;
};

XcatInfinibandPlan buildXcatInfinibandPlan(const OFED& ofed, const OS& nodeOS,
    std::optional<std::string_view> configuredKernel,
    std::string_view runningKernel)
{
    switch (nodeOS.getPlatform()) {
        case OS::Platform::ubuntu2404:
            if (ofed.getKind() != OFED::Kind::Inbox) {
                throw std::invalid_argument(
                    "xCAT Ubuntu 24.04 compute-node OFED staging only "
                    "supports inbox RDMA packages today");
            }
            return XcatInfinibandPlan {
                .otherPackages = { "rdma-core", "ibverbs-utils",
                    "ibverbs-providers", "infiniband-diags" },
                .kernelVersion = std::nullopt,
                .localRepoName = std::nullopt,
            };
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
                .otherPackages = { },
                .kernelVersion = kernelVersion,
                .localRepoName = fmt::format("doca-kernel-{}", kernelVersion),
            };
        }

        case OFED::Kind::Oracle:
            throw std::logic_error("Oracle RDMA release is not yet supported");
    }

    std::unreachable();
}

std::optional<std::string> selectXcatImageKernelVersion(
    const std::optional<OFED>& ofed, const OS& nodeOS,
    std::optional<std::string_view> configuredKernel,
    std::string_view runningKernel)
{
    if (configuredKernel.has_value()) {
        return std::string(configuredKernel.value());
    }

    if (!ofed.has_value()) {
        return std::nullopt;
    }

    return buildXcatInfinibandPlan(
        ofed.value(), nodeOS, std::nullopt, runningKernel)
        .kernelVersion;
}

std::vector<std::string_view> buildXcatKernelPackageNames(const OS& nodeOS)
{
    switch (nodeOS.getPlatform()) {
        case OS::Platform::el8:
            return { "kernel", "kernel-devel", "kernel-headers", "kernel-core",
                "kernel-modules", "kernel-modules-extra" };
        case OS::Platform::el9:
            return { "kernel", "kernel-devel", "kernel-headers", "kernel-core",
                "kernel-modules", "kernel-modules-core",
                "kernel-modules-extra" };
        case OS::Platform::el10:
            throw std::invalid_argument(
                "xCAT kernel package staging is unsupported on EL10");
        default:
            std::unreachable();
    }
}

std::string buildXcatKernelPackages(
    const OS& nodeOS, std::string_view kernelVersion)
{
    auto output = std::string { };
    for (const auto packageName : buildXcatKernelPackageNames(nodeOS)) {
        if (!output.empty()) {
            output += " ";
        }

        output += fmt::format("{}-{}", packageName, kernelVersion);
    }

    return output;
}

std::string_view rockyKernelPackageRepositoryComponent(
    const OS& nodeOS, std::string_view packageName)
{
    switch (nodeOS.getPlatform()) {
        case OS::Platform::el8:
            return "BaseOS";
        case OS::Platform::el9:
            if (packageName == "kernel-devel"
                || packageName == "kernel-headers") {
                return "AppStream";
            }
            return "BaseOS";
        case OS::Platform::el10:
            throw std::invalid_argument(
                "xCAT Rocky kernel package URLs are unsupported on EL10");
        default:
            std::unreachable();
    }
}

std::optional<std::string> buildRockyKernelPackageUrl(const OS& nodeOS,
    std::string_view packageName, std::string_view kernelVersion)
{
    if (nodeOS.getDistro() != OS::Distro::Rocky) {
        return std::nullopt;
    }

    return fmt::format(
        "https://download.rockylinux.org/pub/rocky/{version}/{repo}/{arch}/"
        "os/Packages/k/{packageName}-{kernelVersion}.rpm",
        fmt::arg("version", nodeOS.getVersion()),
        fmt::arg(
            "repo", rockyKernelPackageRepositoryComponent(nodeOS, packageName)),
        fmt::arg("arch", opencattus::utils::enums::toString(nodeOS.getArch())),
        fmt::arg("packageName", packageName),
        fmt::arg("kernelVersion", kernelVersion));
}

std::optional<std::string> buildRockyXcatKernelDownloadFallbackCommand(
    const OS& nodeOS, std::string_view kernelVersion,
    std::string_view destinationDirectory)
{
    if (nodeOS.getDistro() != OS::Distro::Rocky) {
        return std::nullopt;
    }

    auto fallbackDownloads = std::string { };
    for (const auto packageName : buildXcatKernelPackageNames(nodeOS)) {
        const auto packageUrl
            = buildRockyKernelPackageUrl(nodeOS, packageName, kernelVersion);
        if (!packageUrl.has_value()) {
            return std::nullopt;
        }

        fallbackDownloads += fmt::format(
            "    curl -fsSL --retry 5 --retry-delay 2 --retry-connrefused "
            "-o {destdir}/{packageName}-{kernelVersion}.rpm {packageUrl}\n",
            fmt::arg("destdir", destinationDirectory),
            fmt::arg("packageName", packageName),
            fmt::arg("kernelVersion", kernelVersion),
            fmt::arg("packageUrl", packageUrl.value()));
    }

    return fmt::format(R"(
if ! dnf download {kernelPackages} --destdir {destinationDirectory}; then
    rm -f {destinationDirectory}/*.rpm
{fallbackDownloads}fi
)",
        fmt::arg(
            "kernelPackages", buildXcatKernelPackages(nodeOS, kernelVersion)),
        fmt::arg("destinationDirectory", destinationDirectory),
        fmt::arg("fallbackDownloads", fallbackDownloads));
}

std::optional<std::string> buildRockyXcatRepoUpstreamUrl(
    const OS& osinfo, std::string_view repoId)
{
    if (osinfo.getDistro() != OS::Distro::Rocky) {
        return std::nullopt;
    }

    const auto root = fmt::format(
        "https://dl.rockylinux.org/pub/rocky/{}", osinfo.getMajorVersion());
    const auto arch = opencattus::utils::enums::toString(osinfo.getArch());

    if (repoId == "appstream") {
        return fmt::format("{}/AppStream/{}/os/", root, arch);
    }
    if (repoId == "baseos") {
        return fmt::format("{}/BaseOS/{}/os/", root, arch);
    }
    if (repoId == "powertools") {
        return fmt::format("{}/PowerTools/{}/os/", root, arch);
    }
    if (repoId == "crb") {
        return fmt::format("{}/CRB/{}/os/", root, arch);
    }

    return std::nullopt;
}

std::optional<std::string> extractOpenHpcVersionFromMirrorUrl(
    std::string_view currentUri)
{
    constexpr auto marker = std::string_view("/openhpc/");
    const auto begin = currentUri.find(marker);
    if (begin == std::string_view::npos) {
        return std::nullopt;
    }

    const auto versionBegin = begin + marker.size();
    const auto versionEnd = currentUri.find('/', versionBegin);
    if (versionEnd == std::string_view::npos) {
        return std::nullopt;
    }

    return std::string(
        currentUri.substr(versionBegin, versionEnd - versionBegin));
}

std::optional<std::string> buildXcatRepoUpstreamUrl(
    std::string_view repoId, std::string_view currentUri, const OS& osinfo)
{
    if (const auto rockyUpstream
        = buildRockyXcatRepoUpstreamUrl(osinfo, repoId)) {
        return rockyUpstream;
    }

    const auto arch = opencattus::utils::enums::toString(osinfo.getArch());
    if (repoId == "epel") {
        return fmt::format(
            "https://download.fedoraproject.org/pub/epel/{}/Everything/{}/",
            osinfo.getMajorVersion(), arch);
    }

    if (repoId != "OpenHPC" && repoId != "OpenHPC-Updates") {
        return std::nullopt;
    }

    const auto ohpcVersion = extractOpenHpcVersionFromMirrorUrl(currentUri);
    if (!ohpcVersion.has_value()) {
        return std::nullopt;
    }

    if (repoId == "OpenHPC") {
        return fmt::format("https://repos.openhpc.community/OpenHPC/{}/EL_{}/",
            ohpcVersion.value(), osinfo.getMajorVersion());
    }

    return fmt::format(
        "https://repos.openhpc.community/OpenHPC/{}/updates/EL_{}/",
        ohpcVersion.value(), osinfo.getMajorVersion());
}

std::string resolveXcatOsImageRepoUrl(
    const opencattus::services::repos::IRepository& repo, const OS& osinfo)
{
    const auto uri = opencattus::utils::optional::unwrap(repo.uri(),
        "Expecting value for repository URI {}, found None, check {}",
        repo.id(), repo.source());
    const auto opts = opencattus::utils::singleton::options();
    const auto mirrorBaseUrl
        = opencattus::utils::string::rstrip(opts->mirrorBaseUrl, "/");

    if (!opts->enableMirrors || mirrorBaseUrl.empty()
        || !uri.starts_with(mirrorBaseUrl)) {
        return uri;
    }

    const auto metadataUrl = fmt::format(
        "{}/repodata/repomd.xml", opencattus::utils::string::rstrip(uri, "/"));
    const auto metadataStatus
        = opencattus::functions::getHttpStatus(metadataUrl, 1);
    if (metadataStatus.starts_with("2")) {
        return uri;
    }

    const auto upstreamUrl = buildXcatRepoUpstreamUrl(repo.id(), uri, osinfo);
    if (!upstreamUrl.has_value()) {
        LOG_WARN("xCAT osimage repo {} at {} returned HTTP {} and has no "
                 "upstream fallback, keeping the configured URL",
            repo.id(), uri, metadataStatus);
        return uri;
    }

    LOG_WARN("xCAT osimage repo {} at {} returned HTTP {}; falling back to {}",
        repo.id(), uri, metadataStatus, upstreamUrl.value());
    return upstreamUrl.value();
}

std::string buildXcatDocaPostInstallScript(std::string_view kernelVersion,
    std::string_view docaRepoUrl, std::string_view localRepoUrl)
{
    return fmt::format(R"(dnf -y --nogpgcheck \
  --installroot=$IMG_ROOTIMGDIR \
  --disablerepo='*' \
  --enablerepo=appstream \
  --enablerepo=baseos \
  --enablerepo=crb \
  --repofrompath=kernelpin,file:///install/kernels/{kernelVersion} \
  install \
  kernel-devel-{kernelVersion} \
  kernel-headers-{kernelVersion} \
  kernel-modules-extra-{kernelVersion}
install -d -m 0755 $IMG_ROOTIMGDIR/lib/modules/{kernelVersion}
if [ -d $IMG_ROOTIMGDIR/usr/src/kernels/{kernelVersion} ]; then
  ln -sfn /usr/src/kernels/{kernelVersion} $IMG_ROOTIMGDIR/lib/modules/{kernelVersion}/build
  ln -sfn /usr/src/kernels/{kernelVersion} $IMG_ROOTIMGDIR/lib/modules/{kernelVersion}/source
fi
if [ ! -e $IMG_ROOTIMGDIR/lib/modules/{kernelVersion}/build ]; then
  echo "Expected kernel build tree for {kernelVersion} inside the xCAT root image" >&2
  exit 1
fi
dnf -y --nogpgcheck \
  --installroot=$IMG_ROOTIMGDIR \
  --disablerepo='*' \
  --enablerepo=appstream \
  --enablerepo=baseos \
  --enablerepo=crb \
  --repofrompath=docaimage,{docaRepoUrl} \
  --repofrompath=dockernelimg,{localRepoUrl} \
  install \
  --exclude=kernel \
  --exclude=kernel-core \
  --exclude=kernel-modules \
  --exclude=kernel-modules-core \
  --exclude=kernel-devel\* \
  --exclude=kernel-headers\* \
  doca-extra doca-ofed mlnx-ofa_kernel
if ! chroot $IMG_ROOTIMGDIR rpm -q doca-extra doca-ofed mlnx-ofa_kernel >/dev/null 2>&1; then
  echo "Expected the xCAT DOCA postinstall to install doca-extra, doca-ofed, and mlnx-ofa_kernel" >&2
  exit 1
fi
cleanup_doca_chroot() {{
  mountpoint -q $IMG_ROOTIMGDIR/dev && umount -R -l $IMG_ROOTIMGDIR/dev || :
  mountpoint -q $IMG_ROOTIMGDIR/sys && umount -R -l $IMG_ROOTIMGDIR/sys || :
  mountpoint -q $IMG_ROOTIMGDIR/proc && umount -l $IMG_ROOTIMGDIR/proc || :
}}
trap cleanup_doca_chroot EXIT
install -d -m 0755 $IMG_ROOTIMGDIR/proc $IMG_ROOTIMGDIR/sys $IMG_ROOTIMGDIR/dev
mount -t proc proc $IMG_ROOTIMGDIR/proc
mount --rbind /sys $IMG_ROOTIMGDIR/sys
mount --make-rslave $IMG_ROOTIMGDIR/sys
mount --rbind /dev $IMG_ROOTIMGDIR/dev
mount --make-rslave $IMG_ROOTIMGDIR/dev
if chroot $IMG_ROOTIMGDIR rpm -q mlnx-ofa_kernel-dkms >/dev/null 2>&1; then
  chroot $IMG_ROOTIMGDIR dkms autoinstall -k {kernelVersion}
fi
chroot $IMG_ROOTIMGDIR depmod -a {kernelVersion}
chroot $IMG_ROOTIMGDIR dracut --force /boot/initramfs-{kernelVersion}.img {kernelVersion}
cleanup_doca_chroot
trap - EXIT
)",
        fmt::arg("kernelVersion", kernelVersion),
        fmt::arg("docaRepoUrl", docaRepoUrl),
        fmt::arg("localRepoUrl", localRepoUrl));
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

std::string shellSingleQuote(std::string_view value);

std::string buildNetworkDefinitionName(const Network& network)
{
    const auto formatAddress = [](const auto& address) {
        auto formatted = address.to_string();
        std::ranges::replace(formatted, '.', '_');
        return formatted;
    };

    return fmt::format("{}-{}", formatAddress(network.getAddress()),
        formatAddress(network.getSubnetMask()));
}

std::string defaultDHCPDynamicRangeFor(const Network& network)
{
    const auto subnetAddress = network.getAddress().to_v4().to_uint();
    const auto subnetMask = network.getSubnetMask().to_v4().to_uint();
    const auto broadcastAddress = subnetAddress | ~subnetMask;
    const auto firstUsableAddress = subnetAddress + 1;
    const auto lastUsableAddress = broadcastAddress - 1;

    if (lastUsableAddress < firstUsableAddress) {
        throw std::invalid_argument(
            "Management network is too small for an xCAT DHCP dynamic range");
    }

    // Keep the discovery pool near the top of the subnet so it stays away
    // from the static node allocations that typically start at the low end.
    const auto preferredRangeEnd = lastUsableAddress > firstUsableAddress + 2
        ? lastUsableAddress - 2
        : lastUsableAddress;
    constexpr std::uint32_t preferredRangeSize = 100;
    const auto availableAddresses = preferredRangeEnd - firstUsableAddress + 1;
    const auto rangeSize = std::min(preferredRangeSize, availableAddresses);
    const auto rangeStart = preferredRangeEnd - rangeSize + 1;

    return fmt::format("{}-{}",
        boost::asio::ip::make_address_v4(rangeStart).to_string(),
        boost::asio::ip::make_address_v4(preferredRangeEnd).to_string());
}

std::string buildDHCPDynamicRangeCommand(
    const Network& network, std::string_view interface)
{
    return fmt::format(
        "chdef -t network {} net={} mask={} mgtifname={} dynamicrange={}",
        buildNetworkDefinitionName(network), network.getAddress().to_string(),
        network.getSubnetMask().to_string(), interface,
        defaultDHCPDynamicRangeFor(network));
}

std::string buildPrecreateMyPostscriptsCommand(bool enabled)
{
    return fmt::format(
        "chdef -t site precreatemypostscripts={}", enabled ? "YES" : "NO");
}

std::string buildXCATCredentialReadinessCheckCommand()
{
    return "bash -lc \"[ -s /etc/xcat/ca/ca-cert.pem ] && "
           "[ -s /etc/xcat/ca/private/ca-key.pem ] && "
           "[ -s /etc/xcat/cert/server-cred.pem ] && "
           "[ -s /root/.xcat/client-cred.pem ] && "
           "[ -s /home/conserver/.xcat/client-cred.pem ]\"";
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
        opencattus::functions::abortif(
            nodePassword.value() != firstPassword.value(),
            "xCAT stateless images require the same node_root_password for "
            "all compute nodes; {} differs",
            node.getHostname());
    }

    return firstPassword.value();
}

std::string buildSetRootPasswordPostinstallSnippet(std::string_view password)
{
    const auto quotedPassword = shellSingleQuote(password);

    return fmt::format(
        "if [ -x \"$IMG_ROOTIMGDIR/usr/sbin/chpasswd\" ]; then\n"
        "  printf 'root:%s\\n' {} | chroot \"$IMG_ROOTIMGDIR\" "
        "/usr/sbin/chpasswd || exit 1\n"
        "elif [ -x \"$IMG_ROOTIMGDIR/sbin/chpasswd\" ]; then\n"
        "  printf 'root:%s\\n' {} | chroot \"$IMG_ROOTIMGDIR\" "
        "/sbin/chpasswd || exit 1\n"
        "else\n"
        "  echo \"Unable to set root password: chpasswd not found in xCAT "
        "image\" >&2\n"
        "  exit 1\n"
        "fi\n",
        quotedPassword, quotedPassword);
}

opencattus::services::ScriptBuilder buildFinalizeStatelessRootImageScript(
    const OS& os, const std::filesystem::path& rootImage,
    std::string_view password)
{
    opencattus::services::ScriptBuilder builder(os);
    builder.addCommand("# Set xCAT system root password")
        .addCommand("password={}", shellSingleQuote(password))
        .addCommand("chtab key=system passwd.username=root "
                    "passwd.password=\"$password\" passwd.cryptmethod=sha512")
        .addCommand("# Set xCAT root image root password")
        .addCommand("rootimg={}", shellSingleQuote(rootImage.string()))
        .addCommand(R"(rootfs=
for candidate in "$rootimg" "$rootimg/rootimg"; do
  if [ -x "$candidate/usr/sbin/chpasswd" ] || [ -x "$candidate/sbin/chpasswd" ]; then
    rootfs="$candidate"
    break
  fi
done
if [ -z "$rootfs" ]; then
  echo "Unable to set root password: chpasswd not found in xCAT image" >&2
  exit 1
fi
if [ -x "$rootfs/usr/sbin/chpasswd" ]; then
  printf 'root:%s\n' "$password" | chroot "$rootfs" /usr/sbin/chpasswd
elif [ -x "$rootfs/sbin/chpasswd" ]; then
  printf 'root:%s\n' "$password" | chroot "$rootfs" /sbin/chpasswd
else
  echo "Unable to set root password: chpasswd not found in xCAT image" >&2
  exit 1
fi)");

    return builder;
}

std::string buildIpmitoolCommand(std::string_view address,
    std::string_view username, std::string_view password,
    std::string_view subcommand)
{
    return fmt::format("ipmitool -I lanplus -H {} -U {} -P {} {}",
        shellSingleQuote(address), shellSingleQuote(username),
        shellSingleQuote(password), subcommand);
}

int runDirectIpmiCommand(
    std::string_view description, std::string_view subcommand)
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
            shellSingleQuote(
                fmt::format("Trying direct IPMI fallback for {} on {}",
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

void runIpmiCommandWithFallback(std::string_view xcatCommand,
    std::string_view subcommand, std::string_view description)
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

std::string buildBmcNodeSelector(
    const std::vector<opencattus::models::Node>& nodes)
{
    std::string selector;

    for (const auto& node : nodes) {
        if (!node.getBMC().has_value()) {
            continue;
        }

        if (!selector.empty()) {
            selector += ",";
        }
        selector += node.getHostname();
    }

    return selector;
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
    auto runner = opencattus::utils::singleton::runner();
    const auto& headnodeOS = cluster()->getHeadnode().getOS();
    const auto& computeOS = cluster()->getComputeNodeOS();

    for (const auto& command :
        buildXcatPackageInstallCommands(headnodeOS, computeOS)) {
        runner->checkCommand(command);
    }
}

void XCAT::patchInstall()
{
    /* Required for EL 9.5
     * Upstream PR: https://github.com/xcat2/xcat-core/pull/7489
     */

    const auto opts = opencattus::utils::singleton::options();
    auto runner = opencattus::utils::singleton::runner();
    const auto& headnodeOS = cluster()->getHeadnode().getOS();
    const auto httpService = xcatHttpServiceName(headnodeOS);
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
    } else {
        LOG_WARN("xCAT Already patched, skipping");
    }

    // xCAT packages already run their initial configuration from package
    // scripts. Re-running the full interactive configuration here is not
    // idempotent and can stall unattended installs after certificate prompts.
    // Restart the provisioner services instead so patched helpers and plugin
    // code are picked up without replaying the full bootstrap workflow.
    runner->executeCommand(
        fmt::format("systemctl enable --now xcatd {}", httpService));
    // Some lanes ship xCAT skeleton credential directories in the package
    // payload. The package post-install then skips the actual CA/client/server
    // credential generation because it only checks directory existence, leaving
    // zero-length or missing credentials behind.
    if (runner->executeCommand(buildXCATCredentialReadinessCheckCommand())
        != 0) {
        LOG_WARN("xCAT credentials are incomplete after package init; "
                 "repairing with xcatconfig -c");
        runner->checkCommand("/opt/xcat/sbin/xcatconfig -c");
    }
    runner->executeCommand(
        fmt::format("systemctl restart xcatd {}", httpService));
}

void XCAT::setup() const
{
    auto runner = opencattus::utils::singleton::runner();
    for (int attempt = 1; attempt <= 30; ++attempt) {
        if (runner->executeCommand("/opt/xcat/bin/lsdef -t site") == 0) {
            break;
        }
        if (attempt == 30) {
            throw std::runtime_error(
                "ERROR: xCAT site table never became ready");
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    const auto& managementConnection
        = cluster()->getHeadnode().getConnection(Network::Profile::Management);
    const auto managementInterface
        = managementConnection.getInterface().value();
    const auto& managementNetwork
        = cluster()->getNetwork(Network::Profile::Management);

    setDHCPInterfaces(managementInterface);
    setDHCPDynamicRange(managementNetwork, managementInterface);
    setPrecreateMyPostscripts(true);
    setDomain(cluster()->getDomainName());
}

/* TODO: Maybe create a chdef method to do it cleaner? */
void XCAT::setDHCPInterfaces(std::string_view interface)
{
    auto runner = opencattus::utils::singleton::runner();
    runner->checkCommand(buildDHCPInterfacesCommand(interface));
}

void XCAT::setDHCPDynamicRange(
    const Network& network, std::string_view interface)
{
    auto runner = opencattus::utils::singleton::runner();
    runner->checkCommand(buildDHCPDynamicRangeCommand(network, interface));
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
    constexpr bool shouldReuseExistingImage(bool imageExists, bool skipCopycds)
    {
        return imageExists && !skipCopycds;
    }

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
    opencattus::services::runner::shell::fmt(
        "copycds {}", shellSingleQuote(diskImage.string()));
}

void XCAT::genimage() const
{
    using namespace runner;
    const auto osinfo
        = opencattus::utils::singleton::cluster()->getNodes()[0].getOS();
    const auto configuredKernel = answerfile()->system.kernel;
    const auto runningKernel
        = opencattus::utils::singleton::osservice()->getKernelRunning();
    const auto kernelVersionOpt
        = selectXcatImageKernelVersion(cluster()->getOFED(), osinfo,
            configuredKernel
                ? std::optional<std::string_view>(configuredKernel.value())
                : std::nullopt,
            runningKernel);
    if (osinfo.getPackageType() == OS::PackageType::DEB
        && kernelVersionOpt.has_value()) {
        throw std::invalid_argument(
            "Custom xCAT kernels are not supported for Ubuntu 24.04 images "
            "yet");
    }

    if (!kernelVersionOpt) {
        shell::fmt("genimage {} ", m_stateless.osimage);
        return;
    }

    const auto& kernelVersion = kernelVersionOpt.value();
    const auto& nodeOS = cluster()->getNodes().front().getOS();

    if (configuredKernel.has_value()) {
        LOG_WARN("Using kernel version from the answerfile: {} at "
                 "[system].kernel in {}",
            kernelVersion, answerfile()->path());
    } else {
        LOG_WARN("Using the DOCA compute-node kernel version {} for genimage "
                 "to keep the node image aligned with staged OFED modules",
            kernelVersion);
    }

    LOG_INFO("Customizing the kernel image");
    const auto kernelPackages = buildXcatKernelPackages(osinfo, kernelVersion);
    const auto kernelDirectory
        = fmt::format("/install/kernels/{}", kernelVersion);

    shell::fmt("mkdir -p {}", kernelDirectory);
    if (const auto fallbackCommand
        = buildRockyXcatKernelDownloadFallbackCommand(
            osinfo, kernelVersion, kernelDirectory);
        fallbackCommand.has_value()) {
        shell::cmd(fallbackCommand.value());
    } else {
        shell::fmt(
            "dnf download {} --destdir {}", kernelPackages, kernelDirectory);
    }
    shell::fmt("createrepo {}", kernelDirectory);
    shell::fmt("chdef -t osimage {} -p pkgdir={}", m_stateless.osimage,
        kernelDirectory);
    shell::fmt("genimage {} -k {}", m_stateless.osimage, kernelVersion);
}

void XCAT::packimage() const
{
    opencattus::utils::singleton::runner()->checkCommand(
        fmt::format("packimage {}", m_stateless.osimage));
}

void XCAT::finalizeStatelessRootImage() const
{
    LOG_ASSERT(
        !m_stateless.chroot.empty(), "xCAT stateless root image path is empty");

    const auto script = buildFinalizeStatelessRootImageScript(
        cluster()->getHeadnode().getOS(), m_stateless.chroot,
        getStatelessNodeRootPassword());
    opencattus::utils::singleton::runner()->run(script);
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
    if (cluster()->getNodes().front().getOS().getPackageType()
        == OS::PackageType::DEB) {
        return;
    }

    m_stateless.postinstall.emplace_back(
        fmt::format("echo \"SELINUX=disabled\nSELINUXTYPE=targeted\" > "
                    "$IMG_ROOTIMGDIR/etc/selinux/config\n\n"));
}

void XCAT::configureOpenHPC()
{
    const auto nodeOS = cluster()->getNodes().front().getOS();
    const auto packages = nodeOS.getPackageType() == OS::PackageType::DEB
        ? ubuntu2404OpenHpcPackages(cluster()->getEnabledOpenHPCBundles())
        : std::vector<std::string_view> { "ohpc-base-compute", "lmod-ohpc",
              "lua" };

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
    const auto nodeRootPassword = getStatelessNodeRootPassword();

    // EL9 diskless nodes can reach `multi-user.target` before xCAT's
    // postbootscripts populate SSH material. Seed the authorized key and host
    // keys into the image so sshd can come up on the first boot.
    m_stateless.postinstall.emplace_back(
        buildSetRootPasswordPostinstallSnippet(nodeRootPassword)
        + "install -d -m 0700 $IMG_ROOTIMGDIR/root/.ssh\n"
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
        const auto localRepo
            = functions::createHTTPRepo(plan.localRepoName.value());

        // Create the RPM repository.
        runner->checkCommand(
            fmt::format("bash -c \"cp -v "
                        "/usr/share/doca-host-*/Modules/{}/*.rpm {}\"",
                plan.kernelVersion.value(), localRepo.directory.string()));
        runner->checkCommand(
            fmt::format("createrepo {}", localRepo.directory.string()));

        const auto docaUrl = repoManager->repo("doca")->uri().value();

        // dryRun does not initialize the repositories
        if (!opts->dryRun) {
            runner->checkCommand(fmt::format(
                "bash -c \"chdef -t osimage {} --plus otherpkgdir={}\"",
                m_stateless.osimage, docaUrl));
        }

        // Add the local repository to the stateless image
        runner->checkCommand(
            fmt::format("bash -c \"chdef -t osimage {} --plus otherpkgdir={}\"",
                m_stateless.osimage, localRepo.url));

        m_stateless.postinstall.emplace_back(buildXcatDocaPostInstallScript(
            plan.kernelVersion.value(), docaUrl, localRepo.url));
    }
}

void XCAT::configureSLURM()
{
    // NOTE: hwloc-libs required to fix slurmd
    const auto nodeOS = cluster()->getNodes().front().getOS();
    m_stateless.otherpkgs.emplace_back("ohpc-slurm-client");
    m_stateless.otherpkgs.emplace_back(
        nodeOS.getPackageType() == OS::PackageType::DEB ? "hwloc-ohpc"
                                                        : "hwloc-libs");

    // TODO: Deprecate this for SRV entries on DNS: _slurmctld._tcp 0 100 6817
    const auto slurmdOptionsPath
        = nodeOS.getPackageType() == OS::PackageType::DEB
        ? "/etc/default/slurmd"
        : "/etc/sysconfig/slurmd";
    m_stateless.postinstall.emplace_back(
        fmt::format("install -d $IMG_ROOTIMGDIR/{}\n"
                    "echo SLURMD_OPTIONS=\\\"--conf-server {}\\\" > "
                    "$IMG_ROOTIMGDIR{}\n\n",
            std::filesystem::path(slurmdOptionsPath).parent_path().string(),
            cluster()
                ->getHeadnode()
                .getConnection(Network::Profile::Management)
                .getAddress()
                .to_string(),
            slurmdOptionsPath));

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
        "cat > "
        "$IMG_ROOTIMGDIR/etc/udev/rules.d/99-opencattus-placeholder.rules "
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

    const auto nodeOS = cluster()->getNodes().front().getOS();
    if (nodeOS.getPackageType() == OS::PackageType::RPM) {
        m_stateless.postinstall.emplace_back(
            "chroot $IMG_ROOTIMGDIR systemctl disable firewalld\n");
    } else {
        m_stateless.postinstall.emplace_back(
            "chroot $IMG_ROOTIMGDIR systemctl disable ufw 2>/dev/null || :\n");
    }

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
    const auto nodeOS = cluster()->getNodes().front().getOS();
    if (nodeOS.getPackageType() == OS::PackageType::RPM) {
        const auto localOtherPkgDir = getLocalOtherPkgRepoPath(nodeOS);
        opencattus::services::runner::shell::cmd(
            fmt::format("mkdir -p {} && createrepo_c --update {}",
                shellSingleQuote(localOtherPkgDir.string()),
                shellSingleQuote(localOtherPkgDir.string())));
    }

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
        if (nodeOS.getPackageType() == OS::PackageType::DEB) {
            const auto pkgdirEntries = ubuntu2404XcatPkgdirEntries();
            const auto pkgdir
                = fmt::format("{}", fmt::join(pkgdirEntries, ","));
            opencattus::services::runner::shell::fmt(
                "chdef -t osimage {} pkgdir={}", m_stateless.osimage,
                shellSingleQuote(pkgdir));
        }

        const std::vector<std::string> repos = getxCATOSImageRepos();
        opencattus::services::runner::shell::fmt(
            "chdef -t osimage {} --plus otherpkgdir={}", m_stateless.osimage,
            shellSingleQuote(fmt::format("{}", fmt::join(repos, ","))));
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
    for (const auto& command :
        buildEnterpriseLinuxTemplateAliasCommands(nodeOS)) {
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
    for (const auto& command :
        buildEnterpriseLinuxTemplateAliasCommands(nodeOS)) {
        runner->executeCommand(command);
    }
}

void XCAT::configureUbuntu24()
{
    auto runner = opencattus::utils::singleton::runner();
    patchUbuntu24GenimageOtherpkgdirHandling();
    runner->executeCommand("install -d /opt/xcat/share/xcat/netboot/ubuntu");
    runner->executeCommand(
        R"(bash -c "cat > /opt/xcat/share/xcat/netboot/ubuntu/compute.ubuntu24.04.x86_64.pkglist <<'EOF'
bash
nfs-common
openssl
isc-dhcp-client
libc-bin
linux-image-generic
openssh-server
openssh-client
wget
rsync
busybox-static
gawk
dnsutils
tar
gzip
xz-utils
cpio
chrony
EOF
for suffix in exlist postinstall; do
  target=/opt/xcat/share/xcat/netboot/ubuntu/compute.ubuntu24.04.x86_64.${suffix}
  if [ -e /opt/xcat/share/xcat/netboot/ubuntu/compute.ubuntu20.04.x86_64.${suffix} ]; then
    ln -sf /opt/xcat/share/xcat/netboot/ubuntu/compute.ubuntu20.04.x86_64.${suffix} \"$target\"
  elif [ -e /opt/xcat/share/xcat/netboot/ubuntu/compute.${suffix} ]; then
    ln -sf /opt/xcat/share/xcat/netboot/ubuntu/compute.${suffix} \"$target\"
  fi
done")");
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
        case OS::Platform::ubuntu2404:
            configureUbuntu24();
            break;
        case OS::Platform::el8:
            configureEL8();
            break;
        case OS::Platform::el9:
            configureEL9();
            break;
        case OS::Platform::el10:
            throw std::logic_error(
                "xCAT image generation is unsupported on EL10");
        default:
            std::unreachable();
    }

    generateOSImageName(imageType, nodeType);
    generateOSImagePath(imageType, nodeType);

    const auto opts = opencattus::utils::singleton::options();
    const auto imageExists_ = imageExists(m_stateless.osimage);
    const auto reuseExistingImage
        = shouldReuseExistingImage(imageExists_, opts->shouldSkip("copycds"));
    const auto runner = opencattus::utils::singleton::runner();
    if (!reuseExistingImage) {
        if (opts->shouldSkip("copycds")) {
            // Remove rootfs and cleanup otherpkgs and postinstall scripts
            runner->executeCommand(
                fmt::format("bash -c \"rm -rf {} && echo > {} && echo > {}\"",
                    m_stateless.chroot,
                    "/install/custom/netboot/compute.otherpkglist",
                    "/install/custom/netboot/compute.postinstall"));
        } else {
            copycds(cluster()->getDiskImage().getPath());
            if (isUbuntu2404ComputeImage(
                    cluster()->getNodes().front().getOS())) {
                opencattus::services::runner::shell::cmd(
                    buildUbuntu24CopycdsCompatibilityCommand());
            }
        }
        cleanStatelessRootImage(m_stateless.chroot);
        if (isUbuntu2404ComputeImage(cluster()->getNodes().front().getOS())) {
            runner->executeCommand(
                buildUbuntu24OSImageDefinitionCommand(m_stateless));
        }

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
    } else {
        LOG_INFO("Reusing xCAT image {}, skipping genimage and repacking the "
                 "existing root image",
            m_stateless.osimage);
    }

    finalizeStatelessRootImage();
    packimage();
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
    runner->checkCommand(fmt::format("systemctl restart {}",
        xcatDhcpServiceName(cluster()->getHeadnode().getOS())));
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
    const auto nodes = buildBmcNodeSelector(cluster()->getNodes());
    if (nodes.empty()) {
        LOG_INFO("Skipping xCAT boot setting because no compute node has BMC");
        return;
    }

    runIpmiCommandWithFallback(fmt::format("rsetboot {} net", nodes),
        "chassis bootdev pxe", "PXE boot configuration");
}

void XCAT::resetNodes()
{
    const auto nodes = buildBmcNodeSelector(cluster()->getNodes());
    if (nodes.empty()) {
        LOG_INFO("Skipping xCAT node reset because no compute node has BMC");
        return;
    }

    runIpmiCommandWithFallback(fmt::format("rpower {} reset", nodes),
        "chassis power reset", "node reset");
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
    const auto& osinfo = cluster()->getComputeNodeOS();
    if (osinfo.getPackageType() == OS::PackageType::DEB) {
        return { ubuntu2404OpenHpcOtherpkgdirEntry() };
    }

    const auto repoManager = opencattus::utils::singleton::repos();
    std::vector<std::string> repos;
    const auto addReposFromFile = [&](const std::string& filename) {
        for (auto& repo : repoManager->repoFile(filename)) {
            if (repo->enabled()) {
                repos.emplace_back(resolveXcatOsImageRepoUrl(*repo, osinfo));
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
        case OS::Distro::Ubuntu:
            std::unreachable();
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
    const auto computeOS = cluster()->getComputeNodeOS();
    auto repos = singleton::repos();
    if (cluster()->getHeadnode().getOS().getPackageType()
        == OS::PackageType::RPM) {
        repos->enable("xcat-core");
        repos->enable("xcat-dep");
    }

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
        = networkFileSystem.imageInstallScript(computeOS, imageInstallArgs);

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
        == "rhels9.7.0");
    CHECK(getOSImageDistroVersion(OS(OS::Distro::OL, OS::Platform::el9, 7))
        == "ol9.7.0");
    CHECK(getOSImageDistroVersion(
              OS(OS::Distro::Ubuntu, OS::Platform::ubuntu2404, 0))
        == "ubuntu24.04");
}

TEST_CASE("ubuntu2404XcatPkgdirEntries uses Noble archive repositories")
{
    const auto entries = ubuntu2404XcatPkgdirEntries();

    CHECK(entries.size() == 4);
    CHECK(entries[0] == "/install/ubuntu24.04/x86_64");
    CHECK(entries[1]
        == "http://archive.ubuntu.com/ubuntu noble main restricted universe "
           "multiverse");
    CHECK(entries[2]
        == "http://archive.ubuntu.com/ubuntu noble-updates main restricted "
           "universe multiverse");
    CHECK(entries[3]
        == "http://security.ubuntu.com/ubuntu noble-security main restricted "
           "universe multiverse");
}

TEST_CASE("ubuntu2404OpenHpcOtherpkgdirEntry uses VersatusHPC OpenHPC")
{
    CHECK(ubuntu2404OpenHpcOtherpkgdirEntry()
        == "[trusted=yes] "
           "https://repos.versatushpc.com.br/openhpc/versatushpc-4/"
           "Ubuntu_24.04/ ./");
}

TEST_CASE("buildUbuntu24OSImageDefinitionCommand defines the xCAT image")
{
    const auto command = buildUbuntu24OSImageDefinitionCommand(
        opencattus::services::XCAT::Image {
            .osimage = "ubuntu24.04-x86_64-netboot-compute",
            .chroot = "/install/netboot/ubuntu24.04/x86_64/compute/rootimg" });

    CHECK(command.contains(
        "mkdef -f -t osimage ubuntu24.04-x86_64-netboot-compute"));
    CHECK(command.contains("osvers=ubuntu24.04"));
    CHECK(command.contains("osdistroname=ubuntu24.04-x86_64"));
    CHECK(command.contains("pkglist=/opt/xcat/share/xcat/netboot/ubuntu/"
                           "compute.ubuntu24.04.x86_64.pkglist"));
    CHECK(command.contains(
        "rootimgdir=/install/netboot/ubuntu24.04/x86_64/compute/rootimg"));
}

TEST_CASE("buildUbuntu24CopycdsCompatibilityCommand links patch-level media")
{
    const auto command = buildUbuntu24CopycdsCompatibilityCommand();

    CHECK(command.contains("/install/ubuntu24.04/x86_64"));
    CHECK(command.contains("ubuntu24.04.*"));
    CHECK(command.contains("ln -sfnT"));
}

TEST_CASE("patchUbuntu24GenimageOtherpkgdirContent accepts apt options")
{
    const std::string original = R"(    foreach my $tempdir (@tempdirarray) {
        if ($tempdir =~ /^http.*/) {
            $otherpkgsdir_internet .= "deb " . $tempdir . "\n";
        }
        else {
            $otherpkgsdir_local = $tempdir;
        }
    }
)";

    const auto patched = patchUbuntu24GenimageOtherpkgdirContent(original);

    CHECK(patched.contains(R"($tempdir =~ /^(\[[^\]]+\]\s*)?http.*/)"));
    CHECK(patchUbuntu24GenimageOtherpkgdirContent(patched) == patched);
}

TEST_CASE("buildXcatPackageInstallCommands uses APT on Ubuntu head nodes")
{
    const auto ubuntu = OS(OS::Distro::Ubuntu, OS::Platform::ubuntu2404, 0);
    const auto commands = buildXcatPackageInstallCommands(ubuntu, ubuntu);

    CHECK(commands
        == std::vector<std::string> {
            "env DEBIAN_FRONTEND=noninteractive apt install -y xcat",
            "env DEBIAN_FRONTEND=noninteractive apt install -y ipmitool",
            "env DEBIAN_FRONTEND=noninteractive apt install -y debootstrap",
        });
}

TEST_CASE("xCAT service helpers use Debian service names on Ubuntu")
{
    const auto ubuntu = OS(OS::Distro::Ubuntu, OS::Platform::ubuntu2404, 0);

    CHECK(xcatHttpServiceName(ubuntu) == "apache2");
    CHECK(xcatDhcpServiceName(ubuntu) == "isc-dhcp-server");
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

    CHECK(el8Packages
        == std::vector<std::string> {
            "kernel-4.18.0-553.el8_10",
            "kernel-devel-4.18.0-553.el8_10",
            "kernel-core-4.18.0-553.el8_10",
            "kernel-modules-4.18.0-553.el8_10",
        });
    CHECK(el9Packages
        == std::vector<std::string> {
            "kernel-5.14.0-611.el9_7",
            "kernel-devel-5.14.0-611.el9_7",
            "kernel-core-5.14.0-611.el9_7",
            "kernel-modules-5.14.0-611.el9_7",
            "kernel-modules-core-5.14.0-611.el9_7",
        });
    CHECK(el10Packages
        == std::vector<std::string> {
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

TEST_CASE("buildNetworkDefinitionName matches xCAT network keys")
{
    Network managementNetwork(Network::Profile::Management);
    managementNetwork.setAddress("192.168.30.0");
    managementNetwork.setSubnetMask("255.255.255.0");

    CHECK(buildNetworkDefinitionName(managementNetwork)
        == "192_168_30_0-255_255_255_0");
}

TEST_CASE("defaultDHCPDynamicRangeFor keeps the discovery pool near the top")
{
    Network managementNetwork(Network::Profile::Management);
    managementNetwork.setAddress("192.168.30.0");
    managementNetwork.setSubnetMask("255.255.255.0");

    CHECK(defaultDHCPDynamicRangeFor(managementNetwork)
        == "192.168.30.153-192.168.30.252");
}

TEST_CASE("defaultDHCPDynamicRangeFor scales to wider management subnets")
{
    Network managementNetwork(Network::Profile::Management);
    managementNetwork.setAddress("172.26.0.0");
    managementNetwork.setSubnetMask("255.255.0.0");

    CHECK(defaultDHCPDynamicRangeFor(managementNetwork)
        == "172.26.255.153-172.26.255.252");
}

TEST_CASE("buildDHCPDynamicRangeCommand updates the xCAT network table")
{
    Network managementNetwork(Network::Profile::Management);
    managementNetwork.setAddress("192.168.30.0");
    managementNetwork.setSubnetMask("255.255.255.0");

    CHECK(buildDHCPDynamicRangeCommand(managementNetwork, "oc-mgmt0")
        == "chdef -t network 192_168_30_0-255_255_255_0 "
           "net=192.168.30.0 mask=255.255.255.0 "
           "mgtifname=oc-mgmt0 "
           "dynamicrange=192.168.30.153-192.168.30.252");
}

TEST_CASE(
    "buildPrecreateMyPostscriptsCommand enables pre-generated postscripts")
{
    CHECK(buildPrecreateMyPostscriptsCommand(true)
        == "chdef -t site precreatemypostscripts=YES");
    CHECK(buildPrecreateMyPostscriptsCommand(false)
        == "chdef -t site precreatemypostscripts=NO");
}

TEST_CASE("buildIpmitoolCommand quotes BMC credentials")
{
    CHECK(buildIpmitoolCommand(
              "192.0.2.10", "admin", "pa'ss", "chassis power reset")
        == "ipmitool -I lanplus -H '192.0.2.10' -U 'admin' -P "
           "'pa'\"'\"'ss' chassis power reset");
}

TEST_CASE("buildBmcNodeSelector includes only nodes with BMC")
{
    using opencattus::models::CPU;
    using opencattus::models::Node;
    using opencattus::models::OS;

    auto makeNode = [](std::string_view hostname, bool includeBMC) {
        OS os(OS::Distro::Rocky, OS::Platform::el9, 7);
        CPU cpu(1, 2, 1);
        std::list<Connection> connections;

        std::optional<BMC> bmc = std::nullopt;
        if (includeBMC) {
            bmc.emplace(
                "192.168.30.101", "admin", "pa'ss", 0, 9600, BMC::kind::IPMI);
        }

        return Node(hostname, os, cpu, std::move(connections), bmc);
    };

    CHECK(buildBmcNodeSelector({ makeNode("n01", true), makeNode("n02", false),
              makeNode("n03", true) })
        == "n01,n03");
    CHECK(
        buildBmcNodeSelector({ makeNode("n01", false), makeNode("n02", false) })
            .empty());
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
    const auto plan = buildXcatInfinibandPlan(OFED(OFED::Kind::Inbox, ""),
        OS(OS::Distro::Rocky, OS::Platform::el8, 10, OS::Arch::x86_64),
        std::nullopt, "4.18.0-553.75.1.el8_10.x86_64");
    const std::vector<std::string_view> expectedPackages { "@infiniband" };

    CHECK(plan.otherPackages == expectedPackages);
    CHECK_FALSE(plan.kernelVersion.has_value());
    CHECK_FALSE(plan.localRepoName.has_value());
}

TEST_CASE("buildXcatInfinibandPlan stages DOCA packages for EL8 compute nodes")
{
    const auto plan
        = buildXcatInfinibandPlan(OFED(OFED::Kind::Doca, "latest-3.2-LTS"),
            OS(OS::Distro::Rocky, OS::Platform::el8, 10, OS::Arch::x86_64),
            std::nullopt, "4.18.0-553.75.1.el8_10.x86_64");
    const std::vector<std::string_view> expectedPackages { };

    CHECK(plan.otherPackages == expectedPackages);
    REQUIRE(plan.kernelVersion.has_value());
    CHECK(plan.kernelVersion.value() == "4.18.0-553.75.1.el8_10.x86_64");
    REQUIRE(plan.localRepoName.has_value());
    CHECK(plan.localRepoName.value()
        == "doca-kernel-4.18.0-553.75.1.el8_10.x86_64");
}

TEST_CASE("buildXcatInfinibandPlan keeps explicit EL9 kernel pinning")
{
    const auto plan = buildXcatInfinibandPlan(OFED(OFED::Kind::Doca, "latest"),
        OS(OS::Distro::Rocky, OS::Platform::el9, 7, OS::Arch::x86_64),
        std::optional<std::string_view>("5.14.0-570.28.1.el9_6.x86_64"),
        "5.14.0-570.24.1.el9_6.x86_64");

    REQUIRE(plan.kernelVersion.has_value());
    CHECK(plan.kernelVersion.value() == "5.14.0-570.28.1.el9_6.x86_64");
    REQUIRE(plan.localRepoName.has_value());
    CHECK(plan.localRepoName.value()
        == "doca-kernel-5.14.0-570.28.1.el9_6.x86_64");
}

TEST_CASE("selectXcatImageKernelVersion keeps DOCA images on the staged kernel")
{
    const auto kernelVersion = selectXcatImageKernelVersion(
        std::optional<OFED>(OFED(OFED::Kind::Doca, "latest")),
        OS(OS::Distro::Rocky, OS::Platform::el9, 7, OS::Arch::x86_64),
        std::nullopt, "5.14.0-611.41.1.el9_7.x86_64");

    REQUIRE(kernelVersion.has_value());
    CHECK(kernelVersion.value() == "5.14.0-611.41.1.el9_7.x86_64");
}

TEST_CASE("selectXcatImageKernelVersion stays empty without OFED or a pin")
{
    const auto kernelVersion = selectXcatImageKernelVersion(std::nullopt,
        OS(OS::Distro::Rocky, OS::Platform::el9, 7, OS::Arch::x86_64),
        std::nullopt, "5.14.0-611.41.1.el9_7.x86_64");

    CHECK_FALSE(kernelVersion.has_value());
}

TEST_CASE("buildXcatKernelPackages omits kernel-modules-core on EL8")
{
    CHECK(buildXcatKernelPackages(
              OS(OS::Distro::Rocky, OS::Platform::el8, 10, OS::Arch::x86_64),
              "4.18.0-553.el8_10.x86_64")
        == "kernel-4.18.0-553.el8_10.x86_64 "
           "kernel-devel-4.18.0-553.el8_10.x86_64 "
           "kernel-headers-4.18.0-553.el8_10.x86_64 "
           "kernel-core-4.18.0-553.el8_10.x86_64 "
           "kernel-modules-4.18.0-553.el8_10.x86_64 "
           "kernel-modules-extra-4.18.0-553.el8_10.x86_64");
}

TEST_CASE("buildXcatKernelPackages includes kernel-modules-core on EL9")
{
    CHECK(buildXcatKernelPackages(
              OS(OS::Distro::Rocky, OS::Platform::el9, 7, OS::Arch::x86_64),
              "5.14.0-611.41.1.el9_7.x86_64")
        == "kernel-5.14.0-611.41.1.el9_7.x86_64 "
           "kernel-devel-5.14.0-611.41.1.el9_7.x86_64 "
           "kernel-headers-5.14.0-611.41.1.el9_7.x86_64 "
           "kernel-core-5.14.0-611.41.1.el9_7.x86_64 "
           "kernel-modules-5.14.0-611.41.1.el9_7.x86_64 "
           "kernel-modules-core-5.14.0-611.41.1.el9_7.x86_64 "
           "kernel-modules-extra-5.14.0-611.41.1.el9_7.x86_64");
}

TEST_CASE(
    "buildRockyXcatKernelDownloadFallbackCommand stages exact EL9 kernel RPMs")
{
    const auto fallbackCommand = buildRockyXcatKernelDownloadFallbackCommand(
        OS(OS::Distro::Rocky, OS::Platform::el9, 7, OS::Arch::x86_64),
        "5.14.0-611.41.1.el9_7.x86_64",
        "/install/kernels/5.14.0-611.41.1.el9_7.x86_64");

    REQUIRE(fallbackCommand.has_value());
    CHECK(fallbackCommand->contains(
        "if ! dnf download kernel-5.14.0-611.41.1.el9_7.x86_64"));
    CHECK(fallbackCommand->contains(
        "curl -fsSL --retry 5 --retry-delay 2 --retry-connrefused -o "
        "/install/kernels/5.14.0-611.41.1.el9_7.x86_64/"
        "kernel-5.14.0-611.41.1.el9_7.x86_64.rpm "
        "https://download.rockylinux.org/pub/rocky/9.7/BaseOS/x86_64/os/"
        "Packages/k/"
        "kernel-5.14.0-611.41.1.el9_7.x86_64.rpm"));
    CHECK(fallbackCommand->contains(
        "kernel-devel-5.14.0-611.41.1.el9_7.x86_64.rpm "
        "https://download.rockylinux.org/pub/rocky/9.7/AppStream/x86_64/os/"
        "Packages/k/"
        "kernel-devel-5.14.0-611.41.1.el9_7.x86_64.rpm"));
    CHECK(fallbackCommand->contains(
        "kernel-modules-core-5.14.0-611.41.1.el9_7.x86_64.rpm "
        "https://download.rockylinux.org/pub/rocky/9.7/BaseOS/x86_64/os/"
        "Packages/k/"
        "kernel-modules-core-5.14.0-611.41.1.el9_7.x86_64.rpm"));
}

TEST_CASE("buildRockyXcatKernelDownloadFallbackCommand is disabled for "
          "non-Rocky distros")
{
    CHECK_FALSE(buildRockyXcatKernelDownloadFallbackCommand(
        OS(OS::Distro::RHEL, OS::Platform::el9, 7, OS::Arch::x86_64),
        "5.14.0-611.41.1.el9_7.x86_64",
        "/install/kernels/5.14.0-611.41.1.el9_7.x86_64")
            .has_value());
}

TEST_CASE("buildXcatRepoUpstreamUrl maps Rocky mirror repos back to upstream")
{
    const auto osinfo
        = OS(OS::Distro::Rocky, OS::Platform::el9, 7, OS::Arch::x86_64);

    CHECK(buildXcatRepoUpstreamUrl("appstream",
              "http://mirror.local.versatushpc.com.br/rocky/linux/9/"
              "AppStream/x86_64/os/",
              osinfo)
        == "https://dl.rockylinux.org/pub/rocky/9/AppStream/x86_64/os/");
    CHECK(buildXcatRepoUpstreamUrl("baseos",
              "http://mirror.local.versatushpc.com.br/rocky/linux/9/"
              "BaseOS/x86_64/os/",
              osinfo)
        == "https://dl.rockylinux.org/pub/rocky/9/BaseOS/x86_64/os/");
    CHECK(buildXcatRepoUpstreamUrl("crb",
              "http://mirror.local.versatushpc.com.br/rocky/linux/9/"
              "CRB/x86_64/os/",
              osinfo)
        == "https://dl.rockylinux.org/pub/rocky/9/CRB/x86_64/os/");
}

TEST_CASE("buildXcatRepoUpstreamUrl maps EPEL and OpenHPC mirror repos back to "
          "upstream")
{
    const auto osinfo
        = OS(OS::Distro::Rocky, OS::Platform::el9, 7, OS::Arch::x86_64);

    CHECK(buildXcatRepoUpstreamUrl("epel",
              "http://mirror.local.versatushpc.com.br/epel/9/Everything/"
              "x86_64/",
              osinfo)
        == "https://download.fedoraproject.org/pub/epel/9/Everything/x86_64/");
    CHECK(buildXcatRepoUpstreamUrl("OpenHPC",
              "http://mirror.local.versatushpc.com.br/openhpc/3/EL_9/", osinfo)
        == "https://repos.openhpc.community/OpenHPC/3/EL_9/");
    CHECK(buildXcatRepoUpstreamUrl("OpenHPC-Updates",
              "http://mirror.local.versatushpc.com.br/openhpc/3/updates/EL_9/",
              osinfo)
        == "https://repos.openhpc.community/OpenHPC/3/updates/EL_9/");
}

TEST_CASE("buildXcatDocaPostInstallScript finalizes DOCA modules in the image")
{
    const auto script
        = buildXcatDocaPostInstallScript("5.14.0-611.41.1.el9_7.x86_64",
            "https://linux.mellanox.com/public/repo/doca/latest/rhel9/x86_64",
            "http://localhost/repos/doca-kernel-5.14.0-611.41.1.el9_7.x86_64");

    CHECK(script.contains("--installroot=$IMG_ROOTIMGDIR"));
    CHECK(script.contains("--enablerepo=appstream"));
    CHECK(script.contains("--enablerepo=baseos"));
    CHECK(script.contains("--enablerepo=crb"));
    CHECK(script.contains("--repofrompath=kernelpin,file:///install/kernels/"
                          "5.14.0-611.41.1.el9_7.x86_64"));
    CHECK(script.contains("--repofrompath=docaimage,https://linux.mellanox.com/"
                          "public/repo/doca/latest/rhel9/x86_64"));
    CHECK(script.contains("--repofrompath=dockernelimg,http://localhost/repos/"
                          "doca-kernel-5.14.0-611.41.1.el9_7.x86_64"));
    CHECK_FALSE(script.contains("--enablerepo=epel"));
    CHECK_FALSE(script.contains("--enablerepo=OpenHPC"));
    CHECK_FALSE(script.contains("--enablerepo=OpenHPC-Updates"));
    CHECK(script.contains("--exclude=kernel-devel\\*"));
    CHECK(script.contains("--exclude=kernel-headers\\*"));
    CHECK(script.contains("doca-extra doca-ofed mlnx-ofa_kernel"));
    CHECK(script.contains("kernel-headers-5.14.0-611.41.1.el9_7.x86_64"));
    CHECK(script.contains(
        "ln -sfn /usr/src/kernels/5.14.0-611.41.1.el9_7.x86_64 "
        "$IMG_ROOTIMGDIR/lib/modules/5.14.0-611.41.1.el9_7.x86_64/build"));
    CHECK(script.contains("Expected the xCAT DOCA postinstall to install "
                          "doca-extra, doca-ofed, and mlnx-ofa_kernel"));
    CHECK(script.contains("mount -t proc proc $IMG_ROOTIMGDIR/proc"));
    CHECK(script.contains("mount --rbind /sys $IMG_ROOTIMGDIR/sys"));
    CHECK(script.contains("mount --make-rslave $IMG_ROOTIMGDIR/sys"));
    CHECK(script.contains("mount --rbind /dev $IMG_ROOTIMGDIR/dev"));
    CHECK(script.contains("mount --make-rslave $IMG_ROOTIMGDIR/dev"));
    CHECK(script.contains("dkms autoinstall -k 5.14.0-611.41.1.el9_7.x86_64"));
    CHECK(script.contains("depmod -a 5.14.0-611.41.1.el9_7.x86_64"));
    CHECK(script.contains(
        "dracut --force /boot/initramfs-5.14.0-611.41.1.el9_7.x86_64.img "
        "5.14.0-611.41.1.el9_7.x86_64"));
    CHECK_FALSE(script.contains("Expected xCAT otherpkg staging to install "
                                "doca-extra, doca-ofed, and mlnx-ofa_kernel"));
}

TEST_CASE("shouldReuseExistingImage only skips genimage when copycds is active")
{
    CHECK(opencattus::services::shouldReuseExistingImage(true, false));
    CHECK_FALSE(opencattus::services::shouldReuseExistingImage(false, false));
    CHECK_FALSE(opencattus::services::shouldReuseExistingImage(true, true));
}

TEST_CASE("buildXcatInfinibandPlan rejects EL10 compute-node staging")
{
    CHECK_THROWS_WITH_AS(
        [&] {
            buildXcatInfinibandPlan(OFED(OFED::Kind::Doca, "latest"),
                OS(OS::Distro::Rocky, OS::Platform::el10, 1, OS::Arch::x86_64),
                std::nullopt, "6.12.0-65.el10_1.x86_64");
        }(),
        doctest::Contains("EL10"), std::invalid_argument);
}

TEST_CASE("shellSingleQuote escapes single quotes")
{
    CHECK(shellSingleQuote("labroot") == "'labroot'");
    CHECK(shellSingleQuote("O'Hara") == "'O'\"'\"'Hara'");
}

TEST_CASE("buildSetRootPasswordPostinstallSnippet uses absolute chpasswd paths")
{
    const auto script = buildSetRootPasswordPostinstallSnippet("pa'ss");

    CHECK(script.contains("$IMG_ROOTIMGDIR/usr/sbin/chpasswd"));
    CHECK(script.contains("chroot \"$IMG_ROOTIMGDIR\" /usr/sbin/chpasswd"));
    CHECK(script.contains("chroot \"$IMG_ROOTIMGDIR\" /sbin/chpasswd"));
    CHECK(script.contains("'pa'\"'\"'ss'"));
    CHECK(script.contains("|| exit 1"));
}

TEST_CASE("buildFinalizeStatelessRootImageScript fixes the final root image")
{
    const OS osinfo(OS::Distro::Ubuntu, OS::Platform::ubuntu2404, 0);
    const auto script = buildFinalizeStatelessRootImageScript(osinfo,
        "/install/netboot/ubuntu24.04/x86_64/compute/rootimg", "labroot");
    const auto content = script.toString();

    CHECK(content.contains(
        "/install/netboot/ubuntu24.04/x86_64/compute/rootimg"));
    CHECK(content.contains("password='labroot'"));
    CHECK(content.contains("chtab key=system passwd.username=root "
                           "passwd.password=\"$password\" "
                           "passwd.cryptmethod=sha512"));
    CHECK(content.contains("for candidate in \"$rootimg\" "
                           "\"$rootimg/rootimg\""));
    CHECK(content.contains("chroot \"$rootfs\" /usr/sbin/chpasswd"));
    CHECK(content.contains("chroot \"$rootfs\" /sbin/chpasswd"));
    CHECK(content.contains("printf 'root:%s\\n' \"$password\""));
}
