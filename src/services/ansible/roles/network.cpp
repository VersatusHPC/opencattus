#include <opencattus/functions.h>
#include <opencattus/services/ansible/roles/network.h>
#include <opencattus/services/log.h>
#include <opencattus/services/runner.h>
#include <opencattus/utils/network.h>

#ifdef BUILD_TESTING
#include <doctest/doctest.h>
#else
#define DOCTEST_CONFIG_DISABLE
#include <doctest/doctest.h>
#endif

#include <fmt/core.h>
#include <ifaddrs.h>

namespace {

using namespace opencattus::utils::singleton;
using namespace opencattus::services::runner;

[[nodiscard]] std::string renderStaticConnectionScript(
    const Connection& connection, std::string_view ipv6Method)
{
    return fmt::format(R"(
nmcli device set {iface} managed yes
nmcli device set {iface} autoconnect yes

# Remove any pre-existing profiles bound to this interface before creating the
# static profile we actually want to activate.
nmcli -t -f NAME,DEVICE connection show | awk -F: '$2=="{iface}" {{print $1}}' | while read -r existing_connection; do
    [ -n "$existing_connection" ] || continue
    nmcli connection delete "$existing_connection"
done
nmcli con show "{conn_name}" > /dev/null && nmcli connection delete "{conn_name}" || :

nmcli connection add type {type} mtu {mtu} ifname {iface} con-name "{conn_name}" \
    connection.autoconnect yes \
    ipv4.method manual \
    ipv4.addresses "{ip}/{cidr}" \
    ipv4.gateway "" \
    ipv4.ignore-auto-dns yes \
    ipv4.ignore-auto-routes yes \
    ipv6.method {ipv6_method}
sleep 0.2
nmcli connection up "{conn_name}"
)",
        fmt::arg("iface", connection.getInterface().value()),
        fmt::arg("conn_name", opencattus::utils::enums::toString(
                                  connection.getNetwork()->getProfile())),
        fmt::arg("type", connection.getNetwork()->getType()),
        fmt::arg("mtu", connection.getMTU()),
        fmt::arg("ip", connection.getAddress().to_string()),
        fmt::arg("cidr",
            opencattus::utils::network::subnetMaskToCIDR(
                connection.getNetwork()->getSubnetMask())),
        fmt::arg("ipv6_method", ipv6Method));
}

void disableNetworkManagerDNSOverride()
{
    LOG_INFO("Disabling DNS override on NetworkManager")

    std::string_view filename
        = CHROOT "/etc/NetworkManager/conf.d/90-dns-none.conf";

    // TODO: We should not violently remove the file, we may need to backup if
    //  the file exists, and remove after the copy
    opencattus::functions::removeFile(filename);
    // TODO: Would be better handled with a .conf function
    opencattus::functions::addStringToFile(filename,
        "[main]\n"
        "dns=none\n");

    osservice()->restartService("NetworkManager");
}

// WARNING: We used to do this in a DRY way, but each connection has its own
// idissiocracies. Keep connection setup splitted from now on

void configureManagementNetwork(const Connection& connection)
{
    const std::string_view ipv6Method = cluster()->getProvisioner()
            == opencattus::models::Cluster::Provisioner::xCAT
        ? "disabled"
        : "link-local";

    auto connectionName = opencattus::utils::enums::toString(
        connection.getNetwork()->getProfile());
    LOG_INFO("Setting up {} network", connectionName);
    LOG_ASSERT(connectionName == "Management",
        "configureManagementNetwork called with invalid network")

    shell::cmd(renderStaticConnectionScript(connection, ipv6Method));
}

void configureApplicationNetwork(const Connection& connection)
{
    auto connectionName = opencattus::utils::enums::toString(
        connection.getNetwork()->getProfile());
    LOG_INFO("Setting up {} network", connectionName);
    LOG_ASSERT(connectionName == "Application",
        "configureApplicationNetwork called with invalid network")

    shell::cmd(renderStaticConnectionScript(connection, "link-local"));
}

void configureServiceNetwork(const Connection& connection)
{
    auto connectionName = opencattus::utils::enums::toString(
        connection.getNetwork()->getProfile());
    LOG_INFO("Setting up {} network", connectionName);
    LOG_ASSERT(connectionName == "Service",
        "configureServiceNetwork called with invalid network")

    shell::cmd(renderStaticConnectionScript(connection, "link-local"));
}

void configureNetworks(const std::list<Connection>& connections)
{
    osservice()->enableService("NetworkManager");
    disableNetworkManagerDNSOverride();

    for (const auto& connection : std::as_const(connections)) {
        if (!connection.getInterface().has_value()) {
            LOG_WARN("Interface not found for connection {}, skipping",
                connection.getNetwork()->getProfile());
            continue;
        }

        switch (connection.getNetwork()->getProfile()) {
            case Network::Profile::External:
                continue;
            case Network::Profile::Management:
                configureManagementNetwork(connection);
                break;
            case Network::Profile::Application:
                configureApplicationNetwork(connection);
                break;
            case Network::Profile::Service:
                configureServiceNetwork(connection);
                break;
            default:
                // NOTE: This should never happen
                opencattus::functions::abort("Invalid network profile {}",
                    connection.getNetwork()->getProfile());
        }
    }
}

void configureFQDN()
{
    LOG_INFO("Setting up hostname")

    shell::fmt(
        "hostnamectl set-hostname {}", cluster()->getHeadnode().getFQDN());
}

void configureHostsFile()
{
    LOG_INFO("Setting up additional entries on hosts file")

    const auto& headnode = cluster()->getHeadnode();

    const auto& ip = headnode.getConnection(Network::Profile::Management)
                         .getAddress()
                         .to_string();
    const auto& fqdn = headnode.getFQDN();
    const auto& hostname = headnode.getHostname();

    std::string_view filename = CHROOT "/etc/hosts";

    opencattus::functions::backupFile(filename);
    opencattus::functions::addStringToFile(
        filename, fmt::format("{}\t{} {}\n", ip, fqdn, hostname));
}

}

namespace opencattus::services::ansible::roles::network {

void run(const Role& role)
{
    LOG_INFO("Setting up networks, use `--skip network` to skip")
    if (utils::singleton::options()->shouldSkip("network")) {
        return;
    }

    const auto connections = cluster()->getHeadnode().getConnections();
    configureNetworks(connections);
    configureFQDN();
    configureHostsFile();
}

}

TEST_CASE("renderStaticConnectionScript activates the generated profile")
{
    const auto interfaceName = []() {
        struct ifaddrs* interfaces = nullptr;
        if (getifaddrs(&interfaces) != 0) {
            throw std::runtime_error("Unable to enumerate network interfaces");
        }

        for (auto* current = interfaces; current != nullptr;
             current = current->ifa_next) {
            if (current->ifa_name == nullptr) {
                continue;
            }
            if (std::string_view(current->ifa_name) == "lo") {
                continue;
            }

            const std::string name = current->ifa_name;
            freeifaddrs(interfaces);
            return name;
        }

        freeifaddrs(interfaces);
        throw std::runtime_error("No non-loopback network interface available");
    }();

    Network network(Network::Profile::Management, Network::Type::Ethernet);
    network.setSubnetMask("255.255.255.0");

    Connection connection(&network);
    connection.setInterface(interfaceName);
    connection.setAddress("192.168.30.254");

    const auto script = renderStaticConnectionScript(connection, "disabled");

    CHECK(script.contains("nmcli connection up \"Management\""));
    CHECK(script.contains(
        fmt::format("awk -F: '$2==\"{}\" {{print $1}}' | while read -r ",
            interfaceName)
            + "existing_connection; do"));
    CHECK(script.contains(
        fmt::format("nmcli device set {} managed yes", interfaceName)));
    CHECK(script.contains(
        fmt::format("nmcli connection add type Ethernet mtu 1500 ifname {} "
                    "con-name \"Management\"",
            interfaceName)));
    CHECK_FALSE(script.contains("nmcli device connect oc-mgmt0"));
}
