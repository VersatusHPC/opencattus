/*
 * Copyright 2026 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <opencattus/models/pbs.h>
#include <opencattus/presenter/PresenterPreflight.h>
#include <opencattus/services/repos.h>
#include <opencattus/utils/enums.h>

#include <algorithm>
#include <fmt/ranges.h>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace {

using opencattus::models::Cluster;
using opencattus::models::OS;
using opencattus::models::PBS;
using opencattus::models::QueueSystem;

auto provisionerName(Cluster::Provisioner provisioner) -> std::string
{
    switch (provisioner) {
        case Cluster::Provisioner::xCAT:
            return "xCAT";
        case Cluster::Provisioner::Confluent:
            return "Confluent";
        default:
            std::unreachable();
    }
}

auto compatibilitySummary(const Cluster& model) -> std::string
{
    const auto& os = model.getHeadnode().getOS();
    const auto provisioner = model.getProvisioner();
    const auto target = fmt::format("{} {} {}",
        opencattus::utils::enums::toString(os.getDistro()), os.getVersion(),
        opencattus::utils::enums::toString(os.getArch()));

    if (os.getPlatform() == OS::Platform::el10
        && provisioner == Cluster::Provisioner::xCAT) {
        return fmt::format(
            "Invalid: {} with xCAT is not supported today", target);
    }

    return fmt::format("OK: {} with {}", target, provisionerName(provisioner));
}

auto cidrSuffixFor(const Network& network) -> std::string
{
    const auto subnetMask = network.getSubnetMask().to_string();
    if (const auto it = Network::cidr.find(subnetMask);
        it != Network::cidr.end()) {
        return fmt::format("/{}", static_cast<int>(it->second));
    }

    return fmt::format(" mask {}", subnetMask);
}

auto networkSummaryRows(Cluster& model) -> std::vector<std::string>
{
    std::vector<std::string> entries;
    for (const auto& network : model.getNetworks()) {
        const auto profile
            = opencattus::utils::enums::toString(network->getProfile());
        const auto type
            = opencattus::utils::enums::toString(network->getType());
        const auto networkAddress
            = network->getAddress().to_string() + cidrSuffixFor(*network);
        const auto gateway = network->getGateway().is_unspecified()
            ? std::string("no gateway")
            : fmt::format("gw {}", network->getGateway().to_string());

        try {
            const auto& connection
                = model.getHeadnode().getConnection(network->getProfile());
            const auto interface = connection.getInterface().has_value()
                ? std::string(connection.getInterface().value())
                : std::string("no interface");
            entries.emplace_back(fmt::format("{} {} {} host {} on {} ({})",
                profile, type, interface, connection.getAddress().to_string(),
                networkAddress, gateway));
        } catch (const std::exception& ex) {
            entries.emplace_back(fmt::format(
                "{} {} {} ({})", profile, type, networkAddress, ex.what()));
        }
    }

    if (entries.empty()) {
        return { "No networks configured" };
    }

    return entries;
}

auto bmcSummary(const Cluster& model) -> std::string
{
    const auto& nodes = model.getNodes();
    if (nodes.empty()) {
        return "No compute nodes configured";
    }

    const auto bmcCount = static_cast<std::size_t>(std::ranges::count_if(
        nodes, [](const auto& node) { return node.getBMC().has_value(); }));

    if (bmcCount == 0) {
        return fmt::format(
            "No BMC configured for {} nodes; power control will be skipped",
            nodes.size());
    }

    const auto firstBmcNode = std::ranges::find_if(
        nodes, [](const auto& node) { return node.getBMC().has_value(); });
    const auto firstBmcAddress = firstBmcNode == nodes.end()
        ? std::string("none")
        : firstBmcNode->getBMC()->getAddress();

    return fmt::format("{} of {} nodes have BMC; first BMC {}", bmcCount,
        nodes.size(), firstBmcAddress);
}

auto repositorySummary(const Cluster& model) -> std::string
{
    const auto& enabledRepositories = model.getEnabledRepositories();
    if (!enabledRepositories.has_value() || enabledRepositories->empty()) {
        return "Mandatory repositories only";
    }

    auto repositories = enabledRepositories.value();
    auto summary = fmt::format("Optional: {}", fmt::join(repositories, ", "));

    if (std::ranges::find(repositories, "beegfs") != repositories.end()) {
        summary += "; BeeGFS implies grafana and influxdata";
    }

    return summary;
}

auto isoSummary(const Cluster& model) -> std::string
{
    const auto& os = model.getHeadnode().getOS();
    return fmt::format("{} {} from {}",
        opencattus::utils::enums::toString(os.getDistro()), os.getVersion(),
        model.getDiskImage().getPath().string());
}

auto queueSummary(const Cluster& model) -> std::string
{
    const auto& queue = model.getQueueSystem();
    if (!queue.has_value()) {
        return "No queue system configured";
    }

    switch (queue.value()->getKind()) {
        case QueueSystem::Kind::SLURM:
            return fmt::format(
                "SLURM partition {}", queue.value()->getDefaultQueue());
        case QueueSystem::Kind::PBS: {
            auto* pbs = dynamic_cast<PBS*>(queue.value().get());
            const auto place = pbs == nullptr
                ? std::string("unknown")
                : opencattus::utils::enums::toString(pbs->getExecutionPlace());
            return fmt::format("PBS Professional execution place {}", place);
        }
        case QueueSystem::Kind::None:
            return "No queue system configured";
        default:
            std::unreachable();
    }
}

auto nodeAddressSummary(const opencattus::models::Node& node) -> std::string
{
    if (node.getNodeStartIp().has_value()) {
        return node.getNodeStartIp()->to_string();
    }

    try {
        return node.getConnection(Network::Profile::Management)
            .getAddress()
            .to_string();
    } catch (const std::exception&) {
        return "-";
    }
}

auto bmcAddressSummary(const opencattus::models::Node& node) -> std::string
{
    if (!node.getBMC().has_value()) {
        return "-";
    }

    return node.getBMC()->getAddress();
}

auto fitColumn(std::string value, std::size_t width) -> std::string
{
    if (value.size() <= width) {
        return value;
    }

    if (width == 0) {
        return "";
    }

    value.resize(width);
    value.back() = '~';
    return value;
}

void appendNodeTable(std::vector<std::string>& rows, const Cluster& model)
{
    rows.emplace_back("[Nodes]");
    rows.emplace_back(fmt::format("{:<11} {:<15} {:<15} {:<17}", "Hostname",
        "Node IP", "BMC IP", "MAC address"));

    const auto& nodes = model.getNodes();
    if (nodes.empty()) {
        rows.emplace_back("  No compute nodes configured");
        return;
    }

    for (const auto& node : nodes) {
        rows.emplace_back(fmt::format("{:<11} {:<15} {:<15} {:<17}",
            fitColumn(node.getHostname(), 11),
            fitColumn(nodeAddressSummary(node), 15),
            fitColumn(bmcAddressSummary(node), 15),
            fitColumn(node.getMACAddress().empty() ? "-" : node.getMACAddress(),
                17)));
    }
}

auto buildPreflightText(Cluster& model) -> std::string
{
    std::vector<std::string> rows;
    rows.emplace_back(
        fmt::format("{:<14} {}", "Compatibility", compatibilitySummary(model)));
    rows.emplace_back(
        fmt::format("{:<14} {}", "ISO and OS", isoSummary(model)));
    rows.emplace_back(fmt::format("{:<14} {}", "BMC", bmcSummary(model)));
    rows.emplace_back(
        fmt::format("{:<14} {}", "Repositories", repositorySummary(model)));
    rows.emplace_back(
        fmt::format("{:<14} {}", "Queue system", queueSummary(model)));

    rows.emplace_back("[Networks]");
    for (const auto& network : networkSummaryRows(model)) {
        rows.emplace_back(fmt::format("  {}", network));
    }

    appendNodeTable(rows, model);

    return fmt::format("{}", fmt::join(rows, "\n"));
}

} // namespace

namespace opencattus::presenter {

PresenterPreflight::PresenterPreflight(
    std::unique_ptr<Cluster>& model, std::unique_ptr<View>& view)
    : Presenter(model, view)
{
    const auto preflightText = buildPreflightText(*m_model);
    m_view->scrollableMessage(Messages::title, Messages::question,
        preflightText.c_str(), Messages::help);
}

}
