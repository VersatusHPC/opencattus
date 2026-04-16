/*
 * Copyright 2022 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <opencattus/presenter/PresenterInfiniband.h>

#include <algorithm>

namespace {

auto hasInfinibandInterface(const std::vector<std::string>& interfaces) -> bool
{
    return std::ranges::any_of(interfaces,
        [](const auto& interface) { return interface.starts_with("ib"); });
}

} // namespace

namespace opencattus::presenter {

PresenterInfiniband::PresenterInfiniband(std::unique_ptr<Cluster>& model,
    std::unique_ptr<View>& view, NetworkCreator& nc)
    : Presenter(model, view)
{

    auto interfaces = Connection::fetchInterfaces();
    if (!hasInfinibandInterface(interfaces)) {
        LOG_WARN("No Infiniband interfaces found.")
        return;
    }

    // TODO: Infiniband class? Detect if IB is available (fetch ib0)
    if (m_view->yesNoQuestion(
            Messages::title, Messages::question, Messages::help)) {

        const auto selectedKind
            = opencattus::utils::enums::ofStringOpt<OFED::Kind>(
                m_view->listMenu(Messages::title, Messages::OFED::question,
                    opencattus::utils::enums::toStrings<OFED::Kind>(),
                    Messages::OFED::help))
                  .value();

        std::string selectedVersion = "latest";
        if (selectedKind != OFED::Kind::Inbox) {
            auto version = std::to_array<std::pair<std::string, std::string>>(
                { { Messages::OFED::Version::field, selectedVersion } });
            version = m_view->fieldMenu(Messages::title,
                Messages::OFED::Version::question, version,
                Messages::OFED::Version::help);
            selectedVersion = version[0].second;
        }

        m_model->setOFED(selectedKind, selectedVersion);
        LOG_DEBUG("Set OFED stack as: {}",
            opencattus::utils::enums::toString<OFED::Kind>(
                m_model->getOFED()->getKind()));

        try {
            Call<PresenterNetwork>(
                nc, Network::Profile::Application, Network::Type::Infiniband);
        } catch (const std::exception& ex) {
            LOG_ERROR("Failed to add {} network: {}",
                opencattus::utils::enums::toString(
                    Network::Profile::Application),
                ex.what());
        }
    }
}

}
