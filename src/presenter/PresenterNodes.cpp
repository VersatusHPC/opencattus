/*
 * Copyright 2022 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <opencattus/presenter/PresenterNodes.h>

#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

#ifdef BUILD_TESTING
#include <doctest/doctest.h>
#else
#define DOCTEST_CONFIG_DISABLE
#include <doctest/doctest.h>
#endif

#include <boost/lexical_cast.hpp>

namespace {

using boost::asio::ip::address;
using opencattus::models::CPU;
using opencattus::models::Node;
using opencattus::models::OS;

auto buildNodeName(std::string_view prefix, std::size_t index,
    std::size_t padding) -> std::string
{
    return fmt::format("{}{:0>{}}", prefix, index, padding);
}

auto offsetIPv4Address(const address& baseAddress, std::size_t offset)
{
    return boost::asio::ip::make_address_v4(
        baseAddress.to_v4().to_uint() + static_cast<std::uint32_t>(offset));
}

auto parseSize(std::string_view raw) -> std::optional<std::size_t>
{
    try {
        return boost::lexical_cast<std::size_t>(std::string(raw));
    } catch (const boost::bad_lexical_cast&) {
        return std::nullopt;
    }
}

auto suggestAddressForProfile(opencattus::models::Cluster& model,
    Network::Profile profile,
    std::initializer_list<std::uint32_t> preferredOffsets)
    -> std::optional<address>
{
    try {
        const auto& network = model.getNetwork(profile);
        const auto& connection = model.getHeadnode().getConnection(profile);
        if (!network.getAddress().is_v4() || !network.getSubnetMask().is_v4()
            || !connection.getAddress().is_v4()) {
            return std::nullopt;
        }

        const auto base = network.getAddress().to_v4().to_uint();
        const auto hostSpace = ~network.getSubnetMask().to_v4().to_uint();
        if (hostSpace < 2) {
            return std::nullopt;
        }

        auto blocked = std::unordered_set<std::uint32_t> {
            connection.getAddress().to_v4().to_uint(),
        };
        if (!network.getGateway().is_unspecified()
            && network.getGateway().is_v4()) {
            blocked.insert(network.getGateway().to_v4().to_uint());
        }

        const auto tryOffset
            = [&](std::uint32_t offset) -> std::optional<address> {
            if (offset == 0 || offset >= hostSpace) {
                return std::nullopt;
            }

            const auto candidate = base + offset;
            if (blocked.contains(candidate)) {
                return std::nullopt;
            }

            return boost::asio::ip::make_address_v4(candidate);
        };

        for (const auto offset : preferredOffsets) {
            if (const auto candidate = tryOffset(offset);
                candidate.has_value()) {
                return candidate;
            }
        }

        for (std::uint32_t offset = 1; offset < hostSpace; ++offset) {
            if (const auto candidate = tryOffset(offset);
                candidate.has_value()) {
                return candidate;
            }
        }
    } catch (const std::exception&) {
    }

    return std::nullopt;
}

auto suggestedNodeStartIP(opencattus::models::Cluster& model) -> std::string
{
    if (const auto candidate = suggestAddressForProfile(
            model, Network::Profile::Management, { 101, 1, 11, 2 });
        candidate.has_value()) {
        return candidate->to_string();
    }

    return {};
}

auto suggestedBMCStartIP(opencattus::models::Cluster& model)
    -> std::optional<address>
{
    return suggestAddressForProfile(
        model, Network::Profile::Service, { 101, 11, 2, 201 });
}

} // namespace

namespace opencattus::presenter {

PresenterNodes::PresenterNodes(
    std::unique_ptr<Cluster>& model, std::unique_ptr<View>& view)
    : Presenter(model, view)
{
    const auto suggestedNodeStart = suggestedNodeStartIP(*m_model);
    const auto suggestedBmcStart = suggestedBMCStartIP(*m_model);
    const auto suggestedBmcStartText = suggestedBmcStart.has_value()
            && suggestedBmcStart->to_string() != suggestedNodeStart
        ? suggestedBmcStart->to_string()
        : std::string {};
    m_view->message(Messages::title, Messages::message);

    auto fields = std::to_array<std::pair<std::string, std::string>>({
        { Messages::Nodes::prefix, "n" },
        { Messages::Nodes::padding, "2" },
        { Messages::Nodes::startIP, suggestedNodeStart },
        { Messages::Nodes::bmcStartIP, suggestedBmcStartText },
        { Messages::Nodes::rootPassword, "" },
        { Messages::Nodes::confirmRootPassword, "" },
    });

retry:
    fields = m_view->fieldMenu(Messages::title, Messages::Nodes::question,
        fields, Messages::Nodes::help);

    std::optional<std::size_t> parsedPadding;
    std::optional<address> parsedStartIp;
    std::optional<address> parsedBmcStartIp;

    for (const auto& field : fields) {
        if (field.first == Messages::Nodes::prefix) {
            if (field.second.empty()
                || std::isalpha(static_cast<unsigned char>(field.second[0]))
                    == 0) {
                m_view->message(Messages::title, Messages::Error::prefixLetter);
                goto retry;
            }
        }

        if (field.first == Messages::Nodes::padding) {
            parsedPadding = parseSize(field.second);
            if (!parsedPadding.has_value()) {
                m_view->message(
                    Messages::title, Messages::Error::paddingInvalid);
                goto retry;
            }

            if (parsedPadding.value() > 3) {
                m_view->message(Messages::title, Messages::Error::paddingMax);
                goto retry;
            }
        }

        if (field.first == Messages::Nodes::startIP) {
            try {
                parsedStartIp = boost::asio::ip::make_address(field.second);
            } catch (const std::exception&) {
                m_view->message(
                    Messages::title, Messages::Error::startIPInvalid);
                goto retry;
            }
        }

        if (field.first == Messages::Nodes::bmcStartIP
            && !field.second.empty()) {
            try {
                parsedBmcStartIp = boost::asio::ip::make_address(field.second);
            } catch (const std::exception&) {
                m_view->message(
                    Messages::title, Messages::Error::bmcStartIPInvalid);
                goto retry;
            }
        }
    }

    if (parsedStartIp.has_value() && parsedBmcStartIp.has_value()
        && parsedStartIp.value() == parsedBmcStartIp.value()) {
        m_view->message(
            Messages::title, Messages::Error::bmcStartIPMatchesNode);
        goto retry;
    }

    if (fields[4].second != fields[5].second) {
        m_view->message(Messages::title, Messages::Error::rootPasswordMismatch);
        goto retry;
    }

    m_model->nodePrefix = fields[0].second;
    m_model->nodePadding = parsedPadding.value();
    m_model->nodeStartIP = parsedStartIp.value();
    m_model->nodeRootPassword = fields[4].second;

    auto topology = std::to_array<std::pair<std::string, std::string>>({
        { Messages::Topology::sockets, "1" },
        { Messages::Topology::coresPerSocket, "1" },
        { Messages::Topology::threadsPerCore, "1" },
        { Messages::Topology::realMemory, "4096" },
        { Messages::Topology::bmcUsername, "admin" },
        { Messages::Topology::bmcPassword, "admin" },
        { Messages::Topology::bmcSerialPort, "0" },
        { Messages::Topology::bmcSerialSpeed, "9600" },
    });

    while (true) {
        topology = m_view->fieldMenu(Messages::title,
            Messages::Topology::question, topology, Messages::Topology::help);

        const auto sockets = parseSize(topology[0].second);
        const auto coresPerSocket = parseSize(topology[1].second);
        const auto threadsPerCore = parseSize(topology[2].second);
        const auto realMemory = parseSize(topology[3].second);
        const auto serialPort = parseSize(topology[6].second);
        const auto serialSpeed = parseSize(topology[7].second);

        if (!sockets.has_value() || !coresPerSocket.has_value()
            || !threadsPerCore.has_value() || !realMemory.has_value()
            || !serialPort.has_value() || !serialSpeed.has_value()) {
            m_view->message(Messages::title, Messages::Error::topologyInvalid);
            continue;
        }

        m_model->nodeSockets = sockets.value();
        m_model->nodeCoresPerSocket = coresPerSocket.value();
        m_model->nodeThreadsPerCore = threadsPerCore.value();
        m_model->nodeRealMemory = realMemory.value();
        m_model->nodeBMCUsername = topology[4].second;
        m_model->nodeBMCPassword = topology[5].second;
        m_model->nodeBMCSerialPort = serialPort.value();
        m_model->nodeBMCSerialSpeed = serialSpeed.value();
        break;
    }

    m_model->nodeCPUsPerNode = m_model->nodeSockets
        * m_model->nodeCoresPerSocket * m_model->nodeThreadsPerCore;

    auto nodes = std::to_array<std::pair<std::string, std::string>>({
        { Messages::Quantity::nodes, "" },
    });

    while (true) {
        nodes = m_view->fieldMenu(Messages::title, Messages::Quantity::question,
            nodes, Messages::Quantity::help);

        if (const auto parsedQuantity = parseSize(nodes[0].second);
            parsedQuantity.has_value() && parsedQuantity.value() > 0) {
            m_model->nodeQuantity = parsedQuantity.value();
            break;
        }

        m_view->message(Messages::title, Messages::Error::quantityInvalid);
    }

    auto nodeOS = m_model->getComputeNodeOS();
    CPU nodeCPU(m_model->nodeSockets, m_model->nodeCoresPerSocket,
        m_model->nodeThreadsPerCore);
    Network* managementNetwork = nullptr;
    try {
        managementNetwork = &m_model->getNetwork(Network::Profile::Management);
    } catch (const std::exception& ex) {
        m_view->fatalMessage(Messages::title,
            fmt::format("Management network is not configured: {}", ex.what())
                .c_str());
    }

    for (std::size_t node { 1 }; node <= m_model->nodeQuantity; ++node) {
        const auto nodeName
            = buildNodeName(m_model->nodePrefix, node, m_model->nodePadding);
        auto nodeFields = std::to_array<std::pair<std::string, std::string>>({
            { Messages::NodeEntry::macAddress, "" },
            { Messages::NodeEntry::bmcAddress,
                parsedBmcStartIp.has_value()
                    ? offsetIPv4Address(parsedBmcStartIp.value(), node - 1)
                          .to_string()
                    : std::string {} },
        });
        const auto nodeQuestion
            = fmt::format("{}: {}", Messages::NodeEntry::question, nodeName);

        while (true) {
            nodeFields = m_view->fieldMenu(Messages::title,
                nodeQuestion.c_str(), nodeFields, Messages::NodeEntry::help);

            try {
                const auto nodeAddress
                    = offsetIPv4Address(m_model->nodeStartIP, node - 1);
                std::optional<BMC> bmc = std::nullopt;
                if (!nodeFields[1].second.empty()) {
                    const auto bmcParsedAddress
                        = boost::asio::ip::make_address(nodeFields[1].second);
                    if (bmcParsedAddress == nodeAddress) {
                        m_view->message(Messages::title,
                            Messages::Error::bmcAddressMatchesNode);
                        continue;
                    }

                    bmc = BMC(bmcParsedAddress.to_string(),
                        m_model->nodeBMCUsername, m_model->nodeBMCPassword,
                        m_model->nodeBMCSerialPort, m_model->nodeBMCSerialSpeed,
                        BMC::kind::IPMI);
                }

                std::list<Connection> nodeConnections;
                auto& connection
                    = nodeConnections.emplace_back(managementNetwork);
                connection.setMAC(nodeFields[0].second);
                connection.setAddress(nodeAddress);

                Node newNode(
                    nodeName, nodeOS, nodeCPU, std::move(nodeConnections), bmc);
                newNode.setPrefix(
                    std::optional<std::string> { m_model->nodePrefix });
                newNode.setPadding(
                    std::optional<std::size_t> { m_model->nodePadding });
                newNode.setNodeStartIp(std::optional<address> { nodeAddress });
                newNode.setMACAddress(nodeFields[0].second);
                newNode.setNodeRootPassword(
                    std::optional<std::string> { m_model->nodeRootPassword });
                m_model->addNode(newNode);
                break;
            } catch (const std::exception& ex) {
                m_view->message(Messages::title,
                    fmt::format("Invalid node definition for {}: {}", nodeName,
                        ex.what())
                        .c_str());
            }
        }
    }
}

}; // namespace opencattus::presenter

TEST_CASE("buildNodeName zero pads node indexes")
{
    CHECK(buildNodeName("n", 7, 2) == "n07");
}

TEST_CASE("offsetIPv4Address increments node addresses deterministically")
{
    CHECK(offsetIPv4Address(boost::asio::ip::make_address("192.168.30.1"), 4)
              .to_string()
        == "192.168.30.5");
}
