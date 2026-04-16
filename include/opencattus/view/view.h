/*
 * Copyright 2021 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OPENCATTUS_VIEW_H_
#define OPENCATTUS_VIEW_H_

#include <functional>
#include <cstddef>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <opencattus/services/runner.h>

/* This is just a prototype about making the View as an Interface to be easily
 * swapped in the future if needed. There's much more organization to do before
 * we start using abstract classes.
 */
class View {
public:
    using FieldEntries = std::vector<std::pair<std::string, std::string>>;
    using MultipleSelectionEntries
        = std::vector<std::tuple<std::string, std::string, bool>>;
    using ListButtonCallback = std::function<bool(std::vector<std::string>&)>;
    using ProgressCallback = std::function<std::optional<double>(
        opencattus::services::CommandProxy&)>;

    virtual ~View() = default;

    virtual void abort() = 0;
    virtual void helpMessage(const char*) = 0;

    virtual void message(const char*) = 0;
    virtual void message(const char*, const char*) = 0;
    virtual void fatalMessage(const char*, const char*) = 0;

    virtual void okCancelMessage(const char* message) = 0;
    virtual void okCancelMessage(const char* title, const char* message) = 0;
    virtual void okCancelMessagePairs(
        const char* title, const char* message, const FieldEntries& pairs)
        = 0;

    template <std::ranges::range T>
    void okCancelMessage(const char* title, const char* message, const T& pairs)
    {
        okCancelMessagePairs(
            title, message, FieldEntries(std::begin(pairs), std::end(pairs)));
    }

    template <std::ranges::range T>
    void okCancelMessage(const char* message, const T& pairs)
    {
        okCancelMessage(nullptr, message, pairs);
    }

    virtual std::pair<int, std::vector<std::string>> multipleSelectionMenu(
        const char* title, const char* message, const char* help,
        MultipleSelectionEntries items)
        = 0;

    virtual std::string listMenuImpl(const char* title, const char* message,
        const std::vector<std::string>& items, const char* helpMessage)
        = 0;

    template <std::ranges::range T>
    std::string listMenu(
        const char* title, const char* message, const T& items,
        const char* helpMessage)
    {
        return listMenuImpl(title, message,
            std::vector<std::string>(std::begin(items), std::end(items)),
            helpMessage);
    }

    virtual std::vector<std::string> collectListMenuImpl(const char* title,
        const char* message, const std::vector<std::string>& items,
        const char* helpMessage, ListButtonCallback addCallback)
        = 0;

    template <std::ranges::range T>
    std::vector<std::string> collectListMenu(const char* title,
        const char* message, const T& items, const char* helpMessage,
        ListButtonCallback&& addCallback)
    {
        return collectListMenuImpl(title, message,
            std::vector<std::string>(std::begin(items), std::end(items)),
            helpMessage, std::move(addCallback));
    }

    virtual FieldEntries fieldMenuImpl(const char* title, const char* message,
        const FieldEntries& items, const char* helpMessage)
        = 0;

    template <std::ranges::range T>
    T fieldMenu(const char* title, const char* message, const T& items,
        const char* helpMessage)
    {
        const auto input = FieldEntries(std::begin(items), std::end(items));
        const auto output = fieldMenuImpl(title, message, input, helpMessage);
        if (output.size() != input.size()) {
            throw std::runtime_error(
                "View::fieldMenuImpl returned a different number of fields");
        }

        T result {};
        for (std::size_t i = 0; i < output.size(); ++i) {
            result[i] = output[i];
        }

        return result;
    }

    virtual bool progressMenu(const char* title, const char* message,
        opencattus::services::CommandProxy&& command,
        ProgressCallback fPercent)
        = 0;

    virtual bool yesNoQuestion(
        const char* title, const char* message, const char* helpMessage)
        = 0;
};

#endif // OPENCATTUS_VIEW_H_
