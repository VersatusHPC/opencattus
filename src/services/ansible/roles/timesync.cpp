
#include <fmt/core.h>
#include <opencattus/models/cluster.h>
#include <opencattus/patterns/singleton.h>
#include <opencattus/services/ansible/role.h>
#include <opencattus/services/ansible/roles/timesync.h>
#include <opencattus/services/log.h>
#include <opencattus/services/scriptbuilder.h>
#include <string_view>

#ifdef BUILD_TESTING
#include <doctest/doctest.h>
#else
#define DOCTEST_CONFIG_DISABLE
#include <doctest/doctest.h>
#endif

namespace opencattus::services::ansible::roles::timesync {

ScriptBuilder installScript(
    const Role& role, const opencattus::models::OS& osinfo)
{
    using namespace opencattus;
    ScriptBuilder builder(osinfo);

    LOG_ASSERT(role.roleName() == "timesync",
        fmt::format("Expected timesync role, found {}", role.roleName()));

    builder.addPackage("chrony");

    const auto& connections = opencattus::Singleton<models::Cluster>::get()
                                  ->getHeadnode()
                                  .getConnections();

    const auto filename
        = osinfo.getPackageType() == opencattus::models::OS::PackageType::DEB
        ? std::string_view(CHROOT "/etc/chrony/chrony.conf")
        : std::string_view(CHROOT "/etc/chrony.conf");
    for (const auto& connection : connections) {
        if ((connection.getNetwork()->getProfile()
                == Network::Profile::Management)
            || (connection.getNetwork()->getProfile()
                == Network::Profile::Service)) {

            builder.removeLineWithKeyFromFile(filename, "local stratum")
                .removeLineWithKeyFromFile(filename, "allow")
                .addLineToFile(filename, "local stratum 10", "local stratum 10")
                .addLineToFile(filename, "allow {}/{}", "allow {}/{}",
                    connection.getAddress().to_string(),
                    connection.getNetwork()->cidr.at(
                        connection.getNetwork()->getSubnetMask().to_string()));
        }
    }

    builder.enableService(
        osinfo.getPackageType() == opencattus::models::OS::PackageType::DEB
            ? std::string_view("chrony")
            : std::string_view("chronyd"));

    return builder;
}

} // namespace opencattus::services::ansible::roles::timesync
