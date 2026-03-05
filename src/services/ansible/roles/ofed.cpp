#include <opencattus/ofed.h>
#include <opencattus/services/ansible/roles/ofed.h>
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

void configureInfiniband()
{
    if (const auto& ofed = cluster()->getOFED()) {
        LOG_INFO("Setting up Infiniband support")
        ofed->install(); // shared pointer
    }
}

}

namespace opencattus::services::ansible::roles::ofed {

void run(const Role& role)
{
    LOG_INFO("Setting up Infiniband, use `--skip infiniband` to skip");
    if (opencattus::utils::singleton::options()->shouldSkip("infiniband")) {
        return;
    }
    configureInfiniband();
}

}
