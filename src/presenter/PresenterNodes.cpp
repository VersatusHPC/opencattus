/*
 * Copyright 2022 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <opencattus/presenter/PresenterNodes.h>

#include <cctype>
#include <cstdint>
#include <optional>
#include <string_view>

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

auto buildNodeName(
    std::string_view prefix, std::size_t index, std::size_t padding)
    -> std::string
{
    return fmt::format("{}{:0>{}}", prefix, index, padding);
}

auto offsetIPv4Address(const address& baseAddress, std::size_t offset)
{
    return boost::asio::ip::make_address_v4(
        baseAddress.to_v4().to_uint() + static_cast<std::uint32_t>(offset));
}

} // namespace

namespace opencattus::presenter {

PresenterNodes::PresenterNodes(
    std::unique_ptr<Cluster>& model, std::unique_ptr<Newt>& view)
    : Presenter(model, view)
{

    m_view->message(Messages::title, Messages::message);

    auto fields = std::to_array<std::pair<std::string, std::string>>({
        { Messages::Nodes::prefix, "n" }, { Messages::Nodes::padding, "2" },
        { Messages::Nodes::startIP, "" },
        { Messages::Nodes::rootPassword, "" },
        { Messages::Nodes::confirmRootPassword, "" },
    });

retry:
    fields = m_view->fieldMenu(Messages::title, Messages::Nodes::question,
        fields, Messages::Nodes::help);

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
            if (boost::lexical_cast<std::size_t>(field.second) > 3) {
                m_view->message(Messages::title, Messages::Error::paddingMax);
                goto retry;
            }
        }
    }

    if (fields[3].second != fields[4].second) {
        m_view->message(
            Messages::title, Messages::Error::rootPasswordMismatch);
        goto retry;
    }

    std::size_t i { 0 };
    m_model->nodePrefix = fields[i++].second;
    m_model->nodePadding = boost::lexical_cast<std::size_t>(fields[i++].second);
    m_model->nodeStartIP = boost::asio::ip::make_address(fields[i++].second);
    m_model->nodeRootPassword = fields[i++].second;

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

    topology = m_view->fieldMenu(Messages::title,
        Messages::Topology::question, topology, Messages::Topology::help);

    i = 0;
    m_model->nodeSockets = boost::lexical_cast<std::size_t>(topology[i++].second);
    m_model->nodeCoresPerSocket
        = boost::lexical_cast<std::size_t>(topology[i++].second);
    m_model->nodeThreadsPerCore
        = boost::lexical_cast<std::size_t>(topology[i++].second);
    m_model->nodeRealMemory
        = boost::lexical_cast<std::size_t>(topology[i++].second);
    m_model->nodeBMCUsername = topology[i++].second;
    m_model->nodeBMCPassword = topology[i++].second;
    m_model->nodeBMCSerialPort
        = boost::lexical_cast<std::size_t>(topology[i++].second);
    m_model->nodeBMCSerialSpeed
        = boost::lexical_cast<std::size_t>(topology[i++].second);
    m_model->nodeCPUsPerNode = m_model->nodeSockets
        * m_model->nodeCoresPerSocket * m_model->nodeThreadsPerCore;

    auto nodes = std::to_array<std::pair<std::string, std::string>>({
        { Messages::Quantity::nodes, "" },
    });

    i = 0;
    nodes = m_view->fieldMenu(Messages::title, Messages::Quantity::question,
        nodes, Messages::Quantity::help);

    m_model->nodeQuantity = boost::lexical_cast<std::size_t>(nodes[i++].second);

    auto nodeOS = m_model->getHeadnode().getOS();
    CPU nodeCPU(m_model->nodeSockets, m_model->nodeCoresPerSocket,
        m_model->nodeThreadsPerCore);
    auto& managementNetwork = m_model->getNetwork(Network::Profile::Management);

    for (std::size_t node { 1 }; node <= m_model->nodeQuantity; ++node) {
        const auto nodeName
            = buildNodeName(m_model->nodePrefix, node, m_model->nodePadding);
        auto nodeFields = std::to_array<std::pair<std::string, std::string>>({
            { Messages::NodeEntry::macAddress, "" },
            { Messages::NodeEntry::bmcAddress, "" },
        });

        nodeFields = m_view->fieldMenu(Messages::title,
            fmt::format("{}: {}", Messages::NodeEntry::question, nodeName)
                .c_str(),
            nodeFields, Messages::NodeEntry::help);

        const auto nodeAddress = offsetIPv4Address(m_model->nodeStartIP, node - 1);
        BMC bmc(nodeFields[1].second, m_model->nodeBMCUsername,
            m_model->nodeBMCPassword, m_model->nodeBMCSerialPort,
            m_model->nodeBMCSerialSpeed, BMC::kind::IPMI);

        std::list<Connection> nodeConnections;
        auto& connection = nodeConnections.emplace_back(&managementNetwork);
        connection.setMAC(nodeFields[0].second);
        connection.setAddress(nodeAddress);

        Node newNode(nodeName, nodeOS, nodeCPU, std::move(nodeConnections), bmc);
        newNode.setPrefix(std::optional<std::string> { m_model->nodePrefix });
        newNode.setPadding(std::optional<std::size_t> { m_model->nodePadding });
        newNode.setNodeStartIp(std::optional<address> { nodeAddress });
        newNode.setMACAddress(nodeFields[0].second);
        newNode.setNodeRootPassword(
            std::optional<std::string> { m_model->nodeRootPassword });
        m_model->addNode(newNode);
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
