/*
 * Copyright 2021 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <opencattus/view/newt.h>

bool Newt::yesNoQuestion(
    const char* title, const char* message, const char* helpMessage)
{
    int returnValue;

    while (true) {
        returnValue = newtWinTernary(const_cast<char*>(title),
            const_cast<char*>(TUIText::Buttons::yes),
            const_cast<char*>(TUIText::Buttons::no),
            const_cast<char*>(TUIText::Buttons::help),
            const_cast<char*>(message), NULL);

        switch (returnValue) {
            case 0:
                continue;
            case 1:
                return true;
            case 2:
                return false;
            case 3:
                this->helpMessage(helpMessage);
                break;
            default:
                __builtin_unreachable();
        }
    }
}
