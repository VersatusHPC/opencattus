/*
 * Copyright 2021 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OPENCATTUS_HEADNODE_H_
#define OPENCATTUS_HEADNODE_H_

#include <list>
#include <memory>
#include <string>

#include <opencattus/connection.h>
#include <opencattus/models/os.h>
#include <opencattus/models/server.h>
#include <opencattus/network.h>

namespace opencattus::models {
class Headnode : public Server {
public:
    enum class BootTarget { Text, Graphical };

private:
    BootTarget m_bootTarget;

private:
    void discoverNames();

public:
    Headnode();

    [[nodiscard]] BootTarget getBootTarget() const;
    void setBootTarget(BootTarget bootTarget);
};

}; // namespace opencattus::models

#endif // OPENCATTUS_HEADNODE_H_
