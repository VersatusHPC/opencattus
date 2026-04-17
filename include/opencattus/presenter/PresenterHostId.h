/*
 * Copyright 2022 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OPENCATTUS_PRESENTERHOSTID_H_
#define OPENCATTUS_PRESENTERHOSTID_H_

#include <opencattus/presenter/Presenter.h>

namespace opencattus::presenter {

class PresenterHostId : public Presenter {
private:
    struct Messages {
        static constexpr const auto title = "Hostname settings";
        static constexpr const auto question
            = "Enter the desired hostname and domain name for this machine";
        static constexpr const auto help
            = "Set the short hostname for the head node and the DNS domain "
              "used by the cluster. The domain is reused as a default in "
              "network and mail settings.";

        static constexpr const auto hostname = "Hostname";
        static constexpr const auto domainName = "Domain name";
    };

public:
    PresenterHostId(
        std::unique_ptr<Cluster>& model, std::unique_ptr<View>& view);
};

};

#endif // OPENCATTUS_PRESENTERHOSTID_H_
