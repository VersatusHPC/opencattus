/*
 * Copyright 2022 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <opencattus/presenter/PresenterQueueSystem.h>

namespace opencattus::presenter {

using opencattus::models::SLURM;

PresenterQueueSystem::PresenterQueueSystem(
    std::unique_ptr<Cluster>& model, std::unique_ptr<Newt>& view)
    : Presenter(model, view)
{

    m_model->setQueueSystem(
        opencattus::utils::enums::ofStringOpt<QueueSystem::Kind>(
            m_view->listMenu(Messages::title, Messages::question,
                opencattus::utils::enums::toStrings<QueueSystem::Kind>(),
                Messages::help))
            .value());

    // TODO: Placeholder data
    auto fieldsSLURM = std::to_array<std::pair<std::string, std::string>>(
        { { Messages::SLURM::partition, "execution" },
            { Messages::SLURM::mariadbRootPassword, "mariadb-root" },
            { Messages::SLURM::slurmDBPassword, "slurmdbd" },
            { Messages::SLURM::storagePassword, "slurm-storage" } });

    if (auto& queue = m_model->getQueueSystem()) {
        switch (queue.value()->getKind()) {
            case QueueSystem::Kind::None: {
                __builtin_unreachable();
            }

            case QueueSystem::Kind::SLURM: {
                fieldsSLURM = m_view->fieldMenu(Messages::SLURM::title,
                    Messages::SLURM::question, fieldsSLURM,
                    Messages::SLURM::help);

                const auto& slurm = dynamic_cast<SLURM*>(queue.value().get());
                slurm->setDefaultQueue(fieldsSLURM[0].second);
                m_model->slurmMariaDBRootPassword = fieldsSLURM[1].second;
                m_model->slurmDBPassword = fieldsSLURM[2].second;
                m_model->slurmStoragePassword = fieldsSLURM[3].second;
                LOG_DEBUG(
                    "Set SLURM default queue: {}", slurm->getDefaultQueue());

                break;
            }

            case QueueSystem::Kind::PBS: {
                const auto& execution = m_view->listMenu(Messages::PBS::title,
                    Messages::PBS::question,
                    opencattus::utils::enums::toStrings<PBS::ExecutionPlace>(),
                    Messages::PBS::help);

                const auto& pbs = dynamic_cast<PBS*>(queue.value().get());
                pbs->setExecutionPlace(
                    opencattus::utils::enums::ofStringOpt<PBS::ExecutionPlace>(
                        execution)
                        .value());
                LOG_DEBUG("Set PBS Execution Place: {}",
                    opencattus::utils::enums::toString<PBS::ExecutionPlace>(
                        pbs->getExecutionPlace()));
                queue.value()->setDefaultQueue("execution");
                m_model->slurmMariaDBRootPassword = "unused";
                m_model->slurmDBPassword = "unused";
                m_model->slurmStoragePassword = "unused";

                break;
            }
        }
    }
}
}
