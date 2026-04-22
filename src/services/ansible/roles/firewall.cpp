#include <opencattus/services/ansible/roles/firewall.h>
#include <opencattus/services/log.h>
#include <opencattus/services/runner.h>

#ifdef BUILD_TESTING
#include <doctest/doctest.h>
#else
#define DOCTEST_CONFIG_DISABLE
#include <doctest/doctest.h>
#endif

#include <fmt/core.h>

namespace {
using namespace opencattus::utils::singleton;
void configureFirewall()
{
    LOG_INFO("Setting up firewall")

    if (cluster()->getHeadnode().getOS().getPackageType()
        == opencattus::models::OS::PackageType::DEB) {
        if (cluster()->isFirewall()) {
            LOG_WARN("Ubuntu firewall configuration is not implemented yet; "
                     "leaving the existing firewall state unchanged");
        } else {
            opencattus::services::runner::shell::cmd(
                "systemctl disable --now ufw || :");
            LOG_WARN("UFW has been disabled if it was installed")
        }
        return;
    }

    if (cluster()->isFirewall()) {
        osservice()->enableService("firewalld");

        // Add the management interface as trusted
        ::runner()->executeCommand(fmt::format(
            "firewall-cmd --permanent --zone=trusted --change-interface={}",
            cluster()
                ->getHeadnode()
                .getConnection(Network::Profile::Management)
                .getInterface()
                .value()));

        // If we have IB, also add its interface as trusted
        if (cluster()->getOFED())
            ::runner()->executeCommand(fmt::format(
                "firewall-cmd --permanent --zone=trusted --change-interface={}",
                cluster()
                    ->getHeadnode()
                    .getConnection(Network::Profile::Application)
                    .getInterface()
                    .value()));

        ::runner()->executeCommand("firewall-cmd --reload");
    } else {
        osservice()->disableService("firewalld");

        LOG_WARN("Firewalld has been disabled")
    }
}

}

namespace opencattus::services::ansible::roles::firewall {

void run(const Role& role) { configureFirewall(); }

}
