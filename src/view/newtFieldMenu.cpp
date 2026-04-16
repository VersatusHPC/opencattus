/*
 * Copyright 2021 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <opencattus/view/newt.h>

View::FieldEntries Newt::fieldMenuImpl(const char* title, const char* message,
    const View::FieldEntries& items, const char* helpMessage)
{
    int returnValue;

    std::size_t arraySize = items.size();
    auto fieldEntries = std::make_unique<char*[]>(arraySize + 1);
    auto field = std::make_unique<newtWinEntry[]>(arraySize + 1);

    for (std::size_t i = 0; i < items.size(); ++i) {
        field[i].text = const_cast<char*>(items[i].first.c_str());
        fieldEntries[i] = const_cast<char*>(items[i].second.c_str());
        LOG_TRACE("fieldEntries[{}] = {}", i, fieldEntries[i])

        field[i].value = &fieldEntries[i];

        if (items[i].first.contains("Password")
            || items[i].first.contains("password")) {
            field[i].flags = NEWT_FLAG_PASSWORD;
        } else {
            field[i].flags = 0;
        }
    }

    field[arraySize].text = nullptr;
    field[arraySize].value = nullptr;
    field[arraySize].flags = 0;

    bool stay = true;
    while (stay) {
        returnValue = newtWinEntries(const_cast<char*>(title),
            const_cast<char*>(message), m_suggestedWidth, m_flexDown,
            m_flexUp, m_dataWidth, field.get(),
            const_cast<char*>(TUIText::Buttons::ok),
            const_cast<char*>(TUIText::Buttons::cancel),
            const_cast<char*>(TUIText::Buttons::help), nullptr);
        stay = false;

        switch (returnValue) {
            case 0:
            case 1: {
                if (hasEmptyField(field.get())) {
                    stay = true;
                    continue;
                }

                View::FieldEntries result;
                result.reserve(items.size());
                for (std::size_t i = 0; field[i].text; ++i) {
                    result.emplace_back(field[i].text, *field[i].value);
                }

                return result;
            }
            case 2:
                abort();
                break;
            case 3:
                this->helpMessage(helpMessage);
                stay = true;
                break;
            default:
                throw std::runtime_error(
                    "Invalid return value from fields on newt library");
        }
    }

    throw std::runtime_error("Invalid return path on newt library");
}
