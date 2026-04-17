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
        static constexpr const auto title = "InfiniBand settings";
        static constexpr const auto question
            = "Do you have an InfiniBand fabric available?";
        static constexpr const auto help
            = "Enable this when compute nodes use an InfiniBand application "
              "network. The installer will ask for the stack and network "
              "settings only when usable InfiniBand interfaces are detected.";

        struct OFED {
            static constexpr const auto question
                = "Choose the desired InfiniBand stack";
            static constexpr const auto help
                = "Inbox uses the distribution drivers. DOCA installs NVIDIA "
                  "DOCA/OFED packages for clusters that need the vendor stack.";

            struct Version {
                static constexpr const auto question
                    = "Enter the OFED/DOCA version to use";
                static constexpr const auto help
                    = "Use latest for the repository default, or enter a "
                      "specific version when the cluster must stay pinned to a "
                      "validated driver stack.";
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
