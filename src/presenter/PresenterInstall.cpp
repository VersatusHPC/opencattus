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

namespace {

struct NetworkMessages {
    static constexpr const auto title = "Network settings";
    static constexpr const auto serviceQuestion
        = "Do you want to configure a service network?";
    static constexpr const auto serviceHelp
        = "Enable this when the cluster uses a dedicated service or BMC "
          "network alongside the management network.";
};

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

    // Destroy the view since we don't need it anymore
    m_view.reset();
}

}
