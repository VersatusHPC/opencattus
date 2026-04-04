#include <opencattus/functions.h>
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

namespace opencattus::services::ansible::roles::check {

void run(const Role& role)
{
    using namespace opencattus::utils;

    // OFED installation fails with ISO kernel, require update and reboot
    runner::shell::cmd("dnf makecache --repo baseos");
    const auto kernelAvailable = runner::shell::output(
        "dnf -q repoquery kernel --available --qf "
        "'%{{VERSION}}-%{{RELEASE}}.%{{ARCH}}' 2>/dev/null | sort -V | tail -1");

    if (!singleton::options()->shouldSkip("check-kernel")) {
        functions::abortif(
            kernelAvailable != singleton::osservice()->getKernelRunning(),
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

} // namespace opencattus::services::ansible::roles::check
