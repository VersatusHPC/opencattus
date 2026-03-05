/*
 * Copyright 2021 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OPENCATTUS_VIEW_H_
#define OPENCATTUS_VIEW_H_

/* This is just a prototype about making the View as an Interface to be easily
 * swapped in the future if needed. There's much more organization to do before
 * we start using abstract classes.
 */
class View {
public:
    virtual ~View() = default;

    virtual void abort() = 0;
    virtual void helpMessage(const char*) = 0;
};

#endif // OPENCATTUS_VIEW_H_
