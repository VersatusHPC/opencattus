/*
 * Copyright 2022 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OPENCATTUS_PRESENTERLOCALE_H_
#define OPENCATTUS_PRESENTERLOCALE_H_

#include <opencattus/presenter/Presenter.h>

namespace opencattus::presenter {

class PresenterLocale : public Presenter {
private:
    struct Messages {
        static constexpr const auto title = "Locale settings";
        static constexpr const auto question
            = "Pick the default locale language";
        static constexpr const auto regionQuestion
            = "Pick the regional UTF-8 locale";
        static constexpr const auto legacyQuestion
            = "Pick an advanced or legacy locale";
        static constexpr const auto help
            = "Choose the default locale for command output, sorting, date "
              "formats, and other regional behavior. UTF-8 locales are "
              "preferred for new systems.";
    };

public:
    PresenterLocale(
        std::unique_ptr<Cluster>& model, std::unique_ptr<View>& view);
};

};

#endif // OPENCATTUS_PRESENTERLOCALE_H_
