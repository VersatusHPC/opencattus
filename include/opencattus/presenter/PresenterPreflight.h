/*
 * Copyright 2026 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OPENCATTUS_PRESENTERPREFLIGHT_H_
#define OPENCATTUS_PRESENTERPREFLIGHT_H_

#include <opencattus/presenter/Presenter.h>

namespace opencattus::presenter {

class PresenterPreflight : public Presenter {
private:
    struct Messages {
        static constexpr const auto title = "Preflight validation";
        static constexpr const auto help
            = "This final check summarizes the selected operating system, "
              "provisioner, networks, BMC coverage, repositories, and queue "
              "system before installation starts. Choose Stop to save the "
              "current draft and leave the installer.";
    };

public:
    PresenterPreflight(
        std::unique_ptr<Cluster>& model, std::unique_ptr<View>& view);
};

};

#endif // OPENCATTUS_PRESENTERPREFLIGHT_H_
