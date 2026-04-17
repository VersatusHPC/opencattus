/*
 * Copyright 2024 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <opencattus/presenter/Presenter.h>

namespace opencattus::presenter {
class PresenterRepository : public Presenter {
private:
    struct Messages {
        static constexpr const auto title = "Repositories";

        struct General {
            static constexpr const auto question
                = "Select the repositories to enable. SPACE toggles an entry.";
            static constexpr const auto help
                = "Only optional repositories are listed here. Distribution, "
                  "EPEL, OpenHPC, and provisioner repositories are enabled by "
                  "the installer when required.";
        };
    };

public:
    PresenterRepository(
        std::unique_ptr<Cluster>& model, std::unique_ptr<View>& view);
};

};
