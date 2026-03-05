/*
 * Copyright 2022 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OPENCATTUS_PBS_H_
#define OPENCATTUS_PBS_H_

#include <opencattus/models/queuesystem.h>
#include <opencattus/services/runner.h>

namespace opencattus::models {

class PBS : public QueueSystem {
public:
    enum class ExecutionPlace { Shared, Scatter };

private:
    ExecutionPlace m_executionPlace = ExecutionPlace::Shared;

public:
    void setExecutionPlace(ExecutionPlace);
    ExecutionPlace getExecutionPlace(void);

public:
    explicit PBS(const Cluster& cluster);
};

};

#endif // OPENCATTUS_PBS_H_
