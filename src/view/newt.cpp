/*
 * Copyright 2021 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <algorithm>
#include <cstdio> /* sprintf() */
#include <cstring> /* strlen() */
#include <newt.h>
#include <opencattus/functions.h>
#include <opencattus/view/newt.h>
#include <string_view>

#ifdef BUILD_TESTING
#include <doctest/doctest.h>
#else
#define DOCTEST_CONFIG_DISABLE
#include <doctest/doctest.h>
#endif

namespace {

constexpr int dialogMargin = 6;
constexpr int minDialogWidth = 20;
constexpr int maxDialogWidth = 72;
constexpr int minDataWidth = 18;
constexpr int maxDataWidth = 28;
constexpr int minListHeight = 4;
constexpr int minFieldDialogWidth = 34;
constexpr int minListDialogWidth = 34;
constexpr int wrappedWindowRows = 2;
constexpr int windowShadowRows = 1;
constexpr int reservedHeaderRows = 1;
constexpr int reservedFooterRows = 1;
constexpr int minWindowTop = reservedHeaderRows + 1;

auto calculateDialogWidth(int cols) -> int
{
    return std::min(
        std::max(cols - dialogMargin, minDialogWidth), maxDialogWidth);
}

auto calculateDataWidth(int dialogWidth) -> int
{
    return std::clamp(dialogWidth / 3, minDataWidth, maxDataWidth);
}

auto calculateMaxListHeight(int rows) -> int
{
    return std::max(rows - 16, minListHeight);
}

auto calculateMaxDialogHeight(int rows) -> int
{
    return std::max(8,
        rows - reservedHeaderRows - reservedFooterRows - wrappedWindowRows
            - windowShadowRows - minWindowTop);
}

auto calculateDialogTop(int rows, int windowHeight) -> int
{
    const auto wrappedHeight = windowHeight + wrappedWindowRows;
    const auto centeredTop = std::max(minWindowTop, (rows - wrappedHeight) / 2);
    const auto lastSafeTop = std::max(minWindowTop,
        rows - windowHeight - wrappedWindowRows - windowShadowRows
            - reservedFooterRows);
    return std::min(centeredTop, lastSafeTop);
}

auto calculateFieldDialogWidth(
    int cols, int baseWidth, int dataWidth, std::size_t longestLabelWidth)
{
    const auto desiredWidth = std::max(minFieldDialogWidth,
        static_cast<int>(longestLabelWidth) + dataWidth + 8);
    const auto maxWidth
        = std::min(baseWidth, std::max(cols - 2, minDialogWidth));
    return std::clamp(desiredWidth, minDialogWidth, maxWidth);
}

auto maxLineWidth(std::string_view text) -> std::size_t
{
    std::size_t longest = 0;
    std::size_t current = 0;

    for (const auto character : text) {
        if (character == '\n') {
            longest = std::max(longest, current);
            current = 0;
            continue;
        }

        ++current;
    }

    return std::max(longest, current);
}

auto calculateListDialogWidth(int baseWidth, std::size_t longestItemWidth,
    std::size_t messageWidth, std::size_t titleWidth) -> int
{
    const auto desiredWidth = std::max({ minListDialogWidth,
        static_cast<int>(longestItemWidth) + 12,
        static_cast<int>(messageWidth) + 8, static_cast<int>(titleWidth) + 8 });

    return std::min(std::max(desiredWidth, minDialogWidth), baseWidth);
}

auto calculateListHeight(int maxListHeight, std::size_t itemCount) -> int
{
    return std::clamp(
        static_cast<int>(itemCount), minListHeight, maxListHeight);
}

auto fieldLabelAllowsEmpty(std::string_view label) -> bool
{
    return label.contains("(optional");
}

} // namespace

Newt::Newt()
    : m_flexDown(2)
    , m_flexUp(2)
{
    newtInit();
    newtCls();

    refreshScreenMetrics();

    // Push the title to the top left corner
    newtDrawRootText(0, 0, TUIText::title);

#ifndef NDEBUG
    const auto developmentHeader
        = fmt::format("{} {}", TUIText::version, TUIText::developmentBuild);
    const auto headerStart = 0 - static_cast<int>(developmentHeader.size());
    newtDrawRootText(headerStart, 0, TUIText::version);
    newtSetColor(NEWT_COLORSET_ROOTTEXT, const_cast<char*>("red"),
        const_cast<char*>("blue"));
    newtDrawRootText(
        headerStart + static_cast<int>(strlen(TUIText::version)) + 1, 0,
        TUIText::developmentBuild);
    newtSetColor(NEWT_COLORSET_ROOTTEXT, const_cast<char*>("yellow"),
        const_cast<char*>("blue"));
#else
    // Push the product version to the top right corner
    newtDrawRootText(
        0 - static_cast<int>(strlen(TUIText::version)), 0, TUIText::version);
#endif

    // Add the default help line in the bottom
    newtPushHelpLine(TUIText::helpLine);
    newtRefresh();
}

Newt::~Newt()
{
    if (!m_finished) {
        newtFinished();
    }
}

void Newt::refreshScreenMetrics()
{
    newtGetScreenSize(&m_cols, &m_rows);
    m_suggestedWidth = calculateDialogWidth(m_cols);
    m_dataWidth = calculateDataWidth(m_suggestedWidth);
    m_maxListHeight = calculateMaxListHeight(m_rows);
}

int Newt::fieldDialogWidth(const std::size_t longestLabelWidth) const
{
    return calculateFieldDialogWidth(
        m_cols, m_suggestedWidth, m_dataWidth, longestLabelWidth);
}

int Newt::listDialogWidth(const std::size_t longestItemWidth,
    std::string_view message, std::string_view title) const
{
    return calculateListDialogWidth(m_suggestedWidth, longestItemWidth,
        maxLineWidth(message), maxLineWidth(title));
}

int Newt::listHeight(const std::size_t itemCount) const
{
    return calculateListHeight(m_maxListHeight, itemCount);
}

int Newt::dialogLeftFor(const int windowWidth) const
{
    return std::max(0, (m_cols - windowWidth) / 2);
}

int Newt::dialogTopFor(const int windowHeight) const
{
    return calculateDialogTop(m_rows, windowHeight);
}

int Newt::maxDialogHeight() const { return calculateMaxDialogHeight(m_rows); }

void Newt::abort()
{
    if (!m_finished) {
        newtFinished();
        m_finished = true;
    }
    LOG_WARN("{}", TUIText::abort)
    throw ViewAbortRequested(TUIText::abort);
}

void Newt::goBack()
{
    LOG_INFO("Returning to the previous questionnaire step")
    throw ViewBackRequested("Questionnaire moved back to the previous step");
}

bool Newt::allowsEmptyField(const struct newtWinEntry& entry)
{
    if (entry.text == nullptr) {
        return false;
    }

    return fieldLabelAllowsEmpty(entry.text);
}

// TODO: Remove this method; this check must be done outside the view
bool Newt::hasEmptyField(const struct newtWinEntry* entries)
{
    /* This may result in a buffer overflow if the string is > 63 chars */
    char message[63] = {};

    /* This for loop will check for empty values on the entries, and it will
     * return true if any value is empty based on the length of the string.
     */
    for (unsigned i = 0; entries[i].text; i++) {
        if (allowsEmptyField(entries[i])) {
            continue;
        }

        if (strlen(*entries[i].value) == 0) {
            sprintf(message, "%s cannot be empty\n", entries[i].text);

            newtWinMessage(
                nullptr, const_cast<char*>(TUIText::Buttons::ok), message);
            return true;
        }
    }

    return false;
}

std::vector<const char*> Newt::convertToNewtList(
    const std::vector<std::string>& s)
{
    // Newt expects a NULL terminated array of C style strings
    std::vector<const char*> cStrings;
    cStrings.reserve(s.size() + 1);

    for (const auto& string : s) {
        cStrings.push_back(string.c_str());
        LOG_TRACE("Pushed back std::string {}", string.c_str())
    }
    cStrings.push_back(nullptr);
    LOG_TRACE("Pushed back nullptr")

    return cStrings;
}

/**
 * Show a progress message dialog
 * @param title
 * @param message
 * @param command
 * @param fPercent A function to transform a command output
 * into a percent (a 0 to 100 value)
 */
bool Newt::progressMenu(const char* title, const char* message,
    opencattus::services::CommandProxy&& cmd,
    std::function<std::optional<double>(opencattus::services::CommandProxy&)>
        fPercent)
{

    std::string text;

    auto* form = newtForm(nullptr, nullptr, NEWT_FLAG_NOF12);

    auto* progress = newtScale(10, -1, 61, 1000);
    auto* label = newtTextbox(-1, -1, 61, 1, NEWT_TEXTBOX_WRAP);
    newtTextboxSetText(label, text.c_str());

    char* dtitle = strdup(title);
    char* dmessage = strdup(message);

    newtGrid grid = newtCreateGrid(1, 3);
    newtComponent b1;
    newtGrid buttonGrid
        = newtButtonBar(const_cast<char*>(TUIText::Buttons::stop), &b1, NULL);
    newtGridSetField(grid, 0, 0, NEWT_GRID_COMPONENT, progress, 1, 1, 0, 0, 0,
        NEWT_GRID_FLAG_GROWX);
    newtGridSetField(grid, 0, 1, NEWT_GRID_COMPONENT, label, 1, 1, 0, 0, 0,
        NEWT_GRID_FLAG_GROWX | NEWT_GRID_FLAG_GROWY);
    newtGridSetField(grid, 0, 2, NEWT_GRID_SUBGRID, buttonGrid, 0, 1, 0, 0, 0,
        NEWT_GRID_FLAG_GROWX);
    int windowWidth = 0;
    int windowHeight = 0;
    newtGridGetSize(grid, &windowWidth, &windowHeight);
    newtGridWrappedWindowAt(
        grid, dtitle, dialogLeftFor(windowWidth), dialogTopFor(windowHeight));

    newtFormAddComponents(form, progress, label, b1, nullptr);
    newtFormWatchFd(form, cmd.pipe_stream.pipe().native_source(), NEWT_FD_READ);
    newtScaleSet(progress, 0);
    newtDrawForm(form);

    newtTextboxSetText(label, dmessage);

    newtExitStruct es = {};
    newtFormRun(form, &es);
    while (es.reason == 2) {
        auto last_value = fPercent(cmd);
        if (!last_value)
            break;

        newtScaleSet(progress, unsigned(*last_value * 10));

        newtFormRun(form, &es);
    }

    newtPopWindow();
    newtFormDestroy(form);
    newtRefresh();
    return es.u.co != b1;
}

void Newt::helpMessage(const char* message)
{
    newtBell();
    newtWinMessage(const_cast<char*>(TUIText::Help::title),
        const_cast<char*>(TUIText::Buttons::ok), const_cast<char*>(message));
}

TEST_CASE("newt geometry keeps dialogs readable on an 80x24 terminal")
{
    CHECK(calculateDialogWidth(80) == 72);
    CHECK(calculateDataWidth(calculateDialogWidth(80)) == 24);
    CHECK(calculateMaxListHeight(24) == 8);
    CHECK(calculateMaxDialogHeight(24) == 17);
    CHECK(calculateDialogTop(24, 16) == 3);
    CHECK(calculateDialogTop(24, 17) == 2);
    CHECK(calculateDialogTop(24, 19) == 2);
    CHECK(calculateFieldDialogWidth(80, 72, 24, 11) == 43);
    CHECK(calculateFieldDialogWidth(80, 72, 24, 20) == 52);
    CHECK(calculateFieldDialogWidth(80, 72, 24, 34) == 66);
    CHECK(calculateListDialogWidth(72, 10, 24, 15) == 34);
}

TEST_CASE("newt geometry stays within small terminals")
{
    CHECK(calculateDialogWidth(40) == 34);
    CHECK(calculateDataWidth(calculateDialogWidth(40)) == 18);
    CHECK(calculateMaxListHeight(18) == 4);
    CHECK(calculateListHeight(10, 2) == 4);
    CHECK(calculateListDialogWidth(34, 20, 24, 15) == 34);
}

TEST_CASE("newt field labels can mark fields optional with extra detail")
{
    CHECK(fieldLabelAllowsEmpty("Gateway (optional)"));
    CHECK(fieldLabelAllowsEmpty("Additional mail domains (optional)"));
    CHECK_FALSE(fieldLabelAllowsEmpty("SMTP server"));
}
