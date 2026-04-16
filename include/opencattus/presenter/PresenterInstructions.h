/*
 * Copyright 2022 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OPENCATTUS_PRESENTERINSTRUCTIONS_H_
#define OPENCATTUS_PRESENTERINSTRUCTIONS_H_

#include <opencattus/presenter/Presenter.h>

namespace opencattus::presenter {

class PresenterInstructions : public Presenter {
private:
    struct Messages {
        struct Instructions {
            static constexpr const auto message
                = "We will start the installation phase now.\n\nYou will be "
                  "guided through "
                  "a series of questions about your HPC cluster so we can "
                  "configure "
                  "everything accordingly to your needs. You're free to cancel "
                  "the process "
                  "at any given time.\n";
        };
    };

public:
    PresenterInstructions(
        std::unique_ptr<Cluster>& model, std::unique_ptr<View>& view);
};

};

#endif // OPENCATTUS_PRESENTERINSTRUCTIONS_H_
