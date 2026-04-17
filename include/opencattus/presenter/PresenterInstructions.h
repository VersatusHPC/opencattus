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
                = "The installer will collect the settings needed for this HPC "
                  "cluster.\n\nYou can stop the questionnaire at any time. "
                  "Progress is saved to a draft answerfile in the current "
                  "directory and can be resumed later.\n";
        };
    };

public:
    PresenterInstructions(
        std::unique_ptr<Cluster>& model, std::unique_ptr<View>& view);
};

};

#endif // OPENCATTUS_PRESENTERINSTRUCTIONS_H_
