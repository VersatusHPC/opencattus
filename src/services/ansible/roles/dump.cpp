#include <opencattus/services/ansible/roles/dump.h>
#include <opencattus/services/log.h>
#include <opencattus/utils/singleton.h>

#ifdef BUILD_TESTING
#include <doctest/doctest.h>
#else
#define DOCTEST_CONFIG_DISABLE
#include <doctest/doctest.h>
#endif

#include <fmt/core.h>

namespace {
using namespace opencattus::utils::singleton;

std::string buildFirewallDumpCommand()
{
    return "if command -v firewall-cmd >/dev/null 2>&1 && "
           "systemctl is-active --quiet firewalld.service; then "
           "firewall-cmd --list-all-zones; else "
           "echo 'firewalld inactive or firewall-cmd not installed'; fi";
}

std::string buildRepositoryDumpCommand()
{
    return "if command -v apt-cache >/dev/null 2>&1; then "
           "grep -RHE '^[[:space:]]*(deb|deb-src) ' "
           "/etc/apt/sources.list /etc/apt/sources.list.d 2>/dev/null || true; "
           "elif test -d /etc/yum.repos.d; then "
           "grep -EH '^(mirrorlist|baseurl)' /etc/yum.repos.d/*.repo "
           "2>/dev/null || true; "
           "else echo 'No supported repository configuration found'; fi";
}

std::string buildPackageDumpCommand()
{
    return "if command -v rpm >/dev/null 2>&1; then rpm -qa; "
           "elif command -v dpkg-query >/dev/null 2>&1; then "
           "dpkg-query -W -f='${binary:Package}\\t${Version}\\n'; "
           "else echo 'No supported package database found'; fi";
}

void dumpPreInstallState()
{
    using namespace opencattus::services::runner;
    const auto opts = opencattus::utils::singleton::options();

    LOG_INFO("Dumping cluster state before the installation begins")

    LOG_INFO("OS");
    shell::cmd("cat /etc/os-release");

    LOG_INFO("Repositories URLs");
    shell::cmd(buildRepositoryDumpCommand());

    LOG_INFO("Packages installed");
    shell::cmd(buildPackageDumpCommand());

    LOG_INFO("Network configuration");
    shell::cmd("ip a");
    shell::cmd("ip link");

    LOG_INFO("Kernel version");
    shell::cmd("uname -a");

    LOG_INFO("Memory");
    shell::cmd("free -m");

    LOG_INFO("Services running");
    shell::cmd("systemctl --no-pager list-units --plain --type=service --all");

    LOG_INFO("Firewall configuration");
    shell::cmd(buildFirewallDumpCommand());

    LOG_INFO("End of cluster state dump");
    opts->maybeStopAfterStep("dump-cluster-state");
}
}

namespace opencattus::services::ansible::roles::dump {

void run(const Role& role) { dumpPreInstallState(); }

}

TEST_CASE("buildFirewallDumpCommand only queries firewalld when the service is "
          "active")
{
    const auto command = buildFirewallDumpCommand();

    CHECK(command.contains("command -v firewall-cmd >/dev/null 2>&1 && "
                           "systemctl is-active --quiet firewalld.service"));
    CHECK(command.contains("firewall-cmd --list-all-zones"));
    CHECK(command.contains("firewalld inactive or firewall-cmd not installed"));
}

TEST_CASE("dump commands support RPM and APT systems")
{
    const auto repositories = buildRepositoryDumpCommand();
    const auto packages = buildPackageDumpCommand();

    CHECK(repositories.contains("/etc/apt/sources.list"));
    CHECK(repositories.contains("/etc/yum.repos.d"));
    CHECK(packages.contains("rpm -qa"));
    CHECK(packages.contains("dpkg-query"));
}
