/*
 * Copyright 2022 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <map>
#include <opencattus/presenter/PresenterTime.h>
#include <set>
#include <string>
#include <vector>

namespace opencattus::presenter {

namespace {
    struct TimezoneNode {
        bool terminal = false;
        std::map<std::string, TimezoneNode> children;
    };

    auto splitTimezonePath(const std::string& path) -> std::vector<std::string>
    {
        std::vector<std::string> parts;
        std::size_t start = 0;

        while (start < path.size()) {
            const auto slash = path.find('/', start);
            parts.emplace_back(path.substr(start,
                slash == std::string::npos ? std::string::npos
                                           : slash - start));
            if (slash == std::string::npos) {
                break;
            }
            start = slash + 1;
        }

        return parts;
    }

    void insertTimezone(TimezoneNode& root, const std::string& path)
    {
        if (path.empty()) {
            root.terminal = true;
            return;
        }

        auto* current = &root;
        for (const auto& part : splitTimezonePath(path)) {
            current = &current->children[part];
        }
        current->terminal = true;
    }

    auto childNames(const TimezoneNode& node) -> std::vector<std::string>
    {
        std::vector<std::string> names;
        names.reserve(node.children.size());
        for (const auto& [name, child] : node.children) {
            static_cast<void>(child);
            names.push_back(name);
        }

        return names;
    }
}

PresenterTime::PresenterTime(
    std::unique_ptr<Cluster>& model, std::unique_ptr<View>& view)
    : Presenter(model, view)
{
    // Timezone area selection

    auto availableTimezones = m_model->getTimezone().getAvailableTimezones();
    if (availableTimezones.empty()) {
        m_view->fatalMessage(Messages::title,
            "No timezones were discovered on this system. Verify that "
            "tzdata's zone1970.tab or 'timedatectl list-timezones --no-pager' "
            "works and try again.");
    }

    std::set<std::string> timezoneAreas;
    for (const auto& tz : availableTimezones)
        timezoneAreas.insert(tz.first);

    auto selectedTimezoneLocationArea = m_view->listMenu(Messages::title,
        Messages::Timezone::question, timezoneAreas, Messages::Timezone::help);

    m_model->getTimezone().setTimezoneArea(selectedTimezoneLocationArea);

    std::string_view timezoneArea = m_model->getTimezone().getTimezoneArea();

    LOG_DEBUG("Timezone area set to: {}", timezoneArea)

    // Timezone location selection

    TimezoneNode timezoneTree;
    const auto& [begin, end]
        = availableTimezones.equal_range(timezoneArea.data());
    for (auto it = begin; it != end; ++it) {
        insertTimezone(timezoneTree, it->second);
    }
    if (!timezoneTree.terminal && timezoneTree.children.empty()) {
        m_view->fatalMessage(Messages::title,
            "No timezone locations were found for the selected area.");
    }

    auto selectedTimezone = std::string(timezoneArea);
    auto* current = &timezoneTree;
    while (!current->children.empty()) {
        const auto selectedTimezoneLocation
            = m_view->listMenu(Messages::title, Messages::Timezone::question,
                childNames(*current), Messages::Timezone::help);

        selectedTimezone
            = fmt::format("{}/{}", selectedTimezone, selectedTimezoneLocation);
        current = &current->children.at(selectedTimezoneLocation);
    }

    m_model->setTimezone(selectedTimezone);

    // FIXME: Horrible call; getTimezone() two times? Srsly?
    LOG_DEBUG("Timezone set to: {}", m_model->getTimezone().getTimezone())

    std::vector<std::string> defaultServers = { "0.br.pool.ntp.org" };

    auto collectCallback = [this](std::vector<std::string>& items) {
        // Timeserver settings
        // FIXME: We left std::to_array if we want to manage the input on the
        // view,
        //        making setTimeservers a little clunky.
        auto timeserver = std::to_array<std::pair<std::string, std::string>>(
            { { Messages::AddTimeserver::field, "" } });

        timeserver = m_view->fieldMenu(Messages::title,
            Messages::AddTimeserver::question, timeserver,
            Messages::AddTimeserver::help);

        items.push_back(timeserver[0].second);

        return true;
    };

    auto timeservers = m_view->collectListMenu(Messages::title,
        Messages::Timeservers::question, defaultServers,
        Messages::Timeservers::help, std::move(collectCallback));

    m_model->getTimezone().setTimeservers(timeservers);
    LOG_DEBUG("Timeservers set to {}",
        fmt::join(m_model->getTimezone().getTimeservers(), ", "));
}

};
