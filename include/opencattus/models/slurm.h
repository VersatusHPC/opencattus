/*
 * Copyright 2022 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OPENCATTUS_SLURM_H_
#define OPENCATTUS_SLURM_H_

#include <opencattus/models/queuesystem.h>

namespace opencattus::models {
/**
 * @class SLURM
 * @brief Manages SLURM server installation and configuration.
 */
class SLURM : public QueueSystem {
private:
    bool m_accounting { false };

public:
    explicit SLURM(const Cluster& cluster);

    /**
     * @brief Installs the SLURM server package on the system.
     */
    static void installServer();

    /**
     * @brief Configures the SLURM server.
     */
    void configureServer();

    /**
     * @brief Enables the SLURM server to start at boot.
     */
    static void enableServer();

    /**
     * @brief Starts the SLURM server.
     */
    static void startServer();
};

};
#endif // OPENCATTUS_SLURM_H_
