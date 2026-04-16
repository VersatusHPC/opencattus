/*
 * Copyright 2022 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <opencattus/patterns/singleton.h>
#include <opencattus/presenter/PresenterLocale.h>

namespace opencattus::presenter {

PresenterLocale::PresenterLocale(
    std::unique_ptr<Cluster>& model, std::unique_ptr<View>& view)
    : Presenter(model, view)
{
    auto availableLocales
        = opencattus::Singleton<IRunner>::get()->checkOutput("locale -a");
    if (availableLocales.empty()) {
        m_view->fatalMessage(Messages::title,
            "No locales were discovered on this system. Verify that "
            "'locale -a' works and try again.");
    }

    const auto& selectedLocale = m_view->listMenu(
        Messages::title, Messages::question, availableLocales, Messages::help);

    m_model->setLocale(selectedLocale);
    LOG_DEBUG("Locale set to: {}", selectedLocale)
}

};
