/*
 * Copyright 2021 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

// This file is empty since the method is now a template

#include <algorithm>
#include <cstdlib>
#include <newt.h>
#include <opencattus/view/newt.h>
#include <optional>
#include <unordered_set>

static constexpr int scrollBarWidth = 3;

static auto checkboxLabel(std::string_view label, bool selected) -> std::string
{
    return fmt::format(" [{}] {}", selected ? "*" : " ", label);
}

static std::vector<std::string> retrieveSelectedItems(newtComponent list)
{
    int selectCount = 0;
    auto** selectedItems = newtListboxGetSelection(list, &selectCount);
    std::vector<std::string> ret;
    ret.reserve(static_cast<std::size_t>(selectCount));
    for (int index = 0; index < selectCount; ++index) {
        ret.emplace_back(static_cast<const char*>(selectedItems[index]));
    }
    std::free(selectedItems);

    return ret;
}

static auto longestCheckboxLabel(const View::MultipleSelectionEntries& items)
    -> std::size_t
{
    std::size_t longest = 0;
    for (const auto& entry : items) {
        longest = std::max(
            longest, checkboxLabel(std::get<1>(entry), false).size());
    }

    return longest;
}

struct CheckboxListState {
    newtComponent list;
    const View::MultipleSelectionEntries* items;
    std::vector<std::string> labels;
};

static void updateCheckboxListLabels(newtComponent, void* data)
{
    auto* state = static_cast<CheckboxListState*>(data);
    int selectedCount = 0;
    auto** rawSelected = newtListboxGetSelection(state->list, &selectedCount);

    std::unordered_set<const void*> selected;
    selected.reserve(static_cast<std::size_t>(selectedCount));
    for (int index = 0; index < selectedCount; ++index) {
        selected.insert(rawSelected[index]);
    }
    std::free(rawSelected);

    for (std::size_t index = 0; index < state->items->size(); ++index) {
        const auto& [key, item, enabled] = state->items->at(index);
        static_cast<void>(enabled);
        state->labels[index]
            = checkboxLabel(item, selected.contains(key.c_str()));
        newtListboxSetEntry(
            state->list, static_cast<int>(index), state->labels[index].c_str());
    }
}

std::string Newt::listMenuImpl(const char* title, const char* message,
    const std::vector<std::string>& items, const char* helpMessage)
{
    refreshScreenMetrics();

    if (items.empty()) {
        fatalMessage(title, "This menu has no available options to display.");
    }

    const auto* safeTitle = title == nullptr ? "" : title;
    const auto* safeMessage = message == nullptr ? "" : message;
    auto* form = newtForm(nullptr, nullptr, NEWT_FLAG_NOF12);
    std::size_t longestItemWidth = 0;
    for (const auto& item : items) {
        longestItemWidth = std::max(longestItemWidth, item.size());
    }

    const auto maxListWidth = std::max(28, m_suggestedWidth - 10);
    const auto visibleListHeight = listHeight(items.size());
    const auto listFlags
        = items.size() > static_cast<std::size_t>(visibleListHeight)
        ? NEWT_FLAG_SCROLL
        : 0;
    const auto scrollAdjust = listFlags == 0 ? 0 : scrollBarWidth;
    const auto listWidth = std::clamp(
        static_cast<int>(longestItemWidth) + scrollAdjust, 28, maxListWidth);
    const auto textWidth = std::max(32, listWidth);

    auto* label = newtTextboxReflowed(
        0, 0, const_cast<char*>(safeMessage), textWidth, 0, 0, 0);
    auto* list = newtListbox(
        0, 0, visibleListHeight, listFlags | NEWT_FLAG_RETURNEXIT);
    newtListboxSetWidth(list, listWidth);
    for (const auto& item : items) {
        newtListboxAppendEntry(list, item.c_str(), item.c_str());
    }

    newtGrid grid = newtCreateGrid(1, 3);
    newtComponent buttonOk, buttonCancel, buttonHelp;
    newtGrid buttonGrid = newtButtonBar(const_cast<char*>(TUIText::Buttons::ok),
        &buttonOk, const_cast<char*>(TUIText::Buttons::cancel), &buttonCancel,
        const_cast<char*>(TUIText::Buttons::help), &buttonHelp, NULL);
    newtGridSetField(grid, 0, 0, NEWT_GRID_COMPONENT, label, 1, 1, 0, 0, 0,
        NEWT_GRID_FLAG_GROWX);
    newtGridSetField(grid, 0, 1, NEWT_GRID_COMPONENT, list, 1, 1, 2, 0, 0,
        NEWT_GRID_FLAG_GROWX | NEWT_GRID_FLAG_GROWY);
    newtGridSetField(grid, 0, 2, NEWT_GRID_SUBGRID, buttonGrid, 0, 1, 0, 0, 0,
        NEWT_GRID_FLAG_GROWX);

    int windowWidth = 0;
    int windowHeight = 0;
    newtGridGetSize(grid, &windowWidth, &windowHeight);
    newtGridWrappedWindowAt(grid, const_cast<char*>(safeTitle),
        std::max(0, (m_cols - windowWidth) / 2),
        std::max(1, (m_rows - windowHeight) / 2));
    newtGridAddComponentsToForm(grid, form, 1);
    newtRefresh();

    auto selected = std::optional<std::string> {};
    auto stopRequested = false;
    while (!selected && !stopRequested) {
        newtExitStruct es = {};
        newtFormRun(form, &es);

        while (es.reason == newtExitStruct::NEWT_EXIT_FDREADY) {
            newtFormRun(form, &es);
        }

        if (es.reason == newtExitStruct::NEWT_EXIT_HOTKEY
            && es.u.key == NEWT_KEY_F12) {
            continue;
        }

        if (es.u.co == list || es.u.co == buttonOk) {
            if (auto* current = newtListboxGetCurrent(list);
                current != nullptr) {
                selected = static_cast<const char*>(current);
            }
        } else if (es.u.co == buttonHelp) {
            this->helpMessage(helpMessage);
        } else {
            stopRequested = true;
        }
    }

    newtPopWindow();
    newtFormDestroy(form);
    newtRefresh();

    if (stopRequested) {
        abort();
    }

    return *selected;
}

std::vector<std::string> Newt::collectListMenuImpl(const char* title,
    const char* message, const std::vector<std::string>& items,
    const char* helpMessage, ListButtonCallback addCallback)
{
    return Newt::collectListMenu(
        title, message, items, helpMessage, std::move(addCallback));
}

std::pair<int, std::vector<std::string>> Newt::checkboxSelectionMenu(
    const char* title, const char* message, const char* help,
    View::MultipleSelectionEntries items)
{
    refreshScreenMetrics();

    auto* form = newtForm(nullptr, nullptr, NEWT_FLAG_NOF12);
    const auto maxListWidth = std::max(28, m_suggestedWidth - 10);
    const auto maxVisibleRows = std::min(m_maxListHeight, 8);
    const auto visibleListHeight
        = std::min(listHeight(items.size()), maxVisibleRows);
    const auto listFlags
        = items.size() > static_cast<std::size_t>(visibleListHeight)
        ? NEWT_FLAG_SCROLL
        : 0;
    const auto scrollAdjust = listFlags == 0 ? 0 : scrollBarWidth;
    const auto listWidth = std::clamp(
        static_cast<int>(longestCheckboxLabel(items)) + scrollAdjust, 28,
        maxListWidth);
    const auto textWidth = std::max(32, listWidth);

    newtSetColor(NEWT_COLORSET_SELLISTBOX, const_cast<char*>("black"),
        const_cast<char*>("lightgray"));
    newtSetColor(NEWT_COLORSET_ACTSELLISTBOX, const_cast<char*>("white"),
        const_cast<char*>("red"));

    auto* label = newtTextboxReflowed(
        0, 0, const_cast<char*>(message), textWidth, 0, 0, 0);

    auto* list
        = newtListbox(0, 0, visibleListHeight, listFlags | NEWT_FLAG_MULTIPLE);
    newtListboxSetWidth(list, listWidth);

    CheckboxListState checkboxState {
        .list = list,
        .items = &items,
        .labels = {},
    };
    checkboxState.labels.reserve(items.size());
    for (const auto& [key, item, enabled] : items) {
        checkboxState.labels.emplace_back(checkboxLabel(item, enabled));
        newtListboxAppendEntry(
            list, checkboxState.labels.back().c_str(), key.c_str());
        if (enabled) {
            newtListboxSelectItem(list, key.c_str(), NEWT_FLAGS_SET);
        }
    }
    newtComponentAddCallback(list, updateCheckboxListLabels, &checkboxState);

    newtGrid grid = newtCreateGrid(1, 3);
    newtComponent buttonOk, buttonCancel, buttonHelp;
    newtGrid buttonGrid = newtButtonBar(const_cast<char*>(TUIText::Buttons::ok),
        &buttonOk, const_cast<char*>(TUIText::Buttons::cancel), &buttonCancel,
        const_cast<char*>(TUIText::Buttons::help), &buttonHelp, NULL);
    newtGridSetField(grid, 0, 0, NEWT_GRID_COMPONENT, label, 1, 1, 0, 0, 0,
        NEWT_GRID_FLAG_GROWX);
    newtGridSetField(grid, 0, 1, NEWT_GRID_COMPONENT, list, 1, 1, 2, 0, 0,
        NEWT_GRID_FLAG_GROWX | NEWT_GRID_FLAG_GROWY);
    newtGridSetField(grid, 0, 2, NEWT_GRID_SUBGRID, buttonGrid, 0, 1, 0, 0, 0,
        NEWT_GRID_FLAG_GROWX);
    int windowWidth = 0;
    int windowHeight = 0;
    newtGridGetSize(grid, &windowWidth, &windowHeight);
    newtGridWrappedWindowAt(grid, const_cast<char*>(title),
        std::max(0, (m_cols - windowWidth) / 2),
        std::max(1, (m_rows - windowHeight) / 2));

    newtGridAddComponentsToForm(grid, form, 1);

    newtRefresh();

    int retval = 0;

    while (retval == 0) {
        newtExitStruct es = {};
        newtFormRun(form, &es);

        while (es.reason == newtExitStruct::NEWT_EXIT_FDREADY) {
            newtFormRun(form, &es);
        }

        if (es.reason == newtExitStruct::NEWT_EXIT_HOTKEY
            && es.u.key == NEWT_KEY_F12) {
            continue;
        }

        if (es.u.co == buttonOk) {
            retval = 1;
        } else if (es.u.co == buttonHelp) {
            this->helpMessage(help);
            continue;
        } else {
            retval = 2;
        }
    }

    auto ret = retrieveSelectedItems(list);
    newtPopWindow();
    newtFormDestroy(form);
    newtRefresh();

    return std::make_pair(retval, ret);
}
