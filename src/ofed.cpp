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
#include <opencattus/utils/singleton.h>
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

    if (requestedKernel.has_value() && requestedKernel.value() != runningKernel)
    {
        return DocaInstallMode::LegacyKernelSupport;
    }

    if (isDocaLtsStream(ofed.getVersion())) {
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

    return "dnf -y --nogpg install kernel kernel-devel kernel-headers "
           "doca-extra tar";
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
    switch (m_kind) {
        case OFED::Kind::Doca:
            return runner->executeCommand("rpm -q doca-ofed") == 0;
        case OFED::Kind::Inbox:
            return runner->executeCommand(
                       "dnf group info \"Infiniband Support\"")
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
        case OFED::Kind::Inbox:
            opencattus::Singleton<opencattus::services::IOSService>::get()
                ->groupInstall("Infiniband Support");
            break;

        case OFED::Kind::Doca: {
            auto runner
                = opencattus::Singleton<opencattus::services::IRunner>::get();
            auto repoManager = opencattus::Singleton<
                opencattus::services::repos::RepoManager>::get();
            auto osservice
                = opencattus::Singleton<opencattus::services::IOSService>::get();
            const auto requestedKernel = requestedKernelForOFED();
            const auto runningKernel = osservice->getKernelRunning();
            const auto installMode
                = selectDocaInstallMode(*this,
                    opencattus::utils::singleton::os(),
                    requestedKernel ? std::optional<std::string_view>(
                                          requestedKernel.value())
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

                runner->checkCommand(buildDocaLegacyPrereqCommand(
                    requestedKernel ? std::optional<std::string_view>(
                                          requestedKernel.value())
                                    : std::nullopt));

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

                auto rpm = runner->checkOutput(
                    "bash -c \"find /tmp/DOCA*/ -name '*.rpm' -printf '%T@ "
                    "%p\n' | sort -nk1 | tail -1 | awk '{print $2}'\"");
                assert(!rpm.empty());
                runner->executeCommand(fmt::format("dnf install -y {}", rpm[0]));
            } else {
                LOG_INFO("Using the DOCA repo + DKMS installation path for "
                         "stream {} and running kernel {}",
                    getVersion(), runningKernel);
                repoManager->enable("epel");
                runner->checkCommand("dnf makecache --repo=epel");
                runner->checkCommand(buildDocaDkmsPrereqCommand(
                    std::optional<std::string_view>(runningKernel)));
                runner->checkCommand(
                    "dnf -y --nogpg install doca-extra tar");

                LOG_INFO("Staging DOCA kernel artifacts for diskless node "
                         "images");
                if (!opts->shouldSkip("compile-doca-driver")) {
                    runner->checkCommand(
                        "/opt/mellanox/doca/tools/doca-kernel-support");
                }
            }

            runner->checkCommand("dnf makecache --repo=doca*");
            runner->checkCommand(
                "dnf install --nogpg -y doca-ofed mlnx-fw-updater");
            runner->executeCommand("systemctl restart openibd || true");
            // runner->executeCommand("systemctl enable --now opensmd");
        } break;

        case OFED::Kind::Oracle:
            throw std::logic_error("Oracle RDMA release is not yet supported");

            break;
    }
}

TEST_CASE("selectDocaInstallMode keeps LTS streams on the legacy path")
{
    const OFED ofed(OFED::Kind::Doca, "latest-2.9-LTS");
    const auto mode = selectDocaInstallMode(ofed,
        opencattus::models::OS(opencattus::models::OS::Distro::Rocky,
            opencattus::models::OS::Platform::el9, 7),
        std::nullopt, "5.14.0-570.24.1.el9_6.x86_64");

    CHECK(mode == DocaInstallMode::LegacyKernelSupport);
}

TEST_CASE("selectDocaInstallMode uses DKMS for current DOCA streams")
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
