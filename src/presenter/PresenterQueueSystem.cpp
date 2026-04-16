/*
 * Copyright 2022 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <opencattus/presenter/PresenterQueueSystem.h>

#include <random>
#include <string_view>

namespace {

auto randomPassword(std::size_t length) -> std::string
{
    static constexpr std::string_view alphabet = "abcdefghijkmnopqrstuvwxyz"
                                                 "ABCDEFGHJKLMNPQRSTUVWXYZ"
                                                 "23456789";

    std::random_device seed;
    std::mt19937 generator(seed());
    std::uniform_int_distribution<std::size_t> pick(0, alphabet.size() - 1);

    std::string password;
    password.reserve(length);
    for (std::size_t i = 0; i < length; ++i) {
        password.push_back(alphabet[pick(generator)]);
    }

    return password;
}

} // namespace

namespace opencattus::presenter {

using opencattus::models::SLURM;

PresenterQueueSystem::PresenterQueueSystem(
    std::unique_ptr<Cluster>& model, std::unique_ptr<View>& view)
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
            { Messages::SLURM::mariadbRootPassword, randomPassword(16) },
            { Messages::SLURM::slurmDBPassword, randomPassword(16) } });

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
                m_model->slurmStoragePassword = fieldsSLURM[2].second;
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

                break;
            }
        }
    }
}
}
