/*
 * Copyright 2022 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OPENCATTUS_PRESENTERWELCOME_H_
#define OPENCATTUS_PRESENTERWELCOME_H_

#include <opencattus/const.h>
#include <opencattus/presenter/Presenter.h>

namespace opencattus::presenter {
class PresenterWelcome : public Presenter {
private:
    struct Messages {
        struct Welcome {
            static constexpr const auto message
                = "Welcome to the " PRODUCT_NAME " guided installer.\n\n"
                  "This questionnaire collects the settings needed to build a "
                  "basic HPC cluster.\n\nFor source code, feature requests, "
                  "and bug reports, visit " PRODUCT_URL;
        };
    };

public:
    PresenterWelcome(
        std::unique_ptr<Cluster>& model, std::unique_ptr<View>& view);
};
};

#endif // OPENCATTUS_PRESENTERWELCOME_H_
