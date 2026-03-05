#include <opencattus/functions.h>
#include <opencattus/services/ansible/roles/sshd.h>
#include <opencattus/services/log.h>

#ifdef BUILD_TESTING
#include <doctest/doctest.h>
#else
#define DOCTEST_CONFIG_DISABLE
#include <doctest/doctest.h>
#endif

#include <fmt/core.h>

// @FIXME: Add support for hostbased auth

namespace {

using namespace opencattus::utils::singleton;

void disallowSSHRootPasswordLogin()
{
    LOG_INFO("Allowing root login only through public key authentication (SSH)")

    ::runner()->executeCommand(
        "sed -i \"/^#\\?PermitRootLogin/c\\PermitRootLogin without-password\""
        " /etc/ssh/sshd_config");
}

}

namespace opencattus::services::ansible::roles::sshd {

void run(const Role& /*role*/) { disallowSSHRootPasswordLogin(); }

}
