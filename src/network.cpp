/*
 * Copyright 2021 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <opencattus/connection.h>
#include <opencattus/functions.h>
#include <opencattus/network.h>
#include <opencattus/services/log.h>
#include <opencattus/utils/enums.h>
#include <opencattus/utils/formatters.h>

#include <algorithm>
#include <arpa/inet.h> /* inet_*() functions */
#include <boost/asio.hpp>
#include <cstdint>
#include <fstream>
#include <ifaddrs.h>
#include <net/if.h>
#include <optional>
#include <regex>
#include <resolv.h>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifdef BUILD_TESTING
#include <doctest/doctest.h>
#else
#define DOCTEST_CONFIG_DISABLE
#include <doctest/doctest.h>
#endif

#if __cpp_lib_starts_ends_with < 201711L
#include <boost/algorithm/string.hpp>
#endif

namespace {

auto gatewayFromRouteHex(std::string_view rawGateway) -> std::optional<address>
{
    try {
        const auto raw = static_cast<std::uint32_t>(
            std::stoul(std::string(rawGateway), nullptr, 16));
        if (raw == 0) {
            return std::nullopt;
        }

        return boost::asio::ip::address_v4(ntohl(raw));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

auto parseGatewayFromProcRoute(std::istream& routes, std::string_view interface)
    -> std::optional<address>
{
    std::string line;
    std::getline(routes, line);

    while (std::getline(routes, line)) {
        std::istringstream fields(line);
        std::string iface;
        std::string destination;
        std::string gateway;
        fields >> iface >> destination >> gateway;

        if (iface != interface || destination != "00000000") {
            continue;
        }

        return gatewayFromRouteHex(gateway);
    }

    return std::nullopt;
}

auto parseFirstAddressToken(std::string_view raw) -> std::optional<address>
{
    std::string normalized(raw);
    std::ranges::replace(normalized, ',', ' ');

    std::istringstream tokens(normalized);
    std::string token;
    while (tokens >> token) {
        boost::system::error_code ec;
        auto candidate = boost::asio::ip::make_address(token, ec);
        if (!ec && !candidate.is_unspecified()) {
            return candidate;
        }
    }

    return std::nullopt;
}

auto parseGatewayFromNetworkManagerDevice(std::istream& device)
    -> std::optional<address>
{
    static constexpr std::string_view routersPrefix = "dhcp4.routers=";

    std::string line;
    while (std::getline(device, line)) {
        if (!line.starts_with(routersPrefix)) {
            continue;
        }

        return parseFirstAddressToken(
            std::string_view(line).substr(routersPrefix.size()));
    }

    return std::nullopt;
}

auto fetchGatewayFromProcRouteFile(std::string_view interface)
    -> std::optional<address>
{
    std::ifstream routes("/proc/net/route");
    if (!routes) {
        return std::nullopt;
    }

    return parseGatewayFromProcRoute(routes, interface);
}

auto fetchGatewayFromNetworkManagerDeviceFile(const std::string& interface)
    -> std::optional<address>
{
    const auto ifindex = if_nametoindex(interface.c_str());
    if (ifindex == 0) {
        return std::nullopt;
    }

    std::ifstream device(
        fmt::format("/run/NetworkManager/devices/{}", ifindex));
    if (!device) {
        return std::nullopt;
    }

    return parseGatewayFromNetworkManagerDevice(device);
}

} // namespace

Network::Network()
    : Network(Profile::External)
{
    LOG_INFO("Initializing network (ctr 1), profile=external");
}

Network::Network(Profile profile)
    : Network(profile, Type::Ethernet)
{
    LOG_INFO("Initializing network (ctr 2) profile={}, type=ethernet",
        opencattus::utils::enums::toString(profile));
}

Network::Network(Profile profile, Type type)
    : m_profile(profile)
    , m_type(type)
{
    LOG_INFO("Initializing network (ctr 3), profile={}, type={}",
        opencattus::utils::enums::toString(profile),
        opencattus::utils::enums::toString(type));
}

Network::Network(Profile profile, Type type, const std::string& ip,
    const std::string& subnetMask, const std::string& gateway,
    const uint16_t& vlan, const std::string& domainName,
    const std::vector<address>& nameserver)
    : Network(profile, type)
{
    LOG_INFO(
        "Initializing network (ctr 4), profile={}, type={}, ip={}, "
        "subnetMask={}, gateway={}, vlan={}, domainName={}, nameservers={}",
        opencattus::utils::enums::toString(profile),
        opencattus::utils::enums::toString(type), ip, subnetMask, gateway, vlan,
        domainName, fmt::join(nameserver, ","));
    setAddress(ip);
    setSubnetMask(subnetMask);
    setGateway(gateway);
    setVLAN(vlan);
    setDomainName(domainName);
    setNameservers(nameserver);
}

Network::Network(Profile profile, Type type, const std::string& ip,
    const std::string& subnetMask, const std::string& gateway,
    const uint16_t& vlan, const std::string& domainName,
    const std::vector<std::string>& nameserver)
    : Network(profile, type)
{
    LOG_INFO(
        "Initializing network (ctr 5), profile={}, type={}, ip={}, "
        "subnetMask={}, gateway={}, vlan={}, domainName={}, nameservers={}",
        opencattus::utils::enums::toString(profile),
        opencattus::utils::enums::toString(type), ip, subnetMask, gateway, vlan,
        domainName, fmt::join(nameserver, ","));
    setAddress(ip);
    setSubnetMask(subnetMask);
    setGateway(gateway);
    setVLAN(vlan);
    setDomainName(domainName);
    setNameservers(nameserver);
}

//// TODO: Check for std::move support on const member data
////  * https://lesleylai.info/en/const-and-reference-member-variables/
// Network::Network(Network&& other) noexcept
//         : m_profile{other.m_profile}
//         , m_type{other.m_type}
//         , m_address{other.m_address}
//         , m_subnetMask{other.m_subnetMask}
//         , m_gateway{other.m_gateway}
//         , m_vlan{other.m_vlan}
//         , m_domainName{std::move(other.m_domainName)}
//         , m_nameservers{std::move(other.m_nameservers)}
//{}

const Network::Profile& Network::getProfile() const { return m_profile; }

const Network::Type& Network::getType() const { return m_type; }

/* TODO: Implement checks
 *  - Subnet correct size
 *  - Overload for different inputs (string and int)
 *  - Check if network address and gateway are inside the mask
 */
address Network::getAddress() const { return m_address; }

void Network::setAddress(const address& ip)
{
    const address unspecifiedAddress = boost::asio::ip::make_address("0.0.0.0");

    if (ip == unspecifiedAddress)
        throw std::runtime_error("IP address cannot be 0.0.0.0");

    m_address = ip;
    LOG_TRACE("{} network address set to {}",
        opencattus::utils::enums::toString(m_profile), m_address.to_string());
}

void Network::setAddress(const std::string& ip)
{
    try {
        setAddress(boost::asio::ip::make_address(ip));
    } catch (boost::system::system_error& e) {
        throw std::runtime_error("Invalid IP address");
    }
}

address Network::fetchAddress(const std::string& interface)
{
    struct in_addr addr {};
    struct in_addr netmask {};
    struct in_addr network {};

    if (inet_aton(
            Connection::fetchAddress(interface).to_string().c_str(), &addr)
        == 0)
        throw std::runtime_error("Invalid IP address");
    if (inet_aton(fetchSubnetMask(interface).to_string().c_str(), &netmask)
        == 0)
        throw std::runtime_error("Invalid subnet mask address");

    network.s_addr = addr.s_addr & netmask.s_addr;

    auto result = boost::asio::ip::make_address(inet_ntoa(network));

    LOG_TRACE(
        "Got address {} from interface {}", result.to_string(), interface);

    return result;
}

address Network::getSubnetMask() const { return m_subnetMask; }

void Network::setSubnetMask(const address& subnetMask)
{
    //@TODO Use class network_v4 instead
    if (!cidr.contains(subnetMask.to_string()))
        throw std::runtime_error("Invalid subnet mask");

    m_subnetMask = subnetMask;
}

void Network::setSubnetMask(const std::string& subnetMask)
{
    try {
        setSubnetMask(boost::asio::ip::make_address(subnetMask));
    } catch (boost::system::system_error& e) {
        throw std::runtime_error("Invalid subnet mask");
    }
}

address Network::fetchSubnetMask(const std::string& interface)
{
    struct ifaddrs *ifaddr, *ifa;

    if (getifaddrs(&ifaddr) == -1)
        throw std::runtime_error(
            fmt::format("Cannot get interfaces: {}", std::strerror(errno)));

    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_netmask == nullptr)
            continue;

        if (ifa->ifa_netmask->sa_family != AF_INET)
            continue;

        // TODO: Check for leaks since we can't run freeifaddrs before return
        if (std::strcmp(ifa->ifa_name, interface.c_str()) == 0) {
            auto* sa = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_netmask);

            if (inet_ntoa(sa->sin_addr) == nullptr)
                continue;

            boost::system::error_code ec;
            address result
                = boost::asio::ip::make_address(inet_ntoa(sa->sin_addr), ec);

            if (ec.value() != boost::system::errc::success) {
                LOG_TRACE("interface {} returned an error while getting subnet "
                          "mask address ({}): {}",
                    interface, inet_ntoa(sa->sin_addr), ec.message());
                continue;
            }

            LOG_TRACE("Got subnet mask address {} from interface {}",
                result.to_string(), interface);

            freeifaddrs(ifaddr);
            return result;
        }
    }

    freeifaddrs(ifaddr);
    throw std::runtime_error(fmt::format(
        "Interface {} does not have a netmask address defined", interface));
}

address Network::calculateAddress(const address& connectionAddress)
{

    if (m_subnetMask.is_unspecified()) {
        throw std::runtime_error(
            fmt::format("Network must have a specified subnet mask to "
                        "calculate the address"));
    }

    struct in_addr ip_addr {};
    struct in_addr subnet_addr {};

    inet_aton(connectionAddress.to_string().c_str(), &ip_addr);
    inet_aton(m_subnetMask.to_string().c_str(), &subnet_addr);
    return boost::asio::ip::make_address(
        inet_ntoa({ ip_addr.s_addr & subnet_addr.s_addr }));
}

address Network::calculateAddress(const std::string& connectionAddress)
{
    try {
        return calculateAddress(
            boost::asio::ip::make_address(connectionAddress));
    } catch (boost::system::system_error& e) {
        throw std::runtime_error("Invalid ip address");
    }
}

address Network::getGateway() const { return m_gateway; }

void Network::setGateway(const address& gateway) { m_gateway = gateway; }

void Network::setGateway(const std::string& gateway)
{
    try {
        setGateway(boost::asio::ip::make_address(gateway));
    } catch (boost::system::system_error& e) {
        throw std::runtime_error("Invalid gateway");
    }
}

address Network::fetchGateway(const std::string& interface)
{
    if (const auto routeGateway = fetchGatewayFromProcRouteFile(interface);
        routeGateway.has_value()) {
        LOG_TRACE("Got gateway address {} from route table for interface {}",
            routeGateway->to_string(), interface);
        return routeGateway.value();
    }

    if (const auto dhcpGateway
        = fetchGatewayFromNetworkManagerDeviceFile(interface);
        dhcpGateway.has_value()) {
        LOG_TRACE("Got gateway address {} from NetworkManager DHCP data for "
                  "interface {}",
            dhcpGateway->to_string(), interface);
        return dhcpGateway.value();
    }

    LOG_TRACE(
        "Interface {} does not have a gateway IP address defined", interface);
    return {};
}

TEST_CASE("parseGatewayFromProcRoute reads the default route for an interface")
{
    std::istringstream routes(
        "Iface\tDestination\tGateway\tFlags\tRefCnt\tUse\tMetric\tMask\tMTU\t"
        "Window\tIRTT\n"
        "bond0\t00000000\t0100140A\t0003\t0\t0\t300\t00000000\t0\t0\t0\n"
        "bond1\t00000000\t010115AC\t0003\t0\t0\t301\t00000000\t0\t0\t0\n");

    const auto gateway = parseGatewayFromProcRoute(routes, "bond1");

    REQUIRE(gateway.has_value());
    CHECK(gateway->to_string() == "172.21.1.1");
}

TEST_CASE("parseGatewayFromNetworkManagerDevice reads DHCP routers")
{
    std::istringstream device("dhcp4.domain_name_servers=10.20.0.1\n"
                              "dhcp4.routers=10.20.0.1\n");

    const auto gateway = parseGatewayFromNetworkManagerDevice(device);

    REQUIRE(gateway.has_value());
    CHECK(gateway->to_string() == "10.20.0.1");
}

uint16_t Network::getVLAN() const { return m_vlan; }

void Network::setVLAN(uint16_t vlan)
{
    if (vlan >= 4096)
        throw std::out_of_range("VLAN value must be less than 4096.");
    m_vlan = vlan;
}

const std::string& Network::getDomainName() const { return m_domainName; }

void Network::setDomainName(const std::string& domainName)
{
    if (domainName.empty())
        throw std::invalid_argument("Domain name cannot be empty.");

    if (domainName.size() > 255)
        throw std::length_error("Domain name exceeds the maximum allowed "
                                "length of 255 characters.");

    if (domainName.starts_with('-') or domainName.ends_with('-'))
        throw std::runtime_error("Hostname cannot start or end with a hyphen.");

    /* Check if string has only digits */
    if (std::regex_match(domainName, std::regex("^[0-9]+$")))
        throw std::invalid_argument(
            "Domain name should not consist solely of numeric digits.");

    /* Check if it's not only alphanumerics and - */
    if (!(std::regex_match(domainName, std::regex("^[A-Za-z0-9-.]+$"))))
        throw std::invalid_argument(fmt::format(
            "Domain name ({}) contains invalid characters. Only alphanumeric "
            "characters and hyphens are allowed.",
            domainName));

    m_domainName = domainName;
}

std::string Network::fetchDomainName()
{
    char domainName[256]; // Adjust the buffer size as needed
    if (getdomainname(domainName, sizeof(domainName)) == -1)
        throw std::runtime_error("Failed to fetch domain name");

    LOG_TRACE("Got domain name {}", domainName)

    // BUG: This is a bug, we must return the domain name
    auto ret = std::string(domainName);
    if (ret == "(none)") {
        ret = "";
    }
    return ret;
}

std::vector<address> Network::getNameservers() const
{
    std::vector<address> returnVector;
    for (const auto& nameserver : std::as_const(m_nameservers))
        returnVector.emplace_back(nameserver);

    return returnVector;
}

void Network::setNameservers(const std::vector<address>& nameservers)
{
    if (nameservers.empty())
        return;

    m_nameservers.reserve(nameservers.size());

#if __cplusplus < 202002L
    size_t i = 0;
    for (const auto& ns : std::as_const(nameservers)) {
#else
    for (std::size_t i = 0; const auto& ns : std::as_const(nameservers)) {
#endif
        m_nameservers.push_back(ns);
    }
}

void Network::setNameservers(const std::vector<std::string>& nameservers)
{
    std::vector<address> formattedNameservers;
    for (std::size_t i = 0; i < nameservers.size(); i++) {
        formattedNameservers.emplace_back(
            boost::asio::ip::make_address(nameservers[i]));
    }

    setNameservers(formattedNameservers);
}

std::vector<address> Network::fetchNameservers()
{
    std::vector<address> nameservers;
    if (res_init() == -1)
        throw std::runtime_error("Failed to initialize domain name resolution");

    nameservers.reserve(static_cast<std::size_t>(_res.nscount));
    for (const auto& ns : _res.nsaddr_list) {
        address formattedNs
            = boost::asio::ip::make_address_v4(inet_ntoa(ns.sin_addr));
        if (formattedNs.to_string() == "0.0.0.0")
            continue;
        nameservers.emplace_back(formattedNs);
    }

    LOG_TRACE("Got nameservers {}", nameservers.data()->to_string())
    return nameservers;
}

#ifndef NDEBUG
void Network::dumpNetwork() const
{
    LOG_DEBUG("Profile: {}", opencattus::utils::enums::toString(m_profile))
    LOG_DEBUG("Type: {}", opencattus::utils::enums::toString(m_type))
}
#endif
