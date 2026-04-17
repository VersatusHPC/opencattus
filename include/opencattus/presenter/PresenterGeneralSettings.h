/*
 * Copyright 2022 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OPENCATTUS_PRESENTERGENERALSETTINGS_H_
#define OPENCATTUS_PRESENTERGENERALSETTINGS_H_

#include <opencattus/presenter/Presenter.h>

namespace opencattus::presenter {

class PresenterGeneralSettings : public Presenter {
private:
    struct Messages {
        static constexpr const auto title = "General cluster settings";

        struct General {
            static constexpr const auto question
                = "Fill the required data about your new cluster";
            static constexpr const auto help
                = "Use a short cluster name without spaces. The administrator "
                  "e-mail is used for generated service configuration and "
                  "cluster notifications.";

            static constexpr const auto clusterName = "Cluster Name";
            static constexpr const auto companyName = "Company Name";
            static constexpr const auto adminEmail = "Administrator e-mail";
        };

        struct BootTarget {
            static constexpr const auto question
                = "Select the boot target for the headnode";
            static constexpr const auto help
                = "Choose Text for a server-style boot target. Graphical is "
                  "only useful when this head node should boot into a desktop.";
        };
    };

public:
    PresenterGeneralSettings(
        std::unique_ptr<Cluster>& model, std::unique_ptr<View>& view);
};

};

#endif // OPENCATTUS_PRESENTERGENERALSETTINGS_H_
