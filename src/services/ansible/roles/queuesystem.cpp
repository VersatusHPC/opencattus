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
                const auto& pbs
                    = dynamic_cast<opencattus::models::PBS*>(queue.value().get());

                osservice()->install("openpbs-server-ohpc");
                osservice()->enableService("pbs");
                ::runner()->executeCommand(
                    "qmgr -c \"set server default_qsub_arguments= -V\"");
                ::runner()->executeCommand(fmt::format(
                    "qmgr -c \"set server resources_default.place={}\"",
                    opencattus::utils::enums::toString<
                        opencattus::models::PBS::ExecutionPlace>(
                        pbs->getExecutionPlace())));
                ::runner()->executeCommand(
                    "qmgr -c \"set server job_history_enable=True\"");
                break;
            }
        }
    }
}

}
namespace opencattus::services::ansible::roles::queuesystem {

void run(const Role& role) { configureQueueSystem(); }

}
