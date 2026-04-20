#include <opencattus/functions.h>
#include <opencattus/models/os.h>
#include <opencattus/services/ansible/roles/check.h>
#include <opencattus/services/log.h>
#include <opencattus/services/runner.h>
#include <opencattus/utils/singleton.h>

#ifdef BUILD_TESTING
#include <doctest/doctest.h>
#else
#define DOCTEST_CONFIG_DISABLE
#include <doctest/doctest.h>
#endif

#include <fmt/core.h>

#include <cstring>

namespace opencattus::services::ansible::roles::check {

auto isNewerKernelAvailable(
    std::string_view availableKernel, std::string_view runningKernel) -> bool
{
    if (availableKernel.empty() || availableKernel == runningKernel) {
        return false;
    }

    return strverscmp(availableKernel.data(), runningKernel.data()) > 0;
}

void run(const Role& role)
{
    using namespace opencattus::utils;

    if (singleton::os().getPackageType()
        == opencattus::models::OS::PackageType::DEB) {
        runner::shell::cmd("DEBIAN_FRONTEND=noninteractive apt-get update");
        LOG_WARN("Skipping kernel freshness check on Ubuntu; the existing "
                 "check is RPM-specific");
        return;
    }

    // OFED installation fails with ISO kernel, require update and reboot
    runner::shell::cmd("dnf makecache");
    const auto kernelAvailable
        = runner::shell::output("dnf -q repoquery kernel --available --qf "
                                "'%{{VERSION}}-%{{RELEASE}}.%{{ARCH}}' "
                                "2>/dev/null | sort -V | tail -1");

    if (!singleton::options()->shouldSkip("check-kernel")) {
        functions::abortif(isNewerKernelAvailable(kernelAvailable,
                               singleton::osservice()->getKernelRunning()),
            "New kernel available, run `dnf install -y kernel` and reboot "
            "before continue, use `--skip check-kernel` to skip (not "
            "recommended)");
    }
    // TODO
    // Implement checks to run before the installation
    //
    // - enough space disk
    // - answerfile validation
    // - internet connection?
    // - no swap?
}

TEST_CASE("isNewerKernelAvailable ignores identical kernels")
{
    CHECK_FALSE(isNewerKernelAvailable(
        "5.14.0-611.41.1.el9_7.x86_64", "5.14.0-611.41.1.el9_7.x86_64"));
}

TEST_CASE("isNewerKernelAvailable ignores older available kernels")
{
    CHECK_FALSE(isNewerKernelAvailable(
        "5.14.0-611.24.1.el9_7.x86_64", "5.14.0-611.41.1.el9_7.x86_64"));
}

TEST_CASE("isNewerKernelAvailable detects newer available kernels")
{
    CHECK(isNewerKernelAvailable(
        "5.14.0-611.50.1.el9_7.x86_64", "5.14.0-611.41.1.el9_7.x86_64"));
}

} // namespace opencattus::services::ansible::roles::check
