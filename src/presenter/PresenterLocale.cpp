/*
 * Copyright 2022 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <opencattus/presenter/PresenterLocale.h>
#include <opencattus/patterns/singleton.h>

namespace opencattus::presenter {

PresenterLocale::PresenterLocale(
    std::unique_ptr<Cluster>& model, std::unique_ptr<Newt>& view)
    : Presenter(model, view)
{
    auto availableLocales = opencattus::Singleton<IRunner>::get()->checkOutput(
        "locale -a");

    const auto& selectedLocale = m_view->listMenu(
        Messages::title, Messages::question, availableLocales, Messages::help);

    m_model->setLocale(selectedLocale);
    LOG_DEBUG("Locale set to: {}", selectedLocale)
}

};
