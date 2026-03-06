/*
 * Copyright 2021 Vinícius Ferrão <vinicius@ferrao.net.br>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <opencattus/functions.h>
#include <opencattus/services/ansible/roles.h>
#include <opencattus/services/ansible/roles/aide.h>
#include <opencattus/services/ansible/roles/audit.h>
#include <opencattus/services/ansible/roles/fail2ban.h>
#include <opencattus/services/ansible/roles/spack.h>
#include <opencattus/services/log.h>
#include <opencattus/services/options.h>
#include <opencattus/services/osservice.h>
#include <opencattus/services/repos.h>
#include <opencattus/services/runner.h>
#include <opencattus/services/shell.h>
#include <opencattus/services/xcat.h>

#include <boost/process.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <chrono>
#include <fmt/format.h>
#include <memory>

#include <opencattus/NFS.h>
#include <opencattus/models/cluster.h>
#include <opencattus/models/pbs.h>
#include <opencattus/models/queuesystem.h>
#include <opencattus/models/slurm.h>
#include <opencattus/utils/singleton.h>

#include <opencattus/dbus_client.h>
#include <ranges>

using opencattus::models::Cluster;
using opencattus::models::OS;
using opencattus::services::IOSService;
using opencattus::services::IRunner;

namespace opencattus::services {

Shell::Shell()
{
    // Initialize directory tree
    functions::createDirectory(std::string { installPath } + "/backup");
    functions::createDirectory(
        std::string { installPath } + "/conf/node/etc/auto.master.d");
}

/* This method is the entrypoint of shell based cluster install
 * The first session of the method will configure and install services on the
 * headnode. The last part will do provisioner related settings and image
 * creation for network booting
 */
void Shell::install()
{
    namespace ansible = ansible::roles;
    constexpr auto run = [](ansible::Roles role) {
        const auto osinfo = utils::singleton::os();
        ansible::run(role, osinfo);
    };

    run(ansible::Roles::DUMP);
    run(ansible::Roles::CHECK);
    run(ansible::Roles::REPOS);
    run(ansible::Roles::BASE);
    run(ansible::Roles::NETWORK);
    run(ansible::Roles::SSHD);
    run(ansible::Roles::SELINUX);
    run(ansible::Roles::LOCALE);
    run(ansible::Roles::TIMESYNC);
    run(ansible::Roles::FIREWALL);
    run(ansible::Roles::OFED);
    run(ansible::Roles::FAIL2BAN);
    run(ansible::Roles::AUDIT);
    run(ansible::Roles::AIDE);
    run(ansible::Roles::SPACK);
    run(ansible::Roles::NFS);
    run(ansible::Roles::OHPC);
    run(ansible::Roles::QUEUESYSTEM);
    run(ansible::Roles::PROVISIONER);
}

}
