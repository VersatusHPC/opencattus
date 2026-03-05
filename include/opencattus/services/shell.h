/*
 * Copyright 2021 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OPENCATTUS_SHELL_H_
#define OPENCATTUS_SHELL_H_

#include <opencattus/models/cluster.h>
#include <opencattus/services/execution.h>

namespace opencattus::services {

using opencattus::models::Cluster;
/**
 * @class Shell
 * @brief Manages the configuration and installation processes on a cluster.
 *
 * This class provides functionalities for configuring various system settings,
 * installing required packages, and setting up cluster-specific services.
 *
 * Note this class delegates the logic to the ansible roles
 */
class Shell final : public Execution {
public:
    /**
     * @brief Installs and configures the system.
     *
     * This function performs the installation and configuration processes.
     */
    void install() override;

    Shell();
};

};

#endif // OPENCATTUS_SHELL_H_
