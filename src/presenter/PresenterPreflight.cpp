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
#include <optional>
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

auto osSummary(const OS& os) -> std::string
{
    return fmt::format("{} {} {}",
        opencattus::utils::enums::toString(os.getDistro()), os.getVersion(),
        opencattus::utils::enums::toString(os.getArch()));
}

auto nodeOperatingSystemSummary(const Cluster& model) -> std::string
{
    const auto& nodes = model.getNodes();
    if (nodes.empty()) {
        return osSummary(model.getHeadnode().getOS());
    }

    std::vector<std::string> summaries;
    for (const auto& node : nodes) {
        const auto summary = osSummary(node.getOS());
        if (std::ranges::find(summaries, summary) == summaries.end()) {
            summaries.emplace_back(summary);
        }
    }

    if (summaries.size() == 1) {
        return summaries.front();
    }

    return fmt::format("Mixed: {}", fmt::join(summaries, ", "));
}

auto compatibilityWarning(const Cluster& model) -> std::optional<std::string>
{
    const auto& os = model.getHeadnode().getOS();
    if (os.getPlatform() == OS::Platform::el10
        && model.getProvisioner() == Cluster::Provisioner::xCAT) {
        return "xCAT does not support EL10 today";
    }

    return std::nullopt;
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

void appendNetworkDetails(std::vector<std::string>& rows, Cluster& model)
{
    rows.emplace_back("");
    rows.emplace_back("[Networks]");

    if (model.getNetworks().empty()) {
        rows.emplace_back("No networks configured");
        return;
    }

    auto firstNetwork = true;
    for (const auto& network : model.getNetworks()) {
        const auto profile
            = opencattus::utils::enums::toString(network->getProfile());
        const auto type
            = opencattus::utils::enums::toString(network->getType());
        const auto networkAddress
            = network->getAddress().to_string() + cidrSuffixFor(*network);
        const auto gateway = network->getGateway().is_unspecified()
            ? std::string("none")
            : network->getGateway().to_string();

        if (!firstNetwork) {
            rows.emplace_back("");
        }
        firstNetwork = false;

        rows.emplace_back(fmt::format("{} {}", profile, type));

        try {
            const auto& connection
                = model.getHeadnode().getConnection(network->getProfile());
            const auto interface = connection.getInterface().has_value()
                ? std::string(connection.getInterface().value())
                : std::string("no interface");
            rows.emplace_back(
                fmt::format("  {:<9} {}", "Interface", interface));
            rows.emplace_back(fmt::format(
                "  {:<9} {}", "Host IP", connection.getAddress().to_string()));
        } catch (const std::exception& ex) {
            rows.emplace_back(fmt::format("  {:<9} {}", "Host IP", "-"));
            rows.emplace_back(fmt::format("  {:<9} {}", "Warning", ex.what()));
        }
        rows.emplace_back(fmt::format("  {:<9} {}", "Network", networkAddress));
        rows.emplace_back(fmt::format("  {:<9} {}", "Gateway", gateway));
    }
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
    rows.emplace_back("");
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
    rows.emplace_back(fmt::format("{:<14} {} with {}", "Headnode",
        osSummary(model.getHeadnode().getOS()),
        provisionerName(model.getProvisioner())));
    rows.emplace_back(
        fmt::format("{:<14} {}", "Nodes", nodeOperatingSystemSummary(model)));
    if (const auto warning = compatibilityWarning(model); warning.has_value()) {
        rows.emplace_back(fmt::format("{:<14} {}", "Warning", *warning));
    }
    rows.emplace_back(
        fmt::format("{:<14} {}", "ISO and OS", isoSummary(model)));
    rows.emplace_back(
        fmt::format("{:<14} {}", "Repositories", repositorySummary(model)));
    rows.emplace_back(
        fmt::format("{:<14} {}", "Queue system", queueSummary(model)));

    appendNetworkDetails(rows, model);
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
