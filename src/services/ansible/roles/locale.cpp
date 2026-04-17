#include <opencattus/services/ansible/roles/locale.h>
#include <opencattus/services/log.h>
#include <opencattus/services/runner.h>

#ifdef BUILD_TESTING
#include <doctest/doctest.h>
#else
#define DOCTEST_CONFIG_DISABLE
#include <doctest/doctest.h>
#endif

#include <fmt/core.h>
#include <list>
#include <string>
#include <vector>

namespace {
using namespace opencattus::utils::singleton;
void configureLocale()
{
    LOG_INFO("Setting up locale")

    ::runner()->executeCommand(
        fmt::format("localectl set-locale LANG={}", cluster()->getLocale()));
}

#ifdef BUILD_TESTING
class RecordingRunner final : public opencattus::services::IRunner {
private:
    std::vector<std::string> m_commands;

public:
    int executeCommand(const std::string& cmd) override
    {
        m_commands.push_back(cmd);
        return 0;
    }

    int executeCommand(
        const std::string& cmd, std::list<std::string>& /*output*/) override
    {
        m_commands.push_back(cmd);
        return 0;
    }

    opencattus::services::CommandProxy executeCommandIter(
        const std::string& cmd, opencattus::services::Stream /*out*/) override
    {
        m_commands.push_back(cmd);
        return opencattus::services::CommandProxy {};
    }

    void checkCommand(const std::string& cmd) override
    {
        m_commands.push_back(cmd);
    }

    std::vector<std::string> checkOutput(const std::string& cmd) override
    {
        m_commands.push_back(cmd);
        return {};
    }

    int downloadFile(const std::string& url, const std::string& file) override
    {
        m_commands.push_back(fmt::format("download {} {}", url, file));
        return 0;
    }

    int run(const opencattus::services::ScriptBuilder& /*script*/) override
    {
        return 0;
    }

    [[nodiscard]] const std::vector<std::string>& commands() const
    {
        return m_commands;
    }
};

TEST_CASE("locale role sets LANG explicitly")
{
    std::unique_ptr<opencattus::services::IRunner> runner
        = std::make_unique<RecordingRunner>();
    const auto* runnerPtr = static_cast<RecordingRunner*>(runner.get());
    opencattus::Singleton<opencattus::services::IRunner>::init(
        std::move(runner));

    auto cluster = std::make_unique<opencattus::models::Cluster>();
    cluster->setLocale("en_US.utf8");
    opencattus::Singleton<opencattus::models::Cluster>::init(
        std::move(cluster));

    configureLocale();

    REQUIRE_FALSE(runnerPtr->commands().empty());
    CHECK(
        runnerPtr->commands().back() == "localectl set-locale LANG=en_US.utf8");
}
#endif
}

namespace opencattus::services::ansible::roles::locale {

void run(const Role& role) { configureLocale(); }

}
