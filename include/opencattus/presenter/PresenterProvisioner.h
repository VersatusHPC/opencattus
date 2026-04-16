/*
 * Copyright 2026 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OPENCATTUS_PRESENTERPROVISIONER_H_
#define OPENCATTUS_PRESENTERPROVISIONER_H_

#include <opencattus/presenter/Presenter.h>

namespace opencattus::presenter {

class PresenterProvisioner : public Presenter {
private:
    struct Messages {
        static constexpr const auto title = "Provisioner settings";
        static constexpr const auto question
            = "Choose how the cluster nodes should be provisioned";
        static constexpr const auto help
            = Presenter::Messages::Placeholder::help;

        static constexpr const auto confluentOnly
            = "Confluent is currently required for EL10 installs";
    };

public:
    PresenterProvisioner(
        std::unique_ptr<Cluster>& model, std::unique_ptr<Newt>& view);
};

};

#endif // OPENCATTUS_PRESENTERPROVISIONER_H_
