/*
 * Copyright 2021 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OPENCATTUS_EXECUTION_H_
#define OPENCATTUS_EXECUTION_H_

#include <string>

#include <opencattus/models/cluster.h>

class Execution {
public:
    virtual ~Execution() = default;

    virtual void install() = 0;
};

#endif // OPENCATTUS_EXECUTION_H_
