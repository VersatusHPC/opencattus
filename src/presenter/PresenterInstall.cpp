/*
 * Copyright 2022 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <opencattus/presenter/PresenterGeneralSettings.h>
#include <opencattus/presenter/PresenterHostId.h>
#include <opencattus/presenter/PresenterInfiniband.h>
#include <opencattus/presenter/PresenterInstall.h>
#include <opencattus/presenter/PresenterInstructions.h>
#include <opencattus/presenter/PresenterLocale.h>
#include <opencattus/presenter/PresenterMailSystem.h>
#include <opencattus/presenter/PresenterNetwork.h>
#include <opencattus/presenter/PresenterNodes.h>
#include <opencattus/presenter/PresenterNodesOperationalSystem.h>
#include <opencattus/presenter/PresenterPreflight.h>
#include <opencattus/presenter/PresenterProvisioner.h>
#include <opencattus/presenter/PresenterQueueSystem.h>
#include <opencattus/presenter/PresenterRepository.h>
#include <opencattus/presenter/PresenterTime.h>
#include <opencattus/presenter/PresenterWelcome.h>
#include <opencattus/services/runner.h>
#include <opencattus/utils/singleton.h>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/lexical_cast.hpp>
#include <filesystem>
#include <fmt/core.h>
#include <optional>
#include <string>
#include <vector>

namespace {

struct NetworkMessages {
    static constexpr const auto title = "Network settings";
    static constexpr const auto serviceQuestion
        = "Do you want to configure a service network?";
    static constexpr const auto serviceHelp
        = "Enable this when the cluster uses a dedicated service or BMC "
          "network alongside the management network.";
};

struct DiskImageDownloadMessages {
    static constexpr const auto title = "Compute node OS settings";
    static constexpr const auto download
        = "Downloading ISO from {0}\nSource: {1}";
};

auto wgetProgressPercent(opencattus::services::CommandProxy& cmd,
    const std::string& downloadURL) -> std::optional<double>
{
    auto out = cmd.getline();
    if (!out) {
        return std::nullopt;
    }
    std::string line = *out;

    // If we have a line like ERROR 404: Not Found this means the URL was not
    // found. wget reports this on stderr, which is the stream we monitor.
    if (line.contains("ERROR 404: Not Found")) {
        LOG_ERROR("URL {} not found", downloadURL);
        return std::nullopt;
    }

    // Line example:
    // 338950K .......... .......... ..........  3% 31.8M 10m40s
    std::vector<std::string> slots;
    boost::split(
        slots, line, boost::is_any_of("\t\r "), boost::token_compress_on);

    if (slots.size() <= 6) {
        return std::make_optional(0.0);
    }

    auto num = slots[6].substr(0, slots[6].find_first_of('%'));

    try {
        return std::make_optional(boost::lexical_cast<double>(num));
    } catch (boost::bad_lexical_cast&) {
        return std::make_optional(0.0);
    }
}

void downloadPendingDiskImage(opencattus::models::Cluster& model, View& view)
{
    const auto pendingURL = model.getPendingDiskImageDownloadURL();
    if (!pendingURL.has_value()) {
        return;
    }

    const auto diskImagePath = model.getDiskImage().getPath();
    const auto opts = opencattus::utils::singleton::options();

    if (opts->dryRun) {
        LOG_INFO("Dry Run: Would download {} from {}", diskImagePath.string(),
            pendingURL.value());
        model.clearPendingDiskImageDownload();
        return;
    }

    const auto downloadDirectory = diskImagePath.parent_path();
    auto command = opencattus::utils::singleton::runner()->executeCommandIter(
        fmt::format(
            "wget -c -P {} {}", downloadDirectory.string(), pendingURL.value()),
        opencattus::services::Stream::Stderr);

    const auto description = fmt::format(DiskImageDownloadMessages::download,
        diskImagePath.filename().string(), pendingURL.value());
    const auto shouldContinue = view.progressMenu(
        DiskImageDownloadMessages::title, description.c_str(),
        std::move(command),
        [&](opencattus::services::CommandProxy& cmd) -> std::optional<double> {
            return wgetProgressPercent(cmd, pendingURL.value());
        });

    if (!shouldContinue) {
        if (command.hasProcess) {
            command.child.terminate();
            command.child.wait();
        }
        throw ViewAbortRequested("ISO download stopped");
    }

    if (command.hasProcess) {
        command.child.wait();
        const auto exitCode = command.child.exit_code();
        if (exitCode != 0) {
            const auto message = fmt::format(
                "ISO download failed with exit code {}. A draft will be saved "
                "so you can retry.",
                exitCode);
            view.message(DiskImageDownloadMessages::title, message.c_str());
            throw ViewAbortRequested(message);
        }

        std::error_code existsError;
        if (!std::filesystem::exists(diskImagePath, existsError)) {
            const auto detail = existsError
                ? fmt::format("Unable to inspect downloaded ISO {}: {}",
                      diskImagePath.string(), existsError.message())
                : fmt::format("Downloaded ISO {} was not found",
                      diskImagePath.string());
            view.message(DiskImageDownloadMessages::title, detail.c_str());
            throw ViewAbortRequested(detail);
        }
    }

    model.clearPendingDiskImageDownload();
}

} // namespace

namespace opencattus::presenter {
PresenterInstall::PresenterInstall(std::unique_ptr<Cluster>& model,
    std::unique_ptr<View>& view, StepCompletionCallback onStepComplete,
    std::set<std::string> completedSteps)
    : Presenter(model, view)
{
    const auto runStep = [&](std::string_view step, auto&& body) {
        const auto stepName = std::string(step);
        if (completedSteps.contains(stepName)) {
            LOG_INFO("Skipping completed TUI step {}", stepName);
            return;
        }

        body();

        if (onStepComplete) {
            onStepComplete(step);
        }
    };

    runStep("welcome", [&]() { Call<PresenterWelcome>(); });
    runStep("instructions", [&]() { Call<PresenterInstructions>(); });

    runStep("general", [&]() { Call<PresenterGeneralSettings>(); });

    runStep("time", [&]() { Call<PresenterTime>(); });
    runStep("locale", [&]() { Call<PresenterLocale>(); });

    runStep("hostname", [&]() { Call<PresenterHostId>(); });

    // TODO: Under development
    //  * Add it to a loop where it asks to the user which kind of network we
    //  should add, while the operator says it's done adding networks. We remove
    //  the lazy network{1,2} after that.
    runStep("networking", [&]() {
        NetworkCreator nc;

        try {
            Call<PresenterNetwork>(nc, Network::Profile::External);
        } catch (const ViewAbortRequested&) {
            throw;
        } catch (const std::exception& ex) {
            LOG_ERROR("Failed to add {} network: {}",
                opencattus::utils::enums::toString(Network::Profile::External),
                ex.what());
        }

        try {
            Call<PresenterNetwork>(nc, Network::Profile::Management);
        } catch (const ViewAbortRequested&) {
            throw;
        } catch (const std::exception& ex) {
            LOG_ERROR("Failed to add {} network: {}",
                opencattus::utils::enums::toString(
                    Network::Profile::Management),
                ex.what());
        }

        if (m_view->yesNoQuestion(NetworkMessages::title,
                NetworkMessages::serviceQuestion,
                NetworkMessages::serviceHelp)) {
            try {
                Call<PresenterNetwork>(nc, Network::Profile::Service);
            } catch (const ViewAbortRequested&) {
                throw;
            } catch (const std::exception& ex) {
                LOG_ERROR("Failed to add {} network: {}",
                    opencattus::utils::enums::toString(
                        Network::Profile::Service),
                    ex.what());
            }
        }

        Call<PresenterInfiniband>(nc);
        nc.saveNetworksToModel(*m_model);
    });

    runStep("os", [&]() { Call<PresenterNodesOperationalSystem>(); });
    runStep("provisioner", [&]() { Call<PresenterProvisioner>(); });
    runStep("repositories", [&]() { Call<PresenterRepository>(); });
    runStep("nodes", [&]() { Call<PresenterNodes>(); });

    runStep("queue", [&]() { Call<PresenterQueueSystem>(); });

    runStep("mail", [&]() { Call<PresenterMailSystem>(); });
    runStep("preflight", [&]() { Call<PresenterPreflight>(); });

    downloadPendingDiskImage(*m_model, *m_view);

    // Destroy the view since we don't need it anymore
    m_view.reset();
}

}
