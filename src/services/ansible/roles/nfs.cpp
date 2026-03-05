#include <opencattus/NFS.h>
#include <opencattus/services/ansible/roles/nfs.h>
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
}

namespace opencattus::services::ansible::roles::nfs {

ScriptBuilder installScript(const Role& role, const OS& osinfo)
{
    NFS networkFileSystem = NFS("pub", "/opt/ohpc",
        cluster()
            ->getHeadnode()
            .getConnection(Network::Profile::Management)
            .getAddress(),
        "ro,no_subtree_check");

    return networkFileSystem.installScript(osinfo);
}

}
