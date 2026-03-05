#include <opencattus/services/ansible/roles/confluent.h>
#include <opencattus/services/confluent.h>
#include <opencattus/services/log.h>

#ifdef BUILD_TESTING
#include <doctest/doctest.h>
#else
#define DOCTEST_CONFIG_DISABLE
#include <doctest/doctest.h>
#endif

#include <fmt/core.h>

namespace opencattus::services::ansible::roles::confluent {

void run(const Role& role)
{
    Confluent confluent;
    confluent.install();
}

}
