#include <opencattus/models/cluster.h>
#include <opencattus/patterns/singleton.h>
#include <opencattus/services/ansible/roles/repos.h>
#include <opencattus/services/log.h>

#ifdef BUILD_TESTING
#include <doctest/doctest.h>
#else
#define DOCTEST_CONFIG_DISABLE
#include <doctest/doctest.h>
#endif

#include <fmt/core.h>

namespace opencattus::services::ansible::roles::repos {

void run(const Role& role)
{
    const auto& osinfo
        = opencattus::Singleton<models::Cluster>::get()->getHeadnode().getOS();
    auto repos = opencattus::Singleton<services::repos::RepoManager>::get();
    repos->initializeDefaultRepositories();
}

}
