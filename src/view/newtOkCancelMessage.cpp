/*
 * Copyright 2021 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <opencattus/view/newt.h>

#include <algorithm>
#include <string_view>

namespace {

auto leadingSpaces(std::string_view line) -> std::size_t
{
    return static_cast<std::size_t>(std::ranges::find_if(line, [](char c) {
        return c != ' ';
    }) - line.begin());
}

auto wrapLine(std::string_view line, std::size_t width) -> std::string
{
    if (line.size() <= width || width == 0) {
        return std::string(line);
    }

    std::string out;
    auto remaining = std::string(line);
    auto continuationIndent = std::string(leadingSpaces(line) + 2, ' ');

    while (remaining.size() > width) {
        auto split = remaining.rfind(' ', width);
        if (split == std::string_view::npos || split == 0) {
            split = width;
        }

        out += remaining.substr(0, split);
        out += '\n';
        remaining = split + 1 < remaining.size() ? remaining.substr(split + 1)
                                                 : std::string {};
        const auto firstText = remaining.find_first_not_of(' ');
        remaining = firstText == std::string::npos
            ? std::string {}
            : remaining.substr(firstText);
        remaining = continuationIndent + remaining;
    }

    out += remaining;
    return out;
}

auto wrapText(std::string_view text, std::size_t width) -> std::string
{
    std::string out;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find('\n', start);
        const auto line = end == std::string_view::npos
            ? text.substr(start)
            : text.substr(start, end - start);
        out += wrapLine(line, width);
        if (end == std::string_view::npos) {
            break;
        }

        out += '\n';
        start = end + 1;
    }

    return out;
}

auto scrollableBodyHeight(int maxListHeight) -> int
{
    return std::max(4, maxListHeight + 2);
}

auto scrollableBodyWidth(int columns) -> int
{
    return std::max(24, columns - 18);
}

} // namespace

void Newt::okCancelMessage(const char* message)
{
    Newt::okCancelMessage(nullptr, message);
}

void Newt::okCancelMessage(const char* title, const char* message)
{
    int returnValue;

    /* Information about the installation scheme */
    while (true) {
        returnValue = newtWinChoice(const_cast<char*>(title),
            const_cast<char*>(TUIText::Buttons::ok),
            const_cast<char*>(TUIText::Buttons::stop),
            const_cast<char*>(message));

        switch (returnValue) {
            case 0:
                continue;
            case 1:
                return;
            case 2:
                abort();
                break;
            default:
                throw std::runtime_error(
                    "Something happened. Please run the software again");
        }
    }
}

void Newt::scrollableMessage(const char* title, const char* message,
    const char* text, const char* helpMessage)
{
    refreshScreenMetrics();

    const auto* safeTitle = title == nullptr ? "" : title;
    const auto* safeMessage = message == nullptr ? "" : message;
    const auto bodyWidth = scrollableBodyWidth(m_cols);
    const auto wrappedText = wrapText(
        text == nullptr ? "" : text, static_cast<std::size_t>(bodyWidth));

    auto* form = newtForm(nullptr, nullptr, NEWT_FLAG_NOF12);
    auto* label = newtTextboxReflowed(
        0, 0, const_cast<char*>(safeMessage), bodyWidth, 0, 0, 0);
    auto bodyHeight = scrollableBodyHeight(m_maxListHeight);
    auto* body = newtTextbox(0, 0, bodyWidth, bodyHeight, NEWT_FLAG_SCROLL);
    newtTextboxSetColors(body, NEWT_COLORSET_TEXTBOX, NEWT_COLORSET_TEXTBOX);
    newtTextboxSetText(body, wrappedText.c_str());

    newtGrid grid = newtCreateGrid(1, 3);
    newtComponent buttonOk, buttonCancel, buttonHelp;
    newtGrid buttonGrid = newtButtonBar(const_cast<char*>(TUIText::Buttons::ok),
        &buttonOk, const_cast<char*>(TUIText::Buttons::stop), &buttonCancel,
        const_cast<char*>(TUIText::Buttons::help), &buttonHelp, NULL);
    newtGridSetField(grid, 0, 0, NEWT_GRID_COMPONENT, label, 1, 0, 0, 0, 0,
        NEWT_GRID_FLAG_GROWX);
    newtGridSetField(grid, 0, 1, NEWT_GRID_COMPONENT, body, 1, 1, 2, 0, 0,
        NEWT_GRID_FLAG_GROWX | NEWT_GRID_FLAG_GROWY);
    newtGridSetField(grid, 0, 2, NEWT_GRID_SUBGRID, buttonGrid, 0, 1, 0, 0, 0,
        NEWT_GRID_FLAG_GROWX);

    int windowWidth = 0;
    int windowHeight = 0;
    newtGridGetSize(grid, &windowWidth, &windowHeight);
    const auto maxWindowHeight = maxDialogHeight();
    while (bodyHeight > 4 && windowHeight > maxWindowHeight) {
        --bodyHeight;
        newtTextboxSetHeight(body, bodyHeight);
        newtGridGetSize(grid, &windowWidth, &windowHeight);
    }

    newtGridWrappedWindowAt(grid, const_cast<char*>(safeTitle),
        dialogLeftFor(windowWidth), dialogTopFor(windowHeight));
    newtGridAddComponentsToForm(grid, form, 1);
    newtFormSetCurrent(form, buttonOk);
    newtRefresh();

    while (true) {
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
            break;
        }
        if (es.u.co == buttonHelp) {
            this->helpMessage(helpMessage);
            continue;
        }
        if (es.u.co == body) {
            continue;
        }

        newtPopWindow();
        newtFormDestroy(form);
        newtRefresh();
        abort();
    }

    newtPopWindow();
    newtFormDestroy(form);
    newtRefresh();
}

void Newt::okCancelMessagePairs(
    const char* title, const char* message, const View::FieldEntries& pairs)
{
    int returnValue;
    std::string newMessage = message;

    newMessage += "\n\n";
    for (const auto& pair : pairs) {
        newMessage += fmt::format("{} -> {}\n", pair.first, pair.second);
    }

    while (true) {
        returnValue = newtWinChoice(const_cast<char*>(title),
            const_cast<char*>(TUIText::Buttons::ok),
            const_cast<char*>(TUIText::Buttons::stop),
            const_cast<char*>(newMessage.c_str()));

        switch (returnValue) {
            case 0:
                continue;
            case 1:
                return;
            case 2:
                abort();
                break;
            default:
                throw std::runtime_error("Out of bounds in a switch statement");
        }
    }
}

// TODO: Primitive implementation, make it better.
