/*
 * Copyright 2022 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <algorithm>
#include <opencattus/functions.h>
#include <opencattus/presenter/PresenterRepository.h>
#include <ranges>

namespace {

using RepositorySelection
    = opencattus::services::repos::RepoManager::RepositorySelection;

auto selectableRepositoryIds() -> const std::vector<std::string>&
{
    static const auto ids = std::vector<std::string> {
        "cuda",
        "beegfs",
        "elrepo",
        "grafana",
        "influxdata",
        "oneAPI",
        "nvhpc",
        "rpmfusion",
        "zfs",
        "zabbix",
    };
    return ids;
}

auto selectableRepositoriesFrom(
    const std::vector<RepositorySelection>& allSelections)
    -> std::vector<RepositorySelection>
{
    std::vector<RepositorySelection> selectable;
    selectable.reserve(selectableRepositoryIds().size());

    for (const auto& selectableRepositoryId : selectableRepositoryIds()) {
        if (const auto it = std::ranges::find_if(allSelections,
                [&selectableRepositoryId](const auto& selection) {
                    return selection.id == selectableRepositoryId;
                });
            it != allSelections.end()) {
            selectable.emplace_back(*it);
        }
    }

    return selectable;
}

}

namespace opencattus::presenter {

PresenterRepository::PresenterRepository(
    std::unique_ptr<Cluster>& model, std::unique_ptr<View>& view)
    : Presenter(model, view)
{
    using UISelectionAdapterTy
        = std::vector<std::tuple<std::string, std::string, bool>>;
    const auto ofedVersion = m_model->getOFED().has_value()
        ? m_model->getOFED()->getVersion()
        : std::string("latest");
    const auto optionalRepos = selectableRepositoriesFrom(
        services::repos::RepoManager::defaultRepositoriesFor(
            m_model->getComputeNodeOS(), ofedVersion,
            m_model->getEnabledRepositories()));
    if (optionalRepos.empty()) {
        m_model->clearEnabledRepositories();
        return;
    }

    auto allReposUIAdapter = optionalRepos
        | std::views::transform([](const auto& entry) {
              return std::make_tuple(entry.id, entry.name, entry.enabled);
          })
        | std::ranges::to<UISelectionAdapterTy>();

    const auto& [ret, toEnable] = m_view->checkboxSelectionMenu(Messages::title,
        Messages::General::question, Messages::General::help,
        allReposUIAdapter);

    LOG_DEBUG("{} repos selected", toEnable.size());

    if (ret != 1) {
        m_view->abort();
    }

    const auto expandedSelection
        = services::repos::expandSelectedRepositoryIds(toEnable);
    if (expandedSelection.empty()) {
        m_model->clearEnabledRepositories();
    } else {
        m_model->setEnabledRepositories(expandedSelection);
    }
}

}
