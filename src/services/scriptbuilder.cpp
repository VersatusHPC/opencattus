#include <filesystem>
#include <utility>
#include <vector>

#include <boost/algorithm/string/join.hpp>
#include <fmt/format.h>

#include <opencattus/models/os.h>
#include <opencattus/utils/formatters.h>

#include <opencattus/services/scriptbuilder.h>

#ifdef BUILD_TESTING
#include <doctest/doctest.h>
#else
#define DOCTEST_CONFIG_DISABLE
#include <doctest/doctest.h>
#endif

namespace opencattus::services {

TEST_SUITE_BEGIN("opencattus::services::ScriptBuilder");

using namespace opencattus::models;

ScriptBuilder::ScriptBuilder(const OS& osinfo)
    : m_os(osinfo)
{
    m_commands.emplace_back("#!/bin/bash -xeu");
};

ScriptBuilder& ScriptBuilder::addNewLine() { return addCommand(""); }

ScriptBuilder& ScriptBuilder::enableService(const std::string_view service)
{
    return addCommand("systemctl enable --now {}", service);
}

ScriptBuilder& ScriptBuilder::disableService(const std::string_view service)
{
    return addCommand("systemctl disable --now {}", service);
};

ScriptBuilder& ScriptBuilder::startService(const std::string_view service)
{
    return addCommand("systemctl start {}", service);
}

ScriptBuilder& ScriptBuilder::stopService(const std::string_view service)
{
    return addCommand("systemctl stop {}", service);
};

ScriptBuilder& ScriptBuilder::addPackage(const std::string_view pkg)
{
    switch (m_os.getPackageType()) {
        case OS::PackageType::RPM:
            return addCommand("dnf install -y {}", pkg);
        case OS::PackageType::DEB:
            return addCommand(
                "DEBIAN_FRONTEND=noninteractive apt install -y {}", pkg);
    }

    std::unreachable();
};

ScriptBuilder& ScriptBuilder::addPackages(const std::set<std::string>& pkgs)
{
    switch (m_os.getPackageType()) {
        case OS::PackageType::RPM:
            return addCommand("dnf install -y {}", fmt::join(pkgs, " "));
        case OS::PackageType::DEB:
            return addCommand(
                "DEBIAN_FRONTEND=noninteractive apt install -y {}",
                fmt::join(pkgs, " "));
    }

    std::unreachable();
}

ScriptBuilder& ScriptBuilder::removePackage(const std::string_view pkg)
{
    switch (m_os.getPackageType()) {
        case OS::PackageType::RPM:
            return addCommand("dnf remove -y {}", pkg);
        case OS::PackageType::DEB:
            return addCommand(
                "DEBIAN_FRONTEND=noninteractive apt remove -y {}", pkg);
    }

    std::unreachable();
}

ScriptBuilder& ScriptBuilder::removeLineWithKeyFromFile(
    const std::filesystem::path& path, const std::string& key)
{
    return addCommand("# Removing line with {} from {}", key, path)
        .addCommand(R"(grep -q "{}" "{}" && sed -i "/{}/d" "{}")", key, path,
            key, path);
}

[[nodiscard]] std::string ScriptBuilder::toString() const
{
    return boost::algorithm::join(m_commands, "\n");
};

[[nodiscard]] const std::vector<std::string>& ScriptBuilder::commands() const
{
    return m_commands;
}

TEST_CASE("Basic")
{
    const OS osinfo
        = opencattus::models::OS(OS::Distro::Rocky, OS::Platform::el9, 5);
    ScriptBuilder builder(osinfo);

    builder.addNewLine()
        .addCommand("# Foo")
        .addCommand("foo")
        .addNewLine()
        .addLineToFile(
            "/etc/hosts", "example.com", "123.123.123.123 example.com", 10)
        .enableService("foo-service");

    const auto script = builder.toString();
    CHECK(script.contains(
        R"_(echo "123.123.123.123 example.com" >> "/etc/hosts")_"));
    CHECK(script.contains("systemctl enable --now foo-service"));
}

TEST_SUITE_END();

}; // namespace opencattus::services
