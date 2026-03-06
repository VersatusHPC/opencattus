#include <opencattus/NFS.h>
#include <opencattus/services/ansible/roles/xcat.h>
#include <opencattus/services/log.h>
#include <opencattus/services/xcat.h>

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

namespace opencattus::services::ansible::roles::xcat {

void run(const Role& role)
{
    XCAT xcat;
    xcat.install();
}

}
