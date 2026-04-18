/*
 * Created by Lucas Gracioso <contact@lbgracioso.net>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <fmt/core.h>
#include <iterator>
#include <map>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <system_error>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/lexical_cast.hpp>

#include <opencattus/functions.h>
#include <opencattus/models/answerfile.h>
#include <opencattus/network.h>
#include <opencattus/services/log.h>
#include <opencattus/services/options.h>
#include <opencattus/services/osservice.h>
#include <opencattus/utils/singleton.h>
#include <opencattus/utils/string.h>

using opencattus::services::Postfix;

namespace opencattus::models {

namespace {

    auto ipv4NetworkAddress(const address& ip, const address& subnetMask)
        -> boost::asio::ip::address_v4
    {
        if (!ip.is_v4() || !subnetMask.is_v4()) {
            throw std::invalid_argument(
                "only IPv4 network sections are supported");
        }

        return boost::asio::ip::address_v4(
            ip.to_v4().to_uint() & subnetMask.to_v4().to_uint());
    }

    void validateSubnetMask(
        const std::string& networkSection, const address& subnetMask)
    {
        if (!subnetMask.is_v4()
            || !Network::cidr.contains(subnetMask.to_string())) {
            throw std::invalid_argument(fmt::format(
                "Network section '{}' field 'subnet_mask' validation failed - "
                "invalid subnet mask '{}'",
                networkSection, subnetMask.to_string()));
        }
    }

    void validateGatewayInSubnet(const std::string& networkSection,
        const address& hostAddress, const address& subnetMask,
        const address& gateway)
    {
        if (gateway.is_unspecified()) {
            return;
        }

        const auto networkAddress = ipv4NetworkAddress(hostAddress, subnetMask);
        if (networkAddress != ipv4NetworkAddress(gateway, subnetMask)) {
            throw std::invalid_argument(fmt::format(
                "Network section '{}' field 'gateway' validation failed - "
                "gateway "
                "{} is outside {}/{}",
                networkSection, gateway.to_string(), networkAddress.to_string(),
                static_cast<int>(Network::cidr.at(subnetMask.to_string()))));
        }
    }

    void rememberUniqueNodeValue(std::map<std::string, std::string>& seen,
        std::string_view fieldName, const std::string& value,
        const std::string& owner)
    {
        if (value.empty()) {
            return;
        }

        const auto [it, inserted] = seen.emplace(value, owner);
        if (!inserted) {
            throw std::invalid_argument(
                fmt::format("Duplicate {} '{}' used by {} and {}", fieldName,
                    value, it->second, owner));
        }
    }

    void validateNodeInventoryTopology(const std::vector<AFNode>& nodes)
    {
        std::map<std::string, std::string> hostnames;
        std::map<std::string, std::string> macAddresses;
        std::map<std::string, std::string> nodeAddresses;
        std::map<std::string, std::string> bmcAddresses;
        std::map<std::string, std::string> allAddresses;

        std::size_t nodeNumber = 0;
        for (const auto& node : nodes) {
            ++nodeNumber;
            const auto owner = fmt::format("node '{}'",
                node.hostname.value_or(fmt::format("#{}", nodeNumber)));

            if (node.hostname.has_value()) {
                rememberUniqueNodeValue(
                    hostnames, "hostname", *node.hostname, owner);
            }
            if (node.mac_address.has_value()) {
                rememberUniqueNodeValue(macAddresses, "mac_address",
                    opencattus::utils::string::lower(*node.mac_address), owner);
            }
            if (node.start_ip.has_value() && !node.start_ip->is_unspecified()) {
                const auto nodeAddress = node.start_ip->to_string();
                rememberUniqueNodeValue(
                    nodeAddresses, "node_ip", nodeAddress, owner);
                rememberUniqueNodeValue(allAddresses, "node/BMC address",
                    nodeAddress, fmt::format("{} node_ip", owner));
            }
            if (node.bmc_address.has_value() && !node.bmc_address->empty()) {
                rememberUniqueNodeValue(
                    bmcAddresses, "bmc_address", *node.bmc_address, owner);
                rememberUniqueNodeValue(allAddresses, "node/BMC address",
                    *node.bmc_address, fmt::format("{} bmc_address", owner));
            }
        }
    }

    auto parseUnsignedAnswerfileField(const std::string& section,
        const std::string& field, const std::string& value, bool allowZero)
        -> std::size_t
    {
        const auto expectedType
            = allowZero ? "an unsigned integer" : "a positive integer";
        const auto fail = [&](std::string_view reason) {
            throw std::invalid_argument(fmt::format(
                "Section '{}' field '{}' validation failed - {} (expected {}, "
                "value is '{}')",
                section, field, reason, expectedType, value));
        };

        if (value.empty()) {
            fail("empty value");
        }

        if (!std::ranges::all_of(value, [](unsigned char character) {
                return std::isdigit(character) != 0;
            })) {
            fail("not a number");
        }

        std::size_t parsed = 0;
        const auto* begin = value.data();
        const auto* end = value.data() + value.size();
        const auto [ptr, error] = std::from_chars(begin, end, parsed);
        if (error == std::errc::result_out_of_range) {
            fail("value is out of range");
        }
        if (error != std::errc {} || ptr != end) {
            fail("not a number");
        }
        if (!allowZero && parsed == 0) {
            fail("zero is not valid");
        }

        return parsed;
    }

    void validateUnsignedAnswerfileField(const std::string& section,
        const std::string& field, const std::optional<std::string>& value,
        bool allowZero = false)
    {
        if (!value.has_value()) {
            return;
        }

        parseUnsignedAnswerfileField(section, field, value.value(), allowZero);
    }

    void validateGenericNodeScalars(const AFNode& node)
    {
        validateUnsignedAnswerfileField("node", "padding", node.padding, true);
        validateUnsignedAnswerfileField("node", "sockets", node.sockets);
        validateUnsignedAnswerfileField(
            "node", "cores_per_socket", node.cores_per_socket);
        validateUnsignedAnswerfileField(
            "node", "cpus_per_node", node.cpus_per_node);
        validateUnsignedAnswerfileField(
            "node", "threads_per_core", node.threads_per_core);
        validateUnsignedAnswerfileField(
            "node", "real_memory", node.real_memory);
        validateUnsignedAnswerfileField(
            "node", "bmc_serialport", node.bmc_serialport, true);
        validateUnsignedAnswerfileField(
            "node", "bmc_serialspeed", node.bmc_serialspeed);
    }

    void validateResolvedNodeScalars(
        const std::string& section, const AFNode& node)
    {
        validateUnsignedAnswerfileField(section, "sockets", node.sockets);
        validateUnsignedAnswerfileField(
            section, "cores_per_socket", node.cores_per_socket);
        validateUnsignedAnswerfileField(
            section, "threads_per_core", node.threads_per_core);
        validateUnsignedAnswerfileField(
            section, "bmc_serialport", node.bmc_serialport, true);
        validateUnsignedAnswerfileField(
            section, "bmc_serialspeed", node.bmc_serialspeed);
    }

} // namespace

AnswerFile::AnswerFile(const std::filesystem::path& path, bool loadFromDisk)
    : m_path(path)
    , m_keyfile(path, false)
{
    if (loadFromDisk) {
        loadFile(m_path);
    }
}

void AnswerFile::loadFile(const std::filesystem::path& path)
{
    m_path = path;
    m_keyfile.load();
    loadOptions();
};

void AnswerFile::loadOptions()
{
    LOG_TRACE("Verify answerfile variables")

    loadExternalNetwork();
    loadManagementNetwork();
    loadServiceNetwork();
    loadApplicationNetwork();
    loadInformation();
    loadTimeSettings();
    loadHostnameSettings();
    loadSystemSettings();
    loadRepositories();
    loadNodes();
    loadPostfix();
    loadOFED();
    loadSlurm();
    loadPBS();
}

void AnswerFile::dumpNetwork(
    const AFNetwork& network, const std::string& networkSection)
{
    if (network.con_interface) {
        m_keyfile.setString(
            networkSection, "interface", network.con_interface.value());
    }

    if (network.con_ip_addr) {
        m_keyfile.setString(
            networkSection, "ip_address", network.con_ip_addr->to_string());
    }

    if (network.con_mac_addr) {
        m_keyfile.setString(
            networkSection, "mac_address", *network.con_mac_addr);
    }

    if (network.subnet_mask) {
        m_keyfile.setString(
            networkSection, "subnet_mask", network.subnet_mask->to_string());
    }

    if (network.domain_name) {
        m_keyfile.setString(
            networkSection, "domain_name", *network.domain_name);
    }

    if (network.gateway) {
        m_keyfile.setString(
            networkSection, "gateway", network.gateway->to_string());
    }

    if (network.nameservers && !network.nameservers->empty()) {
        auto nameval = boost::algorithm::join(*network.nameservers, ", ");
        m_keyfile.setString(networkSection, "nameservers", nameval);
    }
}

void AnswerFile::dumpExternalNetwork()
{
    dumpNetwork(external, "network_external");
}

void AnswerFile::dumpManagementNetwork()
{
    dumpNetwork(management, "network_management");
}

void AnswerFile::dumpServiceNetwork()
{
    dumpNetwork(service, "network_service");
}

void AnswerFile::dumpApplicationNetwork()
{
    dumpNetwork(application, "network_application");
}

void AnswerFile::dumpInformation()
{
    m_keyfile.setString(
        "information", "cluster_name", information.cluster_name);
    m_keyfile.setString(
        "information", "company_name", information.company_name);
    m_keyfile.setString(
        "information", "administrator_email", information.administrator_email);
}

void AnswerFile::dumpTimeSettings()
{
    m_keyfile.setString("time", "timezone", time.timezone);
    m_keyfile.setString("time", "timeserver", time.timeserver);
    m_keyfile.setString("time", "locale", time.locale);
}

void AnswerFile::dumpHostnameSettings()
{
    m_keyfile.setString("hostname", "hostname", hostname.hostname);
    m_keyfile.setString("hostname", "domain_name", hostname.domain_name);
}

void AnswerFile::dumpSystemSettings()
{
    auto distroName = opencattus::utils::enums::toString(system.distro);

    m_keyfile.setString("system", "disk_image", system.disk_image.string());
    m_keyfile.setString("system", "distro", std::string { distroName });
    m_keyfile.setString("system", "version", system.version);
    m_keyfile.setString("system", "kernel", system.kernel);
    m_keyfile.setString("system", "provisioner", system.provisioner);
}

void AnswerFile::dumpNodes()
{
    if (nodes.generic.has_value()) {
        const auto& generic = nodes.generic.value();

        if (generic.prefix) {
            m_keyfile.setString("node", "prefix", *generic.prefix);
        }
        if (generic.padding) {
            m_keyfile.setString("node", "padding", *generic.padding);
        }
        if (generic.start_ip) {
            m_keyfile.setString(
                "node", "node_ip", generic.start_ip->to_string());
        }
        if (generic.root_password) {
            m_keyfile.setString(
                "node", "node_root_password", *generic.root_password);
        }
        if (generic.sockets) {
            m_keyfile.setString("node", "sockets", *generic.sockets);
        }
        if (generic.cores_per_socket) {
            m_keyfile.setString(
                "node", "cores_per_socket", *generic.cores_per_socket);
        }
        if (generic.cpus_per_node) {
            m_keyfile.setString(
                "node", "cpus_per_node", *generic.cpus_per_node);
        }
        if (generic.threads_per_core) {
            m_keyfile.setString(
                "node", "threads_per_core", *generic.threads_per_core);
        }
        if (generic.real_memory) {
            m_keyfile.setString("node", "real_memory", *generic.real_memory);
        }
        if (generic.bmc_username) {
            m_keyfile.setString("node", "bmc_username", *generic.bmc_username);
        }
        if (generic.bmc_password) {
            m_keyfile.setString("node", "bmc_password", *generic.bmc_password);
        }
        if (generic.bmc_serialport) {
            m_keyfile.setString(
                "node", "bmc_serialport", *generic.bmc_serialport);
        }
        if (generic.bmc_serialspeed) {
            m_keyfile.setString(
                "node", "bmc_serialspeed", *generic.bmc_serialspeed);
        }
    }

    size_t counter = 1;
    for (const auto& node : nodes.nodes) {
        std::string sectionName = fmt::format("node.{}", counter);

        if (node.hostname) {
            m_keyfile.setString(sectionName, "hostname", *node.hostname);
        }

        if (node.mac_address) {
            m_keyfile.setString(sectionName, "mac_address", *node.mac_address);
        }

        if (node.start_ip) {
            m_keyfile.setString(
                sectionName, "node_ip", node.start_ip->to_string());
        }

        if (node.root_password) {
            m_keyfile.setString(
                sectionName, "node_root_password", *node.root_password);
        }

        if (node.sockets) {
            m_keyfile.setString(sectionName, "sockets", *node.sockets);
        }

        if (node.cores_per_socket) {
            m_keyfile.setString(
                sectionName, "cores_per_socket", *node.cores_per_socket);
        }

        if (node.threads_per_core) {
            m_keyfile.setString(
                sectionName, "threads_per_core", *node.threads_per_core);
        }

        if (node.cpus_per_node) {
            m_keyfile.setString(
                sectionName, "cpus_per_node", *node.cpus_per_node);
        }

        if (node.real_memory) {
            m_keyfile.setString(sectionName, "real_memory", *node.real_memory);
        }

        if (node.bmc_address) {
            m_keyfile.setString(sectionName, "bmc_address", *node.bmc_address);
        }

        if (node.bmc_username) {
            m_keyfile.setString(
                sectionName, "bmc_username", *node.bmc_username);
        }

        if (node.bmc_password)
            m_keyfile.setString(
                sectionName, "bmc_password", *node.bmc_password);

        if (node.bmc_serialport)
            m_keyfile.setString(
                sectionName, "bmc_serialport", *node.bmc_serialport);

        if (node.bmc_serialspeed)
            m_keyfile.setString(
                sectionName, "bmc_serialspeed", *node.bmc_serialspeed);

        counter++;
    }
}

void AnswerFile::dumpPostfix()
{
    if (!postfix.enabled) {
        return;
    }

    auto profileName = opencattus::utils::enums::toString(postfix.profile);
    m_keyfile.setString("postfix", "profile", std::string { profileName });
    if (!postfix.destination.empty()) {
        m_keyfile.setString("postfix", "destination",
            boost::algorithm::join(postfix.destination, ", "));
    }
    if (!postfix.cert_file.empty()) {
        m_keyfile.setString(
            "postfix", "smtpd_tls_cert_file", postfix.cert_file.string());
    }
    if (!postfix.key_file.empty()) {
        m_keyfile.setString(
            "postfix", "smtpd_tls_key_file", postfix.key_file.string());
    }

    switch (postfix.profile) {
        case Postfix::Profile::Local:
            break;
        case Postfix::Profile::Relay:
            if (postfix.smtp.has_value()) {
                m_keyfile.setString(
                    "postfix.relay", "server", postfix.smtp->server);
                m_keyfile.setString("postfix.relay", "port",
                    std::to_string(postfix.smtp->port));
            }
            break;
        case Postfix::Profile::SASL:
            if (postfix.smtp.has_value()) {
                m_keyfile.setString(
                    "postfix.sasl", "server", postfix.smtp->server);
                m_keyfile.setString(
                    "postfix.sasl", "port", std::to_string(postfix.smtp->port));
                if (postfix.smtp->sasl.has_value()) {
                    m_keyfile.setString("postfix.sasl", "username",
                        postfix.smtp->sasl->username);
                    m_keyfile.setString("postfix.sasl", "password",
                        postfix.smtp->sasl->password);
                }
            }
            break;
    }
}

void AnswerFile::dumpOFED()
{
    if (!ofed.enabled) {
        return;
    }

    m_keyfile.setString("ofed", "kind", ofed.kind);
    if (ofed.version.has_value()) {
        m_keyfile.setString("ofed", "version", ofed.version.value());
    }
}

void AnswerFile::dumpRepositories()
{
    if (!repositories.enabled.has_value()) {
        return;
    }

    m_keyfile.setString("repositories", "enabled",
        boost::algorithm::join(repositories.enabled.value(), ", "));
}

void AnswerFile::dumpOptions()
{
    LOG_TRACE("Dump answerfile variables")

    dumpExternalNetwork();
    dumpManagementNetwork();
    dumpServiceNetwork();
    dumpApplicationNetwork();
    dumpInformation();
    dumpTimeSettings();
    dumpHostnameSettings();
    dumpSystemSettings();
    dumpRepositories();
    dumpSlurm();
    dumpPBS();

    dumpNodes();
    dumpOFED();
    dumpPostfix();
}

void AnswerFile::dumpFile(const std::filesystem::path& path)
{
    m_path = path;
    dumpOptions();
    opencattus::services::files::write(path, m_keyfile.toData());
};

address AnswerFile::convertStringToAddress(const std::string& addr)
{
    try {
        return boost::asio::ip::make_address(addr);
    } catch (boost::system::system_error& e) {
        throw std::invalid_argument("Invalid address");
    }
}

std::vector<address> AnswerFile::convertStringToMultipleAddresses(
    const std::string& addr)
{
    std::vector<address> out;
    std::vector<std::string> strout;
    try {
        boost::split(strout, addr, boost::is_any_of(","));
    } catch (boost::system::system_error& e) {
        throw std::invalid_argument(
            "Invalid character while decoding multiple addresses");
    }

    std::transform(strout.begin(), strout.end(), std::back_inserter(out),
        [this](auto& s) { return this->convertStringToAddress(s); });

    return out;
}

template <typename T>
void AnswerFile::validateAttribute(const std::string& sectionName,
    const std::string& attributeName, T& objectAttr, const T& genericAttr)
{
    if constexpr (std::is_same_v<T, std::optional<std::basic_string<char>>>) {
        if (objectAttr.has_value() && !objectAttr->empty()) {
            return;
        }

        if (!genericAttr.has_value() || genericAttr->empty()) {
            throw std::invalid_argument(
                fmt::format("{1} must have a \"{0}\" key or you must inform a "
                            "generic \"{0}\" value",
                    attributeName, sectionName));
        }

        objectAttr = genericAttr;
    } else if constexpr (std::is_same_v<T,
                             std::optional<boost::asio::ip::address>>) {
        if (objectAttr.has_value() && !objectAttr->is_unspecified()) {
            return;
        }

        if (!genericAttr.has_value() || genericAttr->is_unspecified()) {
            throw std::invalid_argument(
                fmt::format("{1} must have a \"{0}\" key or you must inform a "
                            "generic \"{0}\" value",
                    attributeName, sectionName));
        }

        objectAttr = genericAttr;
    } else {
        throw std::invalid_argument("Unsupported type for validateAttribute");
    }
}

template <typename T>
void AnswerFile::convertNetworkAddressAndValidate(
    const std::string& sectionName, const std::string& fieldName,
    T& destination, bool isOptional)
{
    if (!m_keyfile.getStringOpt(sectionName, fieldName)) {
        return;
    }

    auto value = m_keyfile.getString(sectionName, fieldName);
    try {
        destination = convertStringToAddress(value);
    } catch (const std::invalid_argument& e) {
        throw std::invalid_argument(fmt::format(
            "Network section '{0}' field '{1}' validation failed - {2}",
            sectionName, fieldName, e.what()));
    }
}

void AnswerFile::loadNetwork(
    const std::string& networkSection, AFNetwork& network)
{
    LOG_TRACE("Loading network section {}", networkSection);

    network.con_interface = m_keyfile.getString(networkSection, "interface");
    convertNetworkAddressAndValidate(
        networkSection, "ip_address", network.con_ip_addr);
    network.con_mac_addr
        = m_keyfile.getString(networkSection, "mac_address", "");
    convertNetworkAddressAndValidate(
        networkSection, "subnet_mask", network.subnet_mask);
    if (network.subnet_mask.has_value()) {
        validateSubnetMask(networkSection, network.subnet_mask.value());
    }
    network.domain_name = m_keyfile.getStringOpt(networkSection, "domain_name");
    convertNetworkAddressAndValidate(
        networkSection, "gateway", network.gateway);
    if (network.con_ip_addr.has_value() && network.subnet_mask.has_value()
        && network.gateway.has_value()) {
        validateGatewayInSubnet(networkSection, network.con_ip_addr.value(),
            network.subnet_mask.value(), network.gateway.value());
    }

    if (auto opt = m_keyfile.getStringOpt(networkSection, "nameservers")) {
        std::vector<std::string> nameservers;
        boost::split(nameservers, opt.value(), boost::is_any_of(", "),
            boost::token_compress_on);

        network.nameservers = nameservers;
    }
}

void AnswerFile::loadExternalNetwork()
{
    loadNetwork("network_external", external);
}

void AnswerFile::loadManagementNetwork()
{
    loadNetwork("network_management", management);
}

void AnswerFile::loadServiceNetwork()
{
    if (!m_keyfile.hasGroup("network_service"))
        return;

    loadNetwork("network_service", service);
}

void AnswerFile::loadApplicationNetwork()
{
    if (!m_keyfile.hasGroup("network_application"))
        return;

    loadNetwork("network_application", application);
}

void AnswerFile::loadInformation()
{
    information.cluster_name
        = m_keyfile.getString("information", "cluster_name");
    information.company_name
        = m_keyfile.getString("information", "company_name");
    information.administrator_email
        = m_keyfile.getString("information", "administrator_email");
}

void AnswerFile::loadTimeSettings()
{
    time.timezone = m_keyfile.getString("time", "timezone");
    time.timeserver = m_keyfile.getString("time", "timeserver");
    time.locale = m_keyfile.getString("time", "locale");
}

void AnswerFile::loadHostnameSettings()
{
    hostname.hostname = m_keyfile.getString("hostname", "hostname");
    hostname.domain_name = m_keyfile.getString("hostname", "domain_name");
}

void AnswerFile::loadSystemSettings()
{
    system.disk_image = m_keyfile.getString("system", "disk_image");
    auto opts = opencattus::utils::singleton::options();

    // Verify supported distros
    auto afDistro = opencattus::utils::string::lower(
        m_keyfile.getString("system", "distro"));
    if (afDistro == "alma") {
        afDistro = "almalinux";
    }
    if (const auto& formatDistro
        = opencattus::utils::enums::ofStringOpt<OS::Distro>(
            afDistro, opencattus::utils::enums::Case::Insensitive)) {
        system.distro = formatDistro.value();
    } else {
        if (opts->dryRun) {
            return;
        }
        throw std::runtime_error(
            fmt::format("Unsupported distro: {}", afDistro));
    }

    system.version = m_keyfile.getString("system", "version");
    system.kernel = m_keyfile.getStringOpt("system", "kernel");
    system.provisioner = opencattus::utils::string::lower(
        utils::optional::unwrap(m_keyfile.getStringOpt("system", "provisioner"),
            "[system].provisioner missing in the answerfile {}, expecting one "
            "of: "
            "confluent, xcat",
            path()));
    if (system.provisioner != "confluent" && system.provisioner != "xcat") {
        throw std::invalid_argument(fmt::format(
            "Section 'system' field 'provisioner' validation failed - "
            "unsupported provisioner '{}' (expected confluent or xcat)",
            system.provisioner));
    }
}

AFNode AnswerFile::loadNode(const std::string& section)
{
    AFNode node;
    LOG_DEBUG("Loading node {}", section);

    if (section == "node") {
        // Fully initialize generic node
        node.prefix = m_keyfile.getString(section, "prefix");
        node.padding = m_keyfile.getString(section, "padding");
        node.root_password = m_keyfile.getString(section, "node_root_password");
        node.sockets = m_keyfile.getString(section, "sockets");
        node.cores_per_socket
            = m_keyfile.getString(section, "cores_per_socket");
        node.cpus_per_node = m_keyfile.getString(section, "cpus_per_node");
        node.threads_per_core
            = m_keyfile.getString(section, "threads_per_core");
        node.real_memory = m_keyfile.getString(section, "real_memory");
        node.bmc_username = m_keyfile.getString(section, "bmc_username");
        node.bmc_password = m_keyfile.getString(section, "bmc_password");
        node.bmc_serialport = m_keyfile.getString(section, "bmc_serialport");
        node.bmc_serialspeed = m_keyfile.getString(section, "bmc_serialspeed");
        try {
            node.start_ip = convertStringToAddress(
                m_keyfile.getString(section, "node_ip"));
        } catch (const std::invalid_argument& e) {
            throw std::invalid_argument(
                fmt::format("Section '{}' field 'node_ip' "
                            "validation failed - {}",
                    section, e.what()));
        }
        validateGenericNodeScalars(node);
        LOG_DEBUG("Node generic configuration loaded");
        return node;
    } else {
        node.mac_address = m_keyfile.getString(section, "mac_address");
    }

    if (m_keyfile.getStringOpt(section, "node_ip")) {
        try {
            node.start_ip = convertStringToAddress(
                m_keyfile.getString(section, "node_ip"));
        } catch (const std::invalid_argument& e) {
            throw std::invalid_argument(
                fmt::format("Section '{}' field 'node_ip' "
                            "validation failed - {}",
                    section, e.what()));
        }
    }

    node.hostname = m_keyfile.getString(section, "hostname", "");
    node.root_password = m_keyfile.getString(section, "node_root_password", "");
    node.sockets = m_keyfile.getString(section, "sockets", "");
    node.cores_per_socket
        = m_keyfile.getString(section, "cores_per_socket", "");
    node.cores_per_socket
        = m_keyfile.getString(section, "cores_per_socket", "");
    node.threads_per_core
        = m_keyfile.getString(section, "threads_per_core", "");
    node.bmc_address = m_keyfile.getString(section, "bmc_address", "");
    node.bmc_username = m_keyfile.getString(section, "bmc_username", "");
    node.bmc_password = m_keyfile.getString(section, "bmc_password", "");
    node.bmc_serialport = m_keyfile.getString(section, "bmc_serialport", "");
    node.bmc_serialspeed = m_keyfile.getString(section, "bmc_serialspeed", "");
    LOG_DEBUG("Node loaded {}", section);

    return node;
}

void AnswerFile::loadNodes()
{
    const AFNode generic = loadNode("node");
    nodes.generic = generic;

    /**
     * Some nodes will be out of order.
     * For example, the first node (node.1) will be commented out, but the
     * second node (node.2) will exist; the third (node.3) will be commented,
     * but the fourth (node.4) will exist
     *
     * The only rule we have is that the node must start at 0.
     *
     * For user convenience, we will accept files with this configuration
     */

    LOG_INFO("Enumerating nodes in the file");
    auto nodelist = m_keyfile.listAllPrefixedEntries("node.");

    LOG_INFO("Found {} possible nodes", nodelist.size());

    auto node_counter = [](std::string_view node) {
        auto num = node.substr(node.find_first_of('.') + 1);
        return boost::lexical_cast<std::size_t>(num);
    };

    auto is_node_number = [&node_counter](std::string_view node) {
        try {
            return node_counter(node) > 0;
        } catch (boost::bad_lexical_cast&) {
            return false;
        }
    };

    for (const auto& nodeSection :
        nodelist | std::views::filter(is_node_number)) {
        auto nodeCounter = node_counter(nodeSection);

        AFNode newNode = loadNode(nodeSection);

        if (newNode.hostname->empty()) {
            LOG_DEBUG("Node configured {}", newNode.hostname.value());
            if (generic.prefix->empty()) {
                throw std::invalid_argument(
                    fmt::format("Section node.{} must have a 'hostname' key or "
                                "you must inform a generic 'prefix' value",
                        nodeCounter));
            } else if (generic.padding->empty()) {
                throw std::invalid_argument(fmt::format(
                    "Section node.{} must have a 'hostname' key or you must "
                    "inform a generic 'padding' value",
                    nodeCounter));
            } else {
                newNode.hostname = fmt::format(fmt::runtime("{}{:0>{}}"),
                    generic.prefix.value(), nodeCounter,
                    stoi(generic.padding.value()));
            }
        }

        try {
            newNode = validateNode(newNode, nodeSection);
        } catch (const std::invalid_argument& e) {
            throw std::invalid_argument(
                fmt::format("Section node.{} validation failed - {}",
                    nodeCounter, e.what()));
        }

        nodes.nodes.emplace_back(newNode);
    }

    validateNodeInventoryTopology(nodes.nodes);
}

AFNode AnswerFile::validateNode(AFNode node, const std::string& section)
{
    const auto hasText = [](const std::optional<std::string>& value) {
        return value.has_value() && !value->empty();
    };
    const auto clearIfEmpty = [](std::optional<std::string>& value) {
        if (value.has_value() && value->empty()) {
            value.reset();
        }
    };

    validateAttribute(
        "node", "node_ip", node.start_ip, nodes.generic->start_ip);
    validateAttribute("node", "node_root_password", node.root_password,
        nodes.generic->root_password);
    validateAttribute("node", "sockets", node.sockets, nodes.generic->sockets);
    validateAttribute("node", "cores_per_socket", node.cores_per_socket,
        nodes.generic->cores_per_socket);
    validateAttribute("node", "threads_per_core", node.threads_per_core,
        nodes.generic->threads_per_core);

    const auto hasBMC
        = hasText(node.bmc_address) || hasText(nodes.generic->bmc_address);
    if (hasBMC) {
        validateAttribute("node", "bmc_address", node.bmc_address,
            nodes.generic->bmc_address);
        validateAttribute("node", "bmc_username", node.bmc_username,
            nodes.generic->bmc_username);
        validateAttribute("node", "bmc_password", node.bmc_password,
            nodes.generic->bmc_password);
        validateAttribute("node", "bmc_serialport", node.bmc_serialport,
            nodes.generic->bmc_serialport);
        validateAttribute("node", "bmc_serialspeed", node.bmc_serialspeed,
            nodes.generic->bmc_serialspeed);
    } else {
        clearIfEmpty(node.bmc_address);
        clearIfEmpty(node.bmc_username);
        clearIfEmpty(node.bmc_password);
        clearIfEmpty(node.bmc_serialport);
        clearIfEmpty(node.bmc_serialspeed);
    }

    validateResolvedNodeScalars(section, node);

    return node;
}

auto AnswerFile::AFNodes::nodesNames() const -> std::vector<std::string>
{
    std::uint32_t nodeIdx = 0;
    return nodes | std::views::transform([&](const auto& node) {
        nodeIdx++;
        return utils::optional::unwrap(
            node.hostname, "hostname missing for node {}", nodeIdx);
    }) | std::ranges::to<std::vector>();
}

bool AnswerFile::checkEnabled(const std::string& section)
{
    return m_keyfile.getStringOpt(section, "enabled")
        && m_keyfile.getString(section, "enabled") == "1";
}

void AnswerFile::loadPostfix()
{
    if (!m_keyfile.hasGroup("postfix"))
        return;

    LOG_TRACE("Postfix enabled");

    postfix.enabled = true;

    if (const auto destination
        = m_keyfile.getStringOpt("postfix", "destination");
        destination.has_value() && !destination->empty()) {
        boost::split(postfix.destination, destination.value(),
            boost::is_any_of(", "), boost::token_compress_on);
    }

    auto castProfile = opencattus::utils::enums::ofStringOpt<Postfix::Profile>(
        m_keyfile.getString("postfix", "profile"),
        opencattus::utils::enums::Case::Insensitive);

    if (castProfile.has_value())
        postfix.profile = castProfile.value();
    else {
        throw std::runtime_error(fmt::format("Invalid Postfix profile"));
    }

    AFPostfix::SMTP smtp;
    AFPostfix::SASL sasl;

    switch (postfix.profile) {
        case Postfix::Profile::Local:
            break;
        case Postfix::Profile::Relay:
            smtp.server = m_keyfile.getString("postfix.relay", "server");
            smtp.port = std::stoi(m_keyfile.getString("postfix.relay", "port"));
            postfix.smtp = smtp;
            break;
        case Postfix::Profile::SASL:
            smtp.server = m_keyfile.getString("postfix.sasl", "server");
            smtp.port = std::stoi(m_keyfile.getString("postfix.sasl", "port"));
            sasl.username = m_keyfile.getString("postfix.sasl", "username");
            sasl.password = m_keyfile.getString("postfix.sasl", "password");
            smtp.sasl = sasl;
            postfix.smtp = smtp;
            break;
    }

    postfix.cert_file
        = m_keyfile.getStringOpt("postfix", "smtpd_tls_cert_file").value_or("");
    postfix.key_file
        = m_keyfile.getStringOpt("postfix", "smtpd_tls_key_file").value_or("");
}

void AnswerFile::loadOFED()
{
    if (m_keyfile.hasGroup("ofed")) {
        auto kind = m_keyfile.getString("ofed", "kind");
        if (kind != "") {
            ofed.enabled = true;
            ofed.kind = kind;
            auto afVersion = m_keyfile.getString("ofed", "version");
            if (afVersion != "") {
                ofed.version = afVersion;
            } else {
                ofed.version = "latest"; // use as default
            }

            LOG_DEBUG("OFED enabled, {} {}", ofed.kind, ofed.version.value())
        }
    }
}

void AnswerFile::loadRepositories()
{
    if (!m_keyfile.hasGroup("repositories")) {
        return;
    }

    repositories.enabled = std::vector<std::string> {};
    const auto enabled = m_keyfile.getStringOpt("repositories", "enabled");
    if (!enabled.has_value() || enabled->empty()) {
        return;
    }

    std::vector<std::string> values;
    boost::split(values, enabled.value(), boost::is_any_of(", "),
        boost::token_compress_on);
    repositories.enabled = std::move(values);
}

auto AnswerFile::path() const -> const std::filesystem::path& { return m_path; }

void AnswerFile::loadSlurm()
{
    if (!m_keyfile.hasGroup("slurm")) {
        return;
    }

    using namespace opencattus::utils;
    slurm.enabled = true;
    slurm.mariadb_root_password = optional::unwrap(
        m_keyfile.getStringOpt("slurm", "mariadb_root_password"),
        "mariadb_root_password missing in the answerfile {}", path());
    slurm.slurmdb_password
        = optional::unwrap(m_keyfile.getStringOpt("slurm", "slurmdb_password"),
            "slurmdb_password missing in the answerfile {}", path());
    slurm.storage_password
        = optional::unwrap(m_keyfile.getStringOpt("slurm", "storage_password"),
            "storage_password missing in the answerfile {}", path());
    slurm.partition_name
        = optional::unwrap(m_keyfile.getStringOpt("slurm", "partition_name"),
            "partition_name missing in the answerfile {}", path());
}

void AnswerFile::dumpSlurm()
{
    if (!slurm.enabled) {
        return;
    }

    m_keyfile.setString(
        "slurm", "mariadb_root_password", slurm.mariadb_root_password);
    m_keyfile.setString("slurm", "slurmdb_password", slurm.slurmdb_password);
    m_keyfile.setString("slurm", "storage_password", slurm.storage_password);
    m_keyfile.setString("slurm", "partition_name", slurm.partition_name);
}

void AnswerFile::loadPBS()
{
    if (!m_keyfile.hasGroup("pbs")) {
        return;
    }

    pbs.enabled = true;
    const auto executionPlace
        = m_keyfile.getStringOpt("pbs", "execution_place").value_or("Shared");
    const auto parsedExecutionPlace
        = opencattus::utils::enums::ofStringOpt<PBS::ExecutionPlace>(
            executionPlace, opencattus::utils::enums::Case::Insensitive);
    if (!parsedExecutionPlace.has_value()) {
        throw std::runtime_error(
            fmt::format("Invalid PBS execution_place '{}' in answerfile {}",
                executionPlace, path()));
    }

    pbs.execution_place = parsedExecutionPlace.value();
}

void AnswerFile::dumpPBS()
{
    if (!pbs.enabled) {
        return;
    }

    m_keyfile.setString("pbs", "execution_place",
        opencattus::utils::enums::toString(pbs.execution_place));
}

};
