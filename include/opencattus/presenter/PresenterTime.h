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
            static constexpr const auto question = "Choose your local timezone";
            static constexpr const auto help
                = Presenter::Messages::Placeholder::help;
        };

        struct Timeservers {
            static constexpr const auto question
                = "Add or change the list of available time servers";
            static constexpr const auto help
                = Presenter::Messages::Placeholder::help;
        };

        struct AddTimeserver {
            static constexpr const auto question = "Add time server";
            static constexpr const auto field = "Address:";
            static constexpr const auto help
                = Presenter::Messages::Placeholder::help;
        };
    };

public:
    PresenterTime(std::unique_ptr<Cluster>& model, std::unique_ptr<View>& view);
};

};

#endif // OPENCATTUS_PRESENTERTIMEZONE_H_
