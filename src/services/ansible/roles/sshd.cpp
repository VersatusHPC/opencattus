#include <opencattus/services/ansible/roles/sshd.h>
#include <opencattus/services/log.h>

#ifdef BUILD_TESTING
#include <doctest/doctest.h>
#else
#define DOCTEST_CONFIG_DISABLE
#include <doctest/doctest.h>
#endif

#include <fmt/core.h>

namespace opencattus::services::ansible::roles::sshd {

ScriptBuilder installScript(
    const Role& role, const opencattus::models::OS& osinfo)
{
    ScriptBuilder builder(osinfo);

    LOG_ASSERT(role.roleName() == "sshd",
        fmt::format("Expected sshd role, found {}", role.roleName()));

    builder.addNewLine()
        .addCommand("# Disable root password login, allow public key only")
        .addCommand("sed -i \"/^#\\?PermitRootLogin/c\\PermitRootLogin "
                    "without-password\""
                    " /etc/ssh/sshd_config");

    builder.addNewLine()
        .addCommand("# Install ssh-keysign for host-based authentication")
        .addPackage(
            osinfo.getPackageType() == opencattus::models::OS::PackageType::RPM
                ? "openssh-keysign"
                : "openssh-client");

    builder.addNewLine()
        .addCommand("# Enable host-based authentication in sshd")
        .addCommand(
            "sed -i \"/^#\\?HostbasedAuthentication/c\\HostbasedAuthentication "
            "yes\""
            " /etc/ssh/sshd_config")
        .addCommand("sed -i \"/^#\\?IgnoreRhosts/c\\IgnoreRhosts no\""
                    " /etc/ssh/sshd_config");

    builder.addNewLine()
        .addCommand("# Configure SSH client for host-based authentication")
        .addCommand(
            "install -d -m 0755 /etc/ssh/ssh_config.d && "
            "cat > /etc/ssh/ssh_config.d/50-opencattus.conf <<'SSH_EOF'\n"
            "Host *\n"
            "    HostbasedAuthentication yes\n"
            "    EnableSSHKeysign yes\n"
            "    HostbasedKeyTypes *ed25519*\n"
            "SSH_EOF");

    return builder;
}

} // namespace opencattus::services::ansible::roles::sshd
