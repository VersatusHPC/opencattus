/*
 * Copyright 2021 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <opencattus/view/newt.h>

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
            const_cast<char*>(TUIText::Buttons::cancel),
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
            const_cast<char*>(TUIText::Buttons::cancel),
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
