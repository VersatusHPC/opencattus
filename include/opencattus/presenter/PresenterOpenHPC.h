/*
 * Copyright 2026 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <opencattus/presenter/Presenter.h>

namespace opencattus::presenter {
class PresenterOpenHPC : public Presenter {
private:
    struct Messages {
        static constexpr const auto title = "OpenHPC bundles";

        struct General {
            static constexpr const auto question
                = "Select the OpenHPC software bundles to install. Base GNU "
                  "compilers and MPI stacks are always installed.";
            static constexpr const auto help
                = "Serial and parallel scientific library bundles are enabled "
                  "by default. Selecting Intel oneAPI also installs Intel MPI "
                  "plus the Intel scientific library bundles, and enables the "
                  "Intel repository when the installer configures software "
                  "sources.";
        };
    };

public:
    PresenterOpenHPC(
        std::unique_ptr<Cluster>& model, std::unique_ptr<View>& view);
};

};
