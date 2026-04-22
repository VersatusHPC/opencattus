/*
 * Copyright 2022 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fmt/core.h>

#include <opencattus/functions.h>
#include <opencattus/ofed.h>
#include <opencattus/opencattus.h>
#include <opencattus/services/options.h>
#include <opencattus/services/osservice.h>
#include <opencattus/services/repos.h>
#include <opencattus/services/runner.h>
#include <opencattus/utils/enums.h>
#include <opencattus/utils/singleton.h>
#include <stdexcept>
#include <utility>

#ifdef BUILD_TESTING
#include <doctest/doctest.h>
#else
#define DOCTEST_CONFIG_DISABLE
#include <doctest/doctest.h>
#endif

using opencattus::functions::IRunner;

namespace {

enum class DocaInstallMode { LegacyKernelSupport, RepoDkms };

auto normalizeOFEDVersion(std::string_view version) -> std::string
{
    return opencattus::utils::string::lower(std::string(version));
}

auto isDocaLtsStream(std::string_view version) -> bool
{
    return normalizeOFEDVersion(version).contains("lts");
}

auto parseDocaLtsVersion(std::string_view version)
    -> std::optional<std::pair<int, int>>
{
    const auto normalized = normalizeOFEDVersion(version);
    if (!normalized.contains("lts")) {
        return std::nullopt;
    }

    constexpr std::string_view prefix = "latest-";
    constexpr std::string_view suffix = "-lts";
    const auto prefixPos = normalized.find(prefix);
    const auto suffixPos = normalized.rfind(suffix);
    if (prefixPos == std::string::npos || suffixPos == std::string::npos
        || suffixPos <= prefixPos + prefix.size()) {
        return std::nullopt;
    }

    const auto versionToken = normalized.substr(
        prefixPos + prefix.size(), suffixPos - (prefixPos + prefix.size()));
    const auto dotPos = versionToken.find('.');
    if (dotPos == std::string::npos) {
        return std::nullopt;
    }

    try {
        return std::pair { std::stoi(versionToken.substr(0, dotPos)),
            std::stoi(versionToken.substr(dotPos + 1)) };
    } catch (...) {
        return std::nullopt;
    }
}

auto usesDocaLegacyKernelSupport(std::string_view version) -> bool
{
    if (!isDocaLtsStream(version)) {
        return false;
    }

    if (const auto parsed = parseDocaLtsVersion(version); parsed.has_value()) {
        return parsed.value() < std::pair { 3, 2 };
    }

    return true;
}

auto requestedKernelForOFED() -> std::optional<std::string>
{
    return opencattus::utils::singleton::answerfile()->system.kernel;
}

auto selectDocaInstallMode(const OFED& ofed,
    const opencattus::models::OS& osinfo,
    std::optional<std::string_view> requestedKernel,
    std::string_view runningKernel) -> DocaInstallMode
{
    switch (osinfo.getPlatform()) {
        case opencattus::models::OS::Platform::el8:
        case opencattus::models::OS::Platform::el9:
        case opencattus::models::OS::Platform::el10:
            break;
        default:
            std::unreachable();
    }

    if (requestedKernel.has_value()
        && requestedKernel.value() != runningKernel) {
        return DocaInstallMode::LegacyKernelSupport;
    }

    if (usesDocaLegacyKernelSupport(ofed.getVersion())) {
        return DocaInstallMode::LegacyKernelSupport;
    }

    return DocaInstallMode::RepoDkms;
}

auto buildDocaLegacyPrereqCommand(
    const std::optional<std::string_view> kernelVersion) -> std::string
{
    if (kernelVersion.has_value()) {
        return fmt::format(
            "dnf -y --nogpg install kernel-{kernelVersion} "
            "kernel-devel-{kernelVersion} kernel-headers-{kernelVersion} "
            "doca-extra tar",
            fmt::arg("kernelVersion", kernelVersion.value()));
    }

    return "bash -lc \"dnf -y --nogpg install "
           "kernel-$(uname -r) kernel-devel-$(uname -r) "
           "kernel-headers-$(uname -r) "
           "doca-extra tar\"";
}

auto buildDocaDkmsPrereqCommand(
    const std::optional<std::string_view> kernelVersion) -> std::string
{
    if (kernelVersion.has_value()) {
        return fmt::format(
            "dnf -y --nogpg install dkms gcc make perl mokutil "
            "kernel-devel-{kernelVersion} kernel-headers-{kernelVersion}",
            fmt::arg("kernelVersion", kernelVersion.value()));
    }

    return "dnf -y --nogpg install dkms gcc make perl mokutil "
           "kernel-devel-$(uname -r) kernel-headers-$(uname -r)";
}

auto buildDocaHostInstallCommand() -> std::string
{
    return "dnf install --nogpg -y "
           "--exclude=kernel "
           "--exclude=kernel-core "
           "--exclude=kernel-modules "
           "--exclude=kernel-modules-core "
           "--exclude=kernel-devel\\* "
           "--exclude=kernel-headers\\* "
           "doca-ofed mlnx-fw-updater";
}

auto buildInboxOFEDInstalledCommand(const opencattus::models::OS& osinfo)
    -> std::string
{
    switch (osinfo.getPackageType()) {
        case opencattus::models::OS::PackageType::RPM:
            return "dnf group info \"Infiniband Support\"";
        case opencattus::models::OS::PackageType::DEB:
            return "dpkg-query -W rdma-core ibverbs-providers "
                   "infiniband-diags";
    }

    std::unreachable();
}

auto buildInboxOFEDInstallCommand(const opencattus::models::OS& osinfo)
    -> std::string
{
    switch (osinfo.getPackageType()) {
        case opencattus::models::OS::PackageType::RPM:
            return "Infiniband Support";
        case opencattus::models::OS::PackageType::DEB:
            return "DEBIAN_FRONTEND=noninteractive apt install -y "
                   "rdma-core ibverbs-providers infiniband-diags perftest";
    }

    std::unreachable();
}

auto rockyKernelPackageRepositoryComponent(const opencattus::models::OS& osinfo,
    std::string_view packageName) -> std::string_view
{
    switch (osinfo.getPlatform()) {
        case opencattus::models::OS::Platform::el8:
            return "BaseOS";
        case opencattus::models::OS::Platform::el9:
        case opencattus::models::OS::Platform::el10:
            if (packageName == "kernel-devel"
                || packageName == "kernel-headers") {
                return "AppStream";
            }
            return "BaseOS";
        default:
            std::unreachable();
    }
}

auto buildRockyKernelPackageUrl(const opencattus::models::OS& osinfo,
    std::string_view packageName, std::string_view kernelVersion)
    -> std::optional<std::string>
{
    if (osinfo.getDistro() != opencattus::models::OS::Distro::Rocky) {
        return std::nullopt;
    }

    return fmt::format(
        "https://download.rockylinux.org/pub/rocky/{osversion}/{repo}/{arch}/"
        "os/Packages/k/{packageName}-{kernelVersion}.rpm",
        fmt::arg("osversion", osinfo.getVersion()),
        fmt::arg(
            "repo", rockyKernelPackageRepositoryComponent(osinfo, packageName)),
        fmt::arg("arch", opencattus::utils::enums::toString(osinfo.getArch())),
        fmt::arg("packageName", packageName),
        fmt::arg("kernelVersion", kernelVersion));
}

auto buildRockyLegacyPrereqFallbackCommand(const opencattus::models::OS& osinfo,
    std::string_view kernelVersion) -> std::optional<std::string>
{
    const auto kernelUrl
        = buildRockyKernelPackageUrl(osinfo, "kernel", kernelVersion);
    const auto develUrl
        = buildRockyKernelPackageUrl(osinfo, "kernel-devel", kernelVersion);
    const auto headersUrl
        = buildRockyKernelPackageUrl(osinfo, "kernel-headers", kernelVersion);

    if (!kernelUrl.has_value() || !develUrl.has_value()
        || !headersUrl.has_value()) {
        return std::nullopt;
    }

    return fmt::format("dnf -y --nogpg install doca-extra tar {} {} {}",
        kernelUrl.value(), develUrl.value(), headersUrl.value());
}

auto buildRockyDkmsPrereqFallbackCommand(const opencattus::models::OS& osinfo,
    std::string_view kernelVersion) -> std::optional<std::string>
{
    const auto develUrl
        = buildRockyKernelPackageUrl(osinfo, "kernel-devel", kernelVersion);
    const auto headersUrl
        = buildRockyKernelPackageUrl(osinfo, "kernel-headers", kernelVersion);

    if (!develUrl.has_value() || !headersUrl.has_value()) {
        return std::nullopt;
    }

    return fmt::format(
        "dnf -y --nogpg install dkms gcc make perl mokutil {} {}",
        develUrl.value(), headersUrl.value());
}

auto installDocaPrereqsWithFallback(IRunner& runner,
    std::string_view primaryCommand, std::optional<std::string> fallbackCommand,
    std::string_view kernelVersion) -> void
{
    try {
        runner.checkCommand(std::string(primaryCommand));
    } catch (const std::runtime_error& error) {
        if (!fallbackCommand.has_value()) {
            throw;
        }

        LOG_WARN("Primary exact-kernel prerequisite install for {} failed "
                 "({}); retrying with upstream Rocky package URLs",
            kernelVersion, error.what());
        runner.checkCommand(fallbackCommand.value());
    }
}

auto latestInstalledKernelCore(IRunner& runner) -> std::string
{
    const auto kernels = runner.checkOutput(
        "bash -lc \"rpm -q kernel-core --qf '%{INSTALLTIME} "
        "%{VERSION}-%{RELEASE}.%{ARCH}\\n'\"");

    auto newestKernel = std::string { };
    long long newestInstallTime = 0;
    auto foundKernel = false;

    for (const auto& line : kernels) {
        const auto separator = line.find(' ');
        if (separator == std::string::npos || separator + 1 >= line.size()) {
            continue;
        }

        const auto installTime = std::stoll(line.substr(0, separator));
        if (!foundKernel || installTime > newestInstallTime) {
            newestInstallTime = installTime;
            newestKernel = line.substr(separator + 1);
            foundKernel = true;
        }
    }

    if (!foundKernel || newestKernel.empty()) {
        throw std::runtime_error(
            "Could not determine the latest installed kernel-core package");
    }

    return newestKernel;
}

auto shouldPrimeDocaDkmsForInstalledKernel(
    std::string_view installedKernel, std::string_view runningKernel) -> bool
{
    return installedKernel != runningKernel;
}

auto primeDocaDkmsForInstalledKernel(
    IRunner& runner, std::string_view runningKernel) -> void
{
    const auto installedKernel = latestInstalledKernelCore(runner);
    if (!shouldPrimeDocaDkmsForInstalledKernel(
            installedKernel, runningKernel)) {
        return;
    }

    LOG_WARN("DOCA installed a newer kernel {} while {} is still running; "
             "skipping DKMS priming so current-stream installs remain bound "
             "to the running kernel during validation",
        installedKernel, runningKernel);
}

auto findLatestGeneratedDocaKernelRepoRpm(IRunner& runner) -> std::string
{
    const auto rpm = runner.checkOutput(
        "bash -c \"find /tmp/DOCA*/ -name 'doca-kernel-repo-*.rpm' -printf "
        "'%T@ %p\\n' | sort -nk1 | tail -1 | awk '{print $2}'\"");

    if (rpm.empty() || rpm[0].empty()) {
        throw std::runtime_error(
            "Could not locate the generated DOCA kernel repo RPM under "
            "/tmp/DOCA*/");
    }

    return rpm[0];
}

} // namespace

void OFED::setKind(Kind kind) { m_kind = kind; }

OFED::Kind OFED::getKind() const { return m_kind; }

bool OFED::installed() const
{
    const auto opts = opencattus::utils::singleton::options();
    if (opts->shouldForce("infiniband-install")) {
        return false;
    }

    // Return false so the installation runs on dry run
    if (opts->dryRun) {
        return false;
    }

    auto runner = opencattus::Singleton<IRunner>::get();
    const auto osinfo = opencattus::utils::singleton::os();
    switch (m_kind) {
        case OFED::Kind::Doca:
            if (osinfo.getPackageType()
                == opencattus::models::OS::PackageType::DEB) {
                throw std::runtime_error(
                    "DOCA OFED is only implemented for Enterprise Linux head "
                    "nodes");
            }
            return runner->executeCommand("rpm -q doca-ofed") == 0;
        case OFED::Kind::Inbox:
            return runner->executeCommand(
                       buildInboxOFEDInstalledCommand(osinfo))
                == 0;
        case OFED::Kind::Oracle:
            throw std::logic_error("Not implemented");
    }

    std::unreachable();
}

void OFED::install() const
{
    const auto opts = opencattus::utils::singleton::options();

    if (opts->dryRun) {
        LOG_WARN("Dry-Run: Skiping OFED installation");
        return;
    }

    // Idempotency check
    if (installed()) {
        LOG_WARN("Inifiniband already installed, skipping, use `--force "
                 "infiniband-install` to force");
        return;
    }

    switch (m_kind) {
        case OFED::Kind::Inbox: {
            const auto osinfo = opencattus::utils::singleton::os();
            const auto installCommand = buildInboxOFEDInstallCommand(osinfo);
            if (osinfo.getPackageType()
                == opencattus::models::OS::PackageType::DEB) {
                opencattus::services::runner::shell::cmd(installCommand);
            } else {
                opencattus::utils::singleton::osservice()->groupInstall(
                    installCommand);
            }
            break;
        }

        case OFED::Kind::Doca: {
            auto runner
                = opencattus::Singleton<opencattus::services::IRunner>::get();
            auto repoManager = opencattus::Singleton<
                opencattus::services::repos::RepoManager>::get();
            const auto osservice = opencattus::utils::singleton::osservice();
            const auto osinfo = opencattus::utils::singleton::os();
            const auto requestedKernel = requestedKernelForOFED();
            const auto runningKernel = osservice->getKernelRunning();
            const auto installMode = selectDocaInstallMode(*this, osinfo,
                requestedKernel
                    ? std::optional<std::string_view>(requestedKernel.value())
                    : std::nullopt,
                runningKernel);

            repoManager->enable("doca");
            runner->checkCommand("dnf makecache --repo=doca");

            if (installMode == DocaInstallMode::LegacyKernelSupport) {
                if (requestedKernel) {
                    LOG_WARN("Building DOCA OFED with kernel version from the "
                             "answerfile {} @ [system].kernel: {}",
                        opencattus::utils::singleton::answerfile()->path(),
                        requestedKernel.value());
                } else {
                    LOG_WARN("Using the legacy DOCA kernel-support path for "
                             "DOCA stream {} and running kernel {}",
                        getVersion(), runningKernel);
                }

                const auto kernelVersion
                    = requestedKernel.value_or(runningKernel);
                const auto prereqCommand = buildDocaLegacyPrereqCommand(
                    std::optional<std::string_view>(kernelVersion));
                installDocaPrereqsWithFallback(*runner, prereqCommand,
                    buildRockyLegacyPrereqFallbackCommand(
                        osinfo, kernelVersion),
                    kernelVersion);

                LOG_INFO("Compiling DOCA kernel support, this may take a "
                         "while, use `--skip compile-doca-driver` to skip");
                if (!opts->shouldSkip("compile-doca-driver")) {
                    if (requestedKernel) {
                        runner->checkCommand(fmt::format(
                            "/opt/mellanox/doca/tools/doca-kernel-support -k "
                            "{}",
                            requestedKernel.value()));
                    } else {
                        runner->checkCommand(
                            "/opt/mellanox/doca/tools/doca-kernel-support");
                    }
                }

                runner->checkCommand(fmt::format("dnf install -y {}",
                    findLatestGeneratedDocaKernelRepoRpm(*runner)));
            } else {
                LOG_INFO("Using the DOCA repo + DKMS installation path for "
                         "stream {} and running kernel {}",
                    getVersion(), runningKernel);
                repoManager->enable("epel");
                runner->checkCommand("dnf makecache --repo=epel");
                const auto dkmsPrereqCommand = buildDocaDkmsPrereqCommand(
                    std::optional<std::string_view>(runningKernel));
                installDocaPrereqsWithFallback(*runner, dkmsPrereqCommand,
                    buildRockyDkmsPrereqFallbackCommand(osinfo, runningKernel),
                    runningKernel);
                runner->checkCommand("dnf -y --nogpg install doca-extra tar");

                LOG_INFO("Staging DOCA kernel artifacts for diskless node "
                         "images");
                if (!opts->shouldSkip("compile-doca-driver")) {
                    runner->checkCommand(
                        "/opt/mellanox/doca/tools/doca-kernel-support");
                    runner->checkCommand(fmt::format("dnf install -y {}",
                        findLatestGeneratedDocaKernelRepoRpm(*runner)));
                }
            }

            runner->checkCommand("dnf makecache --repo=doca*");
            runner->checkCommand(buildDocaHostInstallCommand());
            primeDocaDkmsForInstalledKernel(*runner, runningKernel);
            runner->executeCommand("systemctl restart openibd || true");
            // runner->executeCommand("systemctl enable --now opensmd");
        } break;

        case OFED::Kind::Oracle:
            throw std::logic_error("Oracle RDMA release is not yet supported");

            break;
    }
}

TEST_CASE(
    "selectDocaInstallMode keeps EL9 legacy LTS streams on the legacy path")
{
    const OFED ofed(OFED::Kind::Doca, "latest-2.9-LTS");
    const auto mode = selectDocaInstallMode(ofed,
        opencattus::models::OS(opencattus::models::OS::Distro::Rocky,
            opencattus::models::OS::Platform::el9, 7),
        std::nullopt, "5.14.0-570.24.1.el9_6.x86_64");

    CHECK(mode == DocaInstallMode::LegacyKernelSupport);
}

TEST_CASE("selectDocaInstallMode uses DKMS for DOCA 3.2 LTS streams on EL9")
{
    const OFED ofed(OFED::Kind::Doca, "latest-3.2-LTS");
    const auto mode = selectDocaInstallMode(ofed,
        opencattus::models::OS(opencattus::models::OS::Distro::Rocky,
            opencattus::models::OS::Platform::el9, 7),
        std::nullopt, "5.14.0-570.24.1.el9_6.x86_64");

    CHECK(mode == DocaInstallMode::RepoDkms);
}

TEST_CASE("selectDocaInstallMode uses DKMS for DOCA 3.2 LTS streams on EL10")
{
    const OFED ofed(OFED::Kind::Doca, "latest-3.2-LTS");
    const auto mode = selectDocaInstallMode(ofed,
        opencattus::models::OS(opencattus::models::OS::Distro::Rocky,
            opencattus::models::OS::Platform::el10, 1),
        std::nullopt, "6.12.0-65.el10_1.x86_64");

    CHECK(mode == DocaInstallMode::RepoDkms);
}

TEST_CASE("selectDocaInstallMode uses DKMS for current EL8 DOCA streams")
{
    const OFED ofed(OFED::Kind::Doca, "latest");
    const auto mode = selectDocaInstallMode(ofed,
        opencattus::models::OS(opencattus::models::OS::Distro::Rocky,
            opencattus::models::OS::Platform::el8, 10),
        std::nullopt, "4.18.0-553.75.1.el8_10.x86_64");

    CHECK(mode == DocaInstallMode::RepoDkms);
}

TEST_CASE("selectDocaInstallMode uses DKMS for current EL9 DOCA streams")
{
    const OFED ofed(OFED::Kind::Doca, "latest");
    const auto mode = selectDocaInstallMode(ofed,
        opencattus::models::OS(opencattus::models::OS::Distro::Rocky,
            opencattus::models::OS::Platform::el9, 7),
        std::nullopt, "5.14.0-570.24.1.el9_6.x86_64");

    CHECK(mode == DocaInstallMode::RepoDkms);
}

TEST_CASE("selectDocaInstallMode uses DKMS for current EL10 DOCA streams")
{
    const OFED ofed(OFED::Kind::Doca, "latest");
    const auto mode = selectDocaInstallMode(ofed,
        opencattus::models::OS(opencattus::models::OS::Distro::Rocky,
            opencattus::models::OS::Platform::el10, 1),
        std::nullopt, "6.12.0-65.el10_1.x86_64");

    CHECK(mode == DocaInstallMode::RepoDkms);
}

TEST_CASE("selectDocaInstallMode keeps pinned kernels on the legacy path")
{
    const OFED ofed(OFED::Kind::Doca, "latest");
    const auto mode = selectDocaInstallMode(ofed,
        opencattus::models::OS(opencattus::models::OS::Distro::Rocky,
            opencattus::models::OS::Platform::el9, 7),
        std::optional<std::string_view>("5.14.0-570.28.1.el9_6.x86_64"),
        "5.14.0-570.24.1.el9_6.x86_64");

    CHECK(mode == DocaInstallMode::LegacyKernelSupport);
}

TEST_CASE("buildDocaDkmsPrereqCommand uses the running kernel by default")
{
    CHECK(buildDocaDkmsPrereqCommand(std::nullopt)
        == "dnf -y --nogpg install dkms gcc make perl mokutil "
           "kernel-devel-$(uname -r) kernel-headers-$(uname -r)");
}

TEST_CASE("buildDocaLegacyPrereqCommand keeps explicit kernel pinning")
{
    CHECK(buildDocaLegacyPrereqCommand(
              std::optional<std::string_view>("5.14.0-570.28.1.el9_6.x86_64"))
        == "dnf -y --nogpg install "
           "kernel-5.14.0-570.28.1.el9_6.x86_64 "
           "kernel-devel-5.14.0-570.28.1.el9_6.x86_64 "
           "kernel-headers-5.14.0-570.28.1.el9_6.x86_64 "
           "doca-extra tar");
}

TEST_CASE("buildDocaLegacyPrereqCommand uses the running kernel by default")
{
    CHECK(buildDocaLegacyPrereqCommand(std::nullopt)
        == "bash -lc \"dnf -y --nogpg install "
           "kernel-$(uname -r) kernel-devel-$(uname -r) "
           "kernel-headers-$(uname -r) "
           "doca-extra tar\"");
}

TEST_CASE("buildDocaDkmsPrereqCommand keeps explicit installed kernel pinning")
{
    CHECK(buildDocaDkmsPrereqCommand(
              std::optional<std::string_view>("5.14.0-611.41.1.el9_7.x86_64"))
        == "dnf -y --nogpg install dkms gcc make perl mokutil "
           "kernel-devel-5.14.0-611.41.1.el9_7.x86_64 "
           "kernel-headers-5.14.0-611.41.1.el9_7.x86_64");
}

TEST_CASE(
    "buildRockyDkmsPrereqFallbackCommand uses Rocky AppStream URLs on EL9")
{
    const auto fallback = buildRockyDkmsPrereqFallbackCommand(
        opencattus::models::OS(opencattus::models::OS::Distro::Rocky,
            opencattus::models::OS::Platform::el9, 7,
            opencattus::models::OS::Arch::x86_64),
        "5.14.0-611.41.1.el9_7.x86_64");

    REQUIRE(fallback.has_value());
    CHECK(fallback.value().contains(
        "https://download.rockylinux.org/pub/rocky/9.7/AppStream/x86_64/os/"
        "Packages/k/"
        "kernel-devel-5.14.0-611.41.1.el9_7.x86_64.rpm"));
    CHECK(fallback.value().contains(
        "https://download.rockylinux.org/pub/rocky/9.7/AppStream/x86_64/os/"
        "Packages/k/"
        "kernel-headers-5.14.0-611.41.1.el9_7.x86_64.rpm"));
}

TEST_CASE("buildRockyLegacyPrereqFallbackCommand uses Rocky BaseOS and "
          "AppStream URLs")
{
    const auto fallback = buildRockyLegacyPrereqFallbackCommand(
        opencattus::models::OS(opencattus::models::OS::Distro::Rocky,
            opencattus::models::OS::Platform::el8, 10,
            opencattus::models::OS::Arch::x86_64),
        "4.18.0-553.el8_10.x86_64");

    REQUIRE(fallback.has_value());
    CHECK(fallback.value().contains("doca-extra tar"));
    CHECK(fallback.value().contains("https://download.rockylinux.org/pub/rocky/"
                                    "8.10/BaseOS/x86_64/os/Packages/k/"
                                    "kernel-4.18.0-553.el8_10.x86_64.rpm"));
    CHECK(
        fallback.value().contains("https://download.rockylinux.org/pub/rocky/"
                                  "8.10/BaseOS/x86_64/os/Packages/k/"
                                  "kernel-devel-4.18.0-553.el8_10.x86_64.rpm"));
    CHECK(fallback.value().contains(
        "https://download.rockylinux.org/pub/rocky/8.10/BaseOS/x86_64/os/"
        "Packages/k/"
        "kernel-headers-4.18.0-553.el8_10.x86_64.rpm"));
}

TEST_CASE("buildDocaHostInstallCommand excludes kernel package upgrades")
{
    CHECK(buildDocaHostInstallCommand()
        == "dnf install --nogpg -y "
           "--exclude=kernel "
           "--exclude=kernel-core "
           "--exclude=kernel-modules "
           "--exclude=kernel-modules-core "
           "--exclude=kernel-devel\\* "
           "--exclude=kernel-headers\\* "
           "doca-ofed mlnx-fw-updater");
}

TEST_CASE("buildInboxOFEDInstalledCommand uses APT package probes on Ubuntu")
{
    CHECK(buildInboxOFEDInstalledCommand(
              opencattus::models::OS(opencattus::models::OS::Distro::Ubuntu,
                  opencattus::models::OS::Platform::ubuntu24, 4))
        == "dpkg-query -W rdma-core ibverbs-providers infiniband-diags");
}

TEST_CASE("buildInboxOFEDInstallCommand installs Ubuntu RDMA packages")
{
    CHECK(buildInboxOFEDInstallCommand(
              opencattus::models::OS(opencattus::models::OS::Distro::Ubuntu,
                  opencattus::models::OS::Platform::ubuntu24, 4))
        == "DEBIAN_FRONTEND=noninteractive apt install -y "
           "rdma-core ibverbs-providers infiniband-diags perftest");
}

TEST_CASE("DOCA DKMS priming runs when a newer kernel is installed")
{
    CHECK(shouldPrimeDocaDkmsForInstalledKernel(
        "5.14.0-611.41.1.el9_7.x86_64", "5.14.0-611.5.1.el9_7.x86_64"));
}

TEST_CASE("DOCA DKMS priming is skipped on the running kernel")
{
    CHECK_FALSE(shouldPrimeDocaDkmsForInstalledKernel(
        "5.14.0-611.41.1.el9_7.x86_64", "5.14.0-611.41.1.el9_7.x86_64"));
}

TEST_CASE("latestInstalledKernelCore uses install time instead of version sort")
{
    class KernelQueryRunner final : public IRunner {
    public:
        std::string queriedCommand;

        int executeCommand(const std::string&) override { return 0; }
        int executeCommand(const std::string&, std::list<std::string>&) override
        {
            return 0;
        }
        opencattus::services::CommandProxy executeCommandIter(
            const std::string&, opencattus::services::Stream) override
        {
            return { };
        }
        void checkCommand(const std::string&) override { }
        std::vector<std::string> checkOutput(
            const std::string& command) override
        {
            queriedCommand = command;
            return {
                "1712441600 4.18.0-553.el8_10.x86_64",
                "1712450000 4.18.0-553.111.1.el8_10.x86_64",
            };
        }
        int downloadFile(const std::string&, const std::string&) override
        {
            return 0;
        }
        int run(const opencattus::services::ScriptBuilder&) override
        {
            return 0;
        }
    };

    auto runner = KernelQueryRunner { };
    CHECK(
        latestInstalledKernelCore(runner) == "4.18.0-553.111.1.el8_10.x86_64");
    CHECK(runner.queriedCommand
        == "bash -lc \"rpm -q kernel-core --qf '%{INSTALLTIME} "
           "%{VERSION}-%{RELEASE}.%{ARCH}\\n'\"");
}
