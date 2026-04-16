/*
 * Copyright 2022 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OPENCATTUS_PRESENTERQUEUESYSTEM_H_
#define OPENCATTUS_PRESENTERQUEUESYSTEM_H_

#include <opencattus/presenter/Presenter.h>

namespace opencattus::presenter {
class PresenterQueueSystem : public Presenter {
private:
    struct Messages {
        static constexpr const auto title = "Queue System settings";
        static constexpr const auto question
            = "Pick a queue system to run you compute jobs";
        static constexpr const auto help
            = Presenter::Messages::Placeholder::help;

        struct SLURM {
            static constexpr const auto title = "SLURM settings";
            static constexpr const auto question
                = "Enter the SLURM controller settings. The accounting "
                  "storage password will reuse the slurmdbd password.";
            static constexpr const auto help
                = Presenter::Messages::Placeholder::help;

            static constexpr const auto partition = "Partition name";
            static constexpr const auto mariadbRootPassword
                = "MariaDB root password";
            static constexpr const auto slurmDBPassword = "slurmdbd password";
        };

        struct PBS {
            static constexpr const auto title = "PBS Professional settings";
            static constexpr const auto question
                = "Select the default execution place for PBS Professional "
                  "jobs";
            static constexpr const auto help
                = Presenter::Messages::Placeholder::help;
        };
    };

public:
    PresenterQueueSystem(
        std::unique_ptr<Cluster>& model, std::unique_ptr<View>& view);
};

};

#endif // OPENCATTUS_PRESENTERQUEUESYSTEM_H_
