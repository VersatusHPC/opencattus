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
            = "The provisioner imports node images, tracks compute nodes, and "
              "drives network boot. xCAT and Confluent are available on "
              "supported Enterprise Linux releases and Ubuntu 24.04.";

        static constexpr const auto singleProvisioner
            = "Only one provisioner supports the selected operating systems; "
              "it was selected automatically";
    };

public:
    PresenterProvisioner(
        std::unique_ptr<Cluster>& model, std::unique_ptr<View>& view);
};

};

#endif // OPENCATTUS_PRESENTERPROVISIONER_H_
