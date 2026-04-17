/*
 * Copyright 2022 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OPENCATTUS_PRESENTERTIMEZONE_H_
#define OPENCATTUS_PRESENTERTIMEZONE_H_

#include <opencattus/presenter/Presenter.h>

namespace opencattus::presenter {

class PresenterTime : public Presenter {
private:
    struct Messages {
        static constexpr const auto title = "Time and clock settings";

        struct Timezone {
            static constexpr const auto question = "Choose the local timezone";
            static constexpr const auto help
                = "Choose the timezone used by the head node and generated "
                  "cluster configuration. Prefer the canonical Area/Location "
                  "entry when aliases are available.";
        };

        struct Timeservers {
            static constexpr const auto question
                = "Add or change the list of available time servers";
            static constexpr const auto help
                = "Use NTP servers reachable from the head node. Public pool "
                  "servers are fine for internet-connected clusters; isolated "
                  "clusters should use local time sources.";
        };

        struct AddTimeserver {
            static constexpr const auto question = "Add time server";
            static constexpr const auto field = "Address:";
            static constexpr const auto help
                = "Enter one hostname or IP address for an NTP server.";
        };
    };

public:
    PresenterTime(std::unique_ptr<Cluster>& model, std::unique_ptr<View>& view);
};

};

#endif // OPENCATTUS_PRESENTERTIMEZONE_H_
