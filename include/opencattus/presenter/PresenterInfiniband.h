/*
 * Copyright 2022 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OPENCATTUS_PRESENTERINFINIBAND_H_
#define OPENCATTUS_PRESENTERINFINIBAND_H_

#include <opencattus/presenter/Presenter.h>
#include <opencattus/presenter/PresenterNetwork.h>

namespace opencattus::presenter {

class PresenterInfiniband : public Presenter {
private:
    struct Messages {
        static constexpr const auto title = "Infiniband settings";
        static constexpr const auto question
            = "Do you have an Infiniband Fabric available?";
        static constexpr const auto help
            = Presenter::Messages::Placeholder::help;

        struct OFED {
            static constexpr const auto question
                = "Choose the desired Infiniband stack";
            static constexpr const auto help
                = Presenter::Messages::Placeholder::help;

            struct Version {
                static constexpr const auto question
                    = "Enter the OFED/DOCA version to use";
                static constexpr const auto help
                    = Presenter::Messages::Placeholder::help;
                static constexpr const auto field = "Version";
            };
        };
    };

public:
    PresenterInfiniband(std::unique_ptr<Cluster>& model,
        std::unique_ptr<View>& view, opencattus::presenter::NetworkCreator& nc);
};

};

#endif // OPENCATTUS_PRESENTERINFINIBAND_H_
