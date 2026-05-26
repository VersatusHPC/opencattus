#include <opencattus/functions.h>
#include <opencattus/models/os.h>
#include <opencattus/services/ansible/roles/sshd.h>
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

void disallowSSHRootPasswordLogin()
{
    LOG_INFO("Allowing root login only through public key authentication (SSH)")

    ::runner()->executeCommand(
        "sed -i \"/^#\\?PermitRootLogin/c\\PermitRootLogin without-password\""
        " /etc/ssh/sshd_config");
}

void enableHostbasedAuthentication()
{
    LOG_INFO("Enabling host-based authentication (SSH)")

    switch (::os().getPackageType()) {
        case opencattus::models::OS::PackageType::RPM:
            ::runner()->executeCommand("dnf install -y openssh-keysign");
            break;
        case opencattus::models::OS::PackageType::DEB:
            ::runner()->executeCommand("apt-get install -y openssh-client");
            break;
    }

    ::runner()->executeCommand(
        "sed -i \"/^#\\?HostbasedAuthentication/c\\HostbasedAuthentication "
        "yes\""
        " /etc/ssh/sshd_config");

    ::runner()->executeCommand(
        "sed -i \"/^#\\?IgnoreRhosts/c\\IgnoreRhosts no\""
        " /etc/ssh/sshd_config");

    ::runner()->executeCommand(
        "install -d -m 0755 /etc/ssh/ssh_config.d && "
        "cat > /etc/ssh/ssh_config.d/50-opencattus.conf <<'SSH_EOF'\n"
        "Host *\n"
        "    HostbasedAuthentication yes\n"
        "    EnableSSHKeysign yes\n"
        "    HostbasedKeyTypes *ed25519*\n"
        "SSH_EOF");
}

}

namespace opencattus::services::ansible::roles::sshd {

void run(const Role& /*role*/)
{
    disallowSSHRootPasswordLogin();
    enableHostbasedAuthentication();
}

}
