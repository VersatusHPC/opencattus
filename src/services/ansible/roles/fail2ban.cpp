// src/services/ansible/roles/fail2ban.cpp

#include <opencattus/services/ansible/role.h>
#include <opencattus/services/ansible/roles/fail2ban.h>
#include <opencattus/services/log.h>
#include <opencattus/services/scriptbuilder.h>
#include <opencattus/utils/string.h>

#ifdef BUILD_TESTING
#include <doctest/doctest.h>
#else
#define DOCTEST_CONFIG_DISABLE
#include <doctest/doctest.h>
#endif

#include <fmt/core.h>
#include <string_view>

namespace opencattus::services::ansible::roles::fail2ban {

ScriptBuilder installScript(
    const Role& role, const opencattus::models::OS& osinfo)
{
    using namespace opencattus;
    ScriptBuilder builder(osinfo);

    LOG_ASSERT(role.roleName() == "fail2ban",
        fmt::format("Expected fail2ban role, found {}", role.roleName()));

    builder.addNewLine()
        .addCommand("# Install fail2ban package")
        .addPackage("fail2ban")
        .addNewLine()
        .addCommand("# Add fail2ban jail.local configuration")
        .addFileTemplate("/etc/fail2ban/jail.local", R"EOF(
[DEFAULT]
bantime  = 10m
findtime  = 10m
maxretry = 5
backend = systemd

[sshd]
enabled = true
)EOF")
        .addNewLine()
        .addCommand("# Enable and start fail2ban service")
        .enableService("fail2ban");

    return builder;
}

} // namespace opencattus::services::ansible::roles::fail2ban
