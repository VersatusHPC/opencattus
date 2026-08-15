#include <opencattus/models/pbs.h>
#include <opencattus/models/queuesystem.h>
#include <opencattus/models/slurm.h>
#include <opencattus/services/ansible/roles.h>
#include <opencattus/services/ansible/roles/queuesystem.h>
#include <opencattus/services/log.h>

#ifdef BUILD_TESTING
#include <doctest/doctest.h>
#else
#define DOCTEST_CONFIG_DISABLE
#include <doctest/doctest.h>
#endif

#include <fmt/core.h>

namespace {
using namespace opencattus::utils::singleton;
using namespace opencattus::services::ansible;
void configureQueueSystem()
{
    LOG_INFO("Setting up the queue system")

    if (const auto& queue = cluster()->getQueueSystem()) {
        switch (queue.value()->getKind()) {
            case opencattus::models::QueueSystem::Kind::None: {
                __builtin_unreachable();
                break;
            }

            case opencattus::models::QueueSystem::Kind::SLURM: {
                roles::run(roles::Roles::SLURM, os());
                break;
            }

            case opencattus::models::QueueSystem::Kind::PBS: {
                const auto& pbs = dynamic_cast<opencattus::models::PBS*>(
                    queue.value().get());

                osservice()->install("openpbs-server-ohpc");
                // The OHPC package ships a pbs.conf with a placeholder
                // server name; normalize it to this head node before the
                // service starts.
                ::runner()->executeCommand(
                    fmt::format("sed -i \"s/^PBS_SERVER=.*/PBS_SERVER={}/\" "
                                "/etc/pbs.conf",
                        cluster()->getHeadnode().getHostname()));
                osservice()->enableService("pbs");
                // OHPC installs OpenPBS under /opt/pbs; its profile.d PATH
                // entry only applies to login shells, and the runner execs
                // directly, so qmgr must be called by absolute path.
                ::runner()->executeCommand("/opt/pbs/bin/qmgr -c \"set "
                                           "server default_qsub_arguments= "
                                           "-V\"");
                ::runner()->executeCommand(
                    fmt::format("/opt/pbs/bin/qmgr -c \"set server "
                                "resources_default.place={}\"",
                        opencattus::utils::enums::toString<
                            opencattus::models::PBS::ExecutionPlace>(
                            pbs->getExecutionPlace())));
                ::runner()->executeCommand("/opt/pbs/bin/qmgr -c \"set "
                                           "server job_history_enable=True\"");
                // PBS only schedules on nodes registered with the server;
                // nothing else creates them (issue #63 follow-through).
                for (const auto& node : cluster()->getNodes()) {
                    ::runner()->executeCommand(
                        fmt::format("/opt/pbs/bin/qmgr -c \"create node {}\"",
                            node.getHostname()));
                }
                break;
            }
        }
    }
}

}
namespace opencattus::services::ansible::roles::queuesystem {

void run(const Role& role) { configureQueueSystem(); }

}
