/*
 * Copyright 2022 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <opencattus/presenter/PresenterGeneralSettings.h>

namespace opencattus::presenter {

using opencattus::models::Headnode;

PresenterGeneralSettings::PresenterGeneralSettings(
    std::unique_ptr<Cluster>& model, std::unique_ptr<View>& view)
    : Presenter(model, view)
{

    // Generic settings
    auto generalSettings = std::to_array<std::pair<std::string, std::string>>(
        { { Messages::General::clusterName, "" },
            { Messages::General::companyName, "" },
            { Messages::General::adminEmail, "" } });

    generalSettings = m_view->fieldMenu(Messages::title,
        Messages::General::question, generalSettings, Messages::General::help);

    std::size_t i { 0 };
    m_model->setName(generalSettings[i++].second);
    LOG_DEBUG("Set cluster name: {}", m_model->getName())

    m_model->setCompanyName(generalSettings[i++].second);
    LOG_DEBUG("Set cluster company name: {}", m_model->getCompanyName())

    m_model->setAdminMail(generalSettings[i++].second);
    LOG_DEBUG("Set cluster admin e-email: {}", m_model->getAdminMail())

    // Boot target
    m_model->getHeadnode().setBootTarget(
        opencattus::utils::enums::ofStringOpt<Headnode::BootTarget>(
            m_view->listMenu(Messages::title, Messages::BootTarget::question,
                opencattus::utils::enums::toStrings<Headnode::BootTarget>(),
                Messages::BootTarget::help))
            .value());
    LOG_DEBUG("{} boot target set on headnode",
        opencattus::utils::enums::toString<Headnode::BootTarget>(
            m_model->getHeadnode().getBootTarget()));
}

};
