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
#include <opencattus/presenter/PresenterProvisioner.h>
#include <opencattus/presenter/PresenterQueueSystem.h>
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
PresenterInstall::PresenterInstall(
    std::unique_ptr<Cluster>& model, std::unique_ptr<Newt>& view)
    : Presenter(model, view)
{

#if 1 // Welcome messages
    Call<PresenterWelcome>();
    Call<PresenterInstructions>();
#endif

#if 1 // Set general settings
    Call<PresenterGeneralSettings>();
#endif

#if 1 // Timezone and locale support
    Call<PresenterTime>();
    Call<PresenterLocale>();
#endif

#if 1 // Hostname and domain
    Call<PresenterHostId>();
#endif

    // Repository configuration still happens during execution. The previous
    // TUI repository screen depended on post-init singletons and did not
    // persist anything into the model.

    NetworkCreator nc;
#if 1 // Networking
    // TODO: Under development
    //  * Add it to a loop where it asks to the user which kind of network we
    //  should add, while the operator says it's done adding networks. We remove
    //  the lazy network{1,2} after that.

    try {
        Call<PresenterNetwork>(nc, Network::Profile::External);
    } catch (const std::exception& ex) {
        LOG_ERROR("Failed to add {} network: {}",
            opencattus::utils::enums::toString(Network::Profile::External),
            ex.what());
    }

    try {
        Call<PresenterNetwork>(nc, Network::Profile::Management);
    } catch (const std::exception& ex) {
        LOG_ERROR("Failed to add {} network: {}",
            opencattus::utils::enums::toString(Network::Profile::Management),
            ex.what());
    }

    if (m_view->yesNoQuestion(NetworkMessages::title,
            NetworkMessages::serviceQuestion, NetworkMessages::serviceHelp)) {
        try {
            Call<PresenterNetwork>(nc, Network::Profile::Service);
        } catch (const std::exception& ex) {
            LOG_ERROR("Failed to add {} network: {}",
                opencattus::utils::enums::toString(Network::Profile::Service),
                ex.what());
        }
    }

#endif

#if 1 // Infiniband support
    Call<PresenterInfiniband>(nc);
#endif
    nc.saveNetworksToModel(*m_model);

#if 1 // Compute nodes formation details
    Call<PresenterNodesOperationalSystem>();
    Call<PresenterProvisioner>();
    Call<PresenterNodes>();
#endif

#if 1 // Queue System
    Call<PresenterQueueSystem>();
#endif

#if 1 // Mail system
    Call<PresenterMailSystem>();
#endif

    // Destroy the view since we don't need it anymore
    m_view.reset();
}

}
