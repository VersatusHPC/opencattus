#include <opencattus/functions.h>
#include <opencattus/models/cluster.h>
#include <opencattus/services/ansible/roles/selinux.h>
#include <opencattus/services/log.h>

#ifdef BUILD_TESTING
#include <doctest/doctest.h>
#else
#define DOCTEST_CONFIG_DISABLE
#include <doctest/doctest.h>
#endif

#include <fmt/core.h>

namespace {
using namespace opencattus::utils::singleton;

void disableSELinux()
{
    ::runner()->executeCommand("setenforce 0");

    const auto filename = CHROOT "/etc/sysconfig/selinux";

    opencattus::functions::backupFile(filename);
    opencattus::functions::changeValueInConfigurationFile(
        filename, "SELINUX", "disabled");

    LOG_WARN("SELinux has been disabled")
}
void configureSELinuxMode()
{
    LOG_INFO("Setting up SELinux")

    if (cluster()->getHeadnode().getOS().getPackageType()
        == opencattus::models::OS::PackageType::DEB) {
        LOG_INFO("Skipping SELinux configuration on Ubuntu");
        return;
    }

    switch (cluster()->getSELinux()) {
        case opencattus::models::Cluster::SELinuxMode::Permissive:
            ::runner()->executeCommand("setenforce 0");
            /* Permissive mode */
            break;

        case opencattus::models::Cluster::SELinuxMode::Enforcing:
            /* Enforcing mode */
            ::runner()->executeCommand("setenforce 1");
            break;

        case opencattus::models::Cluster::SELinuxMode::Disabled:
            disableSELinux();
            break;
    }
}

}

namespace opencattus::services::ansible::roles::selinux {

void run(const Role& role) { configureSELinuxMode(); }

}
