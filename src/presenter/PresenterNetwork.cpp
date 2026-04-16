/*
 * Copyright 2022 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <boost/algorithm/string.hpp>
#include <opencattus/presenter/PresenterNetwork.h>

// Maybe a central NetworkCreator class can be added here?
// The PresenterNetwork receives the NetworkCreator alongside both parameters,
// and just add them to the NetworkInfo. After that, the NetworkInfo, with
// proper methods, adds them to the model

#include <algorithm>

#ifdef BUILD_TESTING
#include <doctest/doctest.h>
#else
#define DOCTEST_CONFIG_DISABLE
#include <doctest/doctest.h>
#endif

namespace {

auto isInfinibandInterface(std::string_view interface) -> bool
{
    return interface.starts_with("ib");
}

auto interfaceMatchesType(std::string_view interface, Network::Type type)
    -> bool
{
    switch (type) {
        case Network::Type::Ethernet:
            return !isInfinibandInterface(interface);
        case Network::Type::Infiniband:
            return isInfinibandInterface(interface);
        default:
            __builtin_unreachable();
    }
}

auto canShareInterface(
    Network::Profile existingProfile, Network::Profile requestedProfile) -> bool
{
    return (existingProfile == Network::Profile::Management
               && requestedProfile == Network::Profile::Service)
        || (existingProfile == Network::Profile::Service
            && requestedProfile == Network::Profile::Management);
}

auto isUsableQuestionnaireInterface(
    std::string_view interface, Network::Type type) -> bool
{
    if (!interfaceMatchesType(interface, type)) {
        return false;
    }

    try {
        static_cast<void>(Connection::fetchAddress(std::string(interface)));
        static_cast<void>(Network::fetchSubnetMask(std::string(interface)));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

auto gatewayLabelFor(Network::Profile profile) -> const char*
{
    switch (profile) {
        case Network::Profile::Service:
        case Network::Profile::Application:
            return "Gateway (optional)";
        case Network::Profile::External:
        case Network::Profile::Management:
            return "Gateway";
        default:
            __builtin_unreachable();
    }
}

auto fetchOptionalGateway(const std::string& interface) -> std::string
{
    try {
        const auto gateway = Network::fetchGateway(interface);
        if (gateway.is_unspecified()) {
            return {};
        }

        return gateway.to_string();
    } catch (const std::exception&) {
        return {};
    }
}

auto fetchOptionalDomainName() -> std::string
{
    try {
        return Network::fetchDomainName();
    } catch (const std::exception&) {
        return {};
    }
}

auto fetchOptionalNameservers() -> std::vector<address>
{
    try {
        return Network::fetchNameservers();
    } catch (const std::exception&) {
        return {};
    }
}

auto parseNameservers(std::string_view raw) -> std::vector<address>
{
    std::vector<address> parsed;
    std::vector<std::string> parts;
    boost::split(parts, std::string(raw), boost::is_any_of(", "),
        boost::token_compress_on);

    for (const auto& part : parts) {
        if (!part.empty()) {
            parsed.emplace_back(boost::asio::ip::make_address(part));
        }
    }

    return parsed;
}

auto formatNameservers(const std::vector<address>& nameservers) -> std::string
{
    std::vector<std::string> formatted;
    formatted.reserve(nameservers.size());

    for (const auto& nameserver : nameservers) {
        formatted.emplace_back(nameserver.to_string());
    }

    return fmt::format("{}", fmt::join(formatted, ", "));
}

auto calculateNetworkAddress(
    const address& connectionAddress, const address& subnetMask) -> address
{
    if (!connectionAddress.is_v4() || !subnetMask.is_v4()) {
        throw std::runtime_error("Only IPv4 network details are supported");
    }

    return boost::asio::ip::make_address_v4(
        connectionAddress.to_v4().to_uint() & subnetMask.to_v4().to_uint());
}

enum class SharedServiceNetworkStatus {
    valid,
    sharedIP,
    sharedSubnet,
    invalid,
};

enum class GatewayNetworkStatus {
    valid,
    outsideSubnet,
    invalid,
};

auto validateGatewaySubnet(const std::string& addressText,
    const std::string& subnetMaskText, const std::string& gatewayText)
    -> GatewayNetworkStatus
{
    if (gatewayText.empty()) {
        return GatewayNetworkStatus::valid;
    }

    try {
        const auto networkAddress = boost::asio::ip::make_address(addressText);
        const auto subnetMask = boost::asio::ip::make_address(subnetMaskText);
        const auto gateway = boost::asio::ip::make_address(gatewayText);

        if (!networkAddress.is_v4() || !subnetMask.is_v4()
            || !gateway.is_v4()) {
            return GatewayNetworkStatus::invalid;
        }

        if (gateway.is_unspecified()) {
            return GatewayNetworkStatus::valid;
        }

        if (calculateNetworkAddress(networkAddress, subnetMask)
            != calculateNetworkAddress(gateway, subnetMask)) {
            return GatewayNetworkStatus::outsideSubnet;
        }
    } catch (const std::exception&) {
        return GatewayNetworkStatus::invalid;
    }

    return GatewayNetworkStatus::valid;
}

auto validateSharedServiceNetwork(
    const opencattus::presenter::NetworkCreatorData& sharedDefaults,
    const std::string& addressText, const std::string& subnetMaskText)
    -> SharedServiceNetworkStatus
{
    try {
        const auto serviceAddress = boost::asio::ip::make_address(addressText);
        const auto serviceSubnetMask
            = boost::asio::ip::make_address(subnetMaskText);
        const auto sharedAddress
            = boost::asio::ip::make_address(sharedDefaults.address);
        const auto sharedSubnetMask
            = boost::asio::ip::make_address(sharedDefaults.subnetMask);

        if (serviceAddress == sharedAddress) {
            return SharedServiceNetworkStatus::sharedIP;
        }

        if (calculateNetworkAddress(serviceAddress, serviceSubnetMask)
            == calculateNetworkAddress(sharedAddress, sharedSubnetMask)) {
            return SharedServiceNetworkStatus::sharedSubnet;
        }
    } catch (const std::exception&) {
        return SharedServiceNetworkStatus::invalid;
    }

    return SharedServiceNetworkStatus::valid;
}

} // namespace

namespace opencattus::presenter {

bool NetworkCreator::checkIfProfileExists(Network::Profile profile)
{
    namespace ranges = std::ranges;
    if (auto it = ranges::find_if(
            m_networks, [profile](auto& n) { return n.profile == profile; });
        it != m_networks.end()) {
        return true;
    }

    return false;
}

bool NetworkCreator::addNetworkInformation(NetworkCreatorData&& data)
{
    if (checkIfProfileExists(data.profile)) {
        return false;
    }

    m_networks.push_back(data);
    return true;
}

bool NetworkCreator::canUseInterface(
    std::string_view interface, Network::Profile requestedProfile) const
{
    for (const auto& network : m_networks) {
        if (network.interface != interface) {
            continue;
        }

        if (!canShareInterface(network.profile, requestedProfile)) {
            return false;
        }
    }

    return true;
}

const NetworkCreatorData* NetworkCreator::findByInterface(
    std::string_view interface) const
{
    namespace ranges = std::ranges;

    if (auto it = ranges::find_if(m_networks,
            [interface](const auto& network) {
                return network.interface == interface;
            });
        it != m_networks.end()) {
        return &*it;
    }

    return nullptr;
}

const NetworkCreatorData* NetworkCreator::findByProfile(
    Network::Profile profile) const
{
    namespace ranges = std::ranges;

    if (auto it = ranges::find_if(m_networks,
            [profile](
                const auto& network) { return network.profile == profile; });
        it != m_networks.end()) {
        return &*it;
    }

    return nullptr;
}

std::size_t NetworkCreator::getSelectedInterfaces()
{
    return m_networks.size();
}

void NetworkCreator::saveNetworksToModel(Cluster& model)
{
    for (const auto& net : m_networks) {
        auto netptr = std::make_unique<Network>(net.profile, net.type);
        Connection conn(netptr.get());

        conn.setAddress(net.address);
        conn.setInterface(net.interface);

        netptr->setSubnetMask(net.subnetMask);
        netptr->setAddress(netptr->calculateAddress(conn.getAddress()));
        if (!net.gateway.empty()) {
            netptr->setGateway(net.gateway);
        }
        netptr->setDomainName(net.name);
        netptr->setNameservers(net.domains);

        LOG_TRACE("Moved m_network and m_connection into m_model")
        model.addNetwork(std::move(netptr));
        model.getHeadnode().addConnection(std::move(conn));

        // Check moved data
        LOG_DEBUG("Added {} connection on headnode: {} -> {}",
            opencattus::utils::enums::toString(net.profile),
            model.getHeadnode()
                .getConnection(net.profile)
                .getInterface()
                .value(),
            model.getHeadnode()
                .getConnection(net.profile)
                .getAddress()
                .to_string());
    }
}

// FIXME: I don't like this code, it's ugly.
//  *** The following ideas were implemented, although not properly tested, so
//  *** we leave this comment here.
//  * Lifecycle may be a problem: what happens if any function throws after
//  adding the Network or Connection? A ghost object will exist without proper
//  data.
//  * If an exception is thrown inside the constructor, the destructor will not
//  be called, leaving garbage on the model.
//  * Try and catch blocks should be added to avoid all those conditions.
//
// Most of issues above are being solved:
//  - we moved the network/connection addition to later, after we destroy
//    this class. We move the data using the NetworkCreator class, and create
//    things there.
//
// TODO: Just an idea, instead of undoing changes on the model, we may
//  instantiate a Network and a Connection object and after setting up it's
//  attributes we just copy them to the right place or even better, we move it.
//  After the end of this class the temporary objects will be destroyed anyway.

PresenterNetwork::PresenterNetwork(std::unique_ptr<Cluster>& model,
    std::unique_ptr<View>& view, NetworkCreator& nc, Network::Profile profile,
    Network::Type type)
    : Presenter(model, view)
    , m_network(std::make_unique<Network>(profile, type))
{
    LOG_DEBUG("Added {} network with type {}",
        opencattus::utils::enums::toString(profile),
        opencattus::utils::enums::toString(type));

    LOG_DEBUG("Added connection to {} network",
        opencattus::utils::enums::toString(profile));

    // TODO: This should be on the header and be constexpr (if possible)
    m_view->message(Messages::title,
        fmt::format(
            "We will now ask questions about your {} ({}) network interface",
            opencattus::utils::enums::toString(profile),
            opencattus::utils::enums::toString(type))
            .c_str());

    auto interfaces = retrievePossibleInterfaces(nc);
    if (interfaces.empty()) {
        throw std::runtime_error(
            fmt::format("No usable {} ({}) network interfaces remain available",
                opencattus::utils::enums::toString(profile),
                opencattus::utils::enums::toString(type)));
    }

    auto available = interfaces.size() + nc.getSelectedInterfaces();
    if (available < 2) {
        m_view->fatalMessage(Messages::title, Messages::errorInsufficient);
    }

    NetworkCreatorData ncd;
    ncd.type = type;
    ncd.profile = profile;
    createNetwork(interfaces, nc, ncd);
    nc.addNetworkInformation(std::move(ncd));
}

std::vector<std::string> PresenterNetwork::retrievePossibleInterfaces(
    NetworkCreator& nc)
{
    namespace ranges = std::ranges;

    // Get the network interface
    auto ifs = Connection::fetchInterfaces();

    const auto [last, end] = ranges::remove_if(ifs, [this, &nc](const auto& i) {
        return !isUsableQuestionnaireInterface(i, m_network->getType())
            || !nc.canUseInterface(i, m_network->getProfile());
    });
    ifs.erase(last, end);
    ranges::sort(ifs);
    return ifs;
}

void PresenterNetwork::createNetwork(
    const std::vector<std::string>& interfaceList, NetworkCreator& nc,
    NetworkCreatorData& ncd)
{

    std::string interface = networkInterfaceSelection(interfaceList);
    const auto* sharedDefaults = nc.findByInterface(interface);
    const auto* managementDefaults
        = nc.findByProfile(Network::Profile::Management);

    std::vector<address> nameservers;
    if (sharedDefaults != nullptr && !sharedDefaults->domains.empty()) {
        nameservers = sharedDefaults->domains;
    } else {
        nameservers = fetchOptionalNameservers();
        if (nameservers.empty()) {
            if (managementDefaults != nullptr) {
                nameservers = managementDefaults->domains;
            } else if (const auto* externalDefaults
                = nc.findByProfile(Network::Profile::External);
                externalDefaults != nullptr) {
                nameservers = externalDefaults->domains;
            }
        }
    }

    auto gateway = fetchOptionalGateway(interface);
    if (ncd.profile == Network::Profile::Service && sharedDefaults != nullptr) {
        gateway.clear();
    } else if (sharedDefaults != nullptr && !sharedDefaults->gateway.empty()) {
        gateway = sharedDefaults->gateway;
    }

    auto domainName = fetchOptionalDomainName();
    if (sharedDefaults != nullptr && !sharedDefaults->name.empty()) {
        domainName = sharedDefaults->name;
    } else if (!m_model->getDomainName().empty()) {
        domainName = m_model->getDomainName();
    }

    auto networkDetails = std::to_array<std::pair<std::string, std::string>>(
        { { Messages::IP::address,
              Connection::fetchAddress(interface).to_string() },
            { Messages::IP::subnetMask,
                Network::fetchSubnetMask(interface).to_string() },
            { gatewayLabelFor(ncd.profile), gateway },
            // Nameserver definitions
            { Messages::Domain::name, domainName },
            { Messages::Domain::servers, formatNameservers(nameservers) } });

    while (true) {
        // TODO: Can we use move semantics?
        networkDetails = networkAddress(networkDetails);

        switch (validateGatewaySubnet(networkDetails[0].second,
            networkDetails[1].second, networkDetails[2].second)) {
            case GatewayNetworkStatus::valid:
                break;
            case GatewayNetworkStatus::outsideSubnet:
                m_view->message(
                    Messages::title, Messages::Error::gatewayOutsideSubnet);
                continue;
            case GatewayNetworkStatus::invalid:
                m_view->message(
                    Messages::title, Messages::Error::gatewayDetailsInvalid);
                continue;
        }

        if (ncd.profile == Network::Profile::Service
            && managementDefaults != nullptr) {
            switch (validateSharedServiceNetwork(*managementDefaults,
                networkDetails[0].second, networkDetails[1].second)) {
                case SharedServiceNetworkStatus::valid:
                    break;
                case SharedServiceNetworkStatus::sharedIP:
                    m_view->message(Messages::title, Messages::Error::sharedIP);
                    continue;
                case SharedServiceNetworkStatus::sharedSubnet:
                    m_view->message(
                        Messages::title, Messages::Error::sharedSubnet);
                    continue;
                case SharedServiceNetworkStatus::invalid:
                    m_view->message(
                        Messages::title, Messages::Error::sharedDetailsInvalid);
                    continue;
            }
        }

        break;
    }

#ifndef NDEBUG
    networkConfirmation(networkDetails);
#endif

    // Set the gathered data
    std::size_t i = 0;

    ncd.interface = interface;
    ncd.address = networkDetails[i++].second;
    ncd.subnetMask = networkDetails[i++].second;
    ncd.gateway = networkDetails[i++].second;

    // Domain Data
    ncd.name = networkDetails[i++].second;
    ncd.domains = parseNameservers(networkDetails[i++].second);
}

}

TEST_CASE("NetworkCreator only allows service and management to share an "
          "interface")
{
    using opencattus::presenter::NetworkCreator;
    using opencattus::presenter::NetworkCreatorData;

    NetworkCreator nc;
    CHECK(nc.addNetworkInformation(NetworkCreatorData {
        .profile = Network::Profile::External,
        .type = Network::Type::Ethernet,
        .interface = "eno1",
    }));
    CHECK_FALSE(nc.canUseInterface("eno1", Network::Profile::Management));

    CHECK(nc.addNetworkInformation(NetworkCreatorData {
        .profile = Network::Profile::Management,
        .type = Network::Type::Ethernet,
        .interface = "eno2",
    }));
    CHECK(nc.canUseInterface("eno2", Network::Profile::Service));
    CHECK_FALSE(nc.canUseInterface("eno2", Network::Profile::Application));
}
