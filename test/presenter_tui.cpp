/*
 * Copyright 2026 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fmt/core.h>
#include <fstream>
#include <list>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include <opencattus/connection.h>
#include <opencattus/models/answerfile.h>
#include <opencattus/models/cluster.h>
#include <opencattus/patterns/singleton.h>
#include <opencattus/presenter/PresenterInfiniband.h>
#include <opencattus/presenter/PresenterInstall.h>
#include <opencattus/presenter/PresenterLocale.h>
#include <opencattus/presenter/PresenterMailSystem.h>
#include <opencattus/presenter/PresenterNetwork.h>
#include <opencattus/presenter/PresenterNodes.h>
#include <opencattus/presenter/PresenterNodesOperationalSystem.h>
#include <opencattus/presenter/PresenterProvisioner.h>
#include <opencattus/presenter/PresenterQueueSystem.h>
#include <opencattus/presenter/PresenterRepository.h>
#include <opencattus/presenter/PresenterTime.h>
#include <opencattus/services/options.h>
#include <opencattus/services/runner.h>
#include <opencattus/view/view.h>

namespace {

using opencattus::models::AnswerFile;
using opencattus::models::Cluster;
using opencattus::models::OS;
using opencattus::models::QueueSystem;
using opencattus::presenter::NetworkCreator;
using opencattus::presenter::NetworkCreatorData;
using opencattus::presenter::PresenterInfiniband;
using opencattus::presenter::PresenterInstall;
using opencattus::presenter::PresenterLocale;
using opencattus::presenter::PresenterMailSystem;
using opencattus::presenter::PresenterNetwork;
using opencattus::presenter::PresenterNodes;
using opencattus::presenter::PresenterNodesOperationalSystem;
using opencattus::presenter::PresenterProvisioner;
using opencattus::presenter::PresenterQueueSystem;
using opencattus::presenter::PresenterRepository;
using opencattus::presenter::PresenterTime;
using opencattus::services::CommandProxy;
using opencattus::services::IRunner;
using opencattus::services::Options;
using opencattus::services::Postfix;
using PBS = opencattus::models::PBS;

auto tempPath(std::string_view stem, std::string_view extension)
    -> std::filesystem::path
{
    return std::filesystem::temp_directory_path()
        / fmt::format("{}.{}", stem, extension);
}

class ScriptedRunner final : public IRunner {
public:
    using Outputs = std::unordered_map<std::string, std::vector<std::string>>;

private:
    Outputs m_outputs;
    std::vector<std::string> m_commands;

public:
    explicit ScriptedRunner(Outputs outputs)
        : m_outputs(std::move(outputs))
    {
    }

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

    CommandProxy executeCommandIter(
        const std::string& cmd, opencattus::services::Stream /*out*/) override
    {
        m_commands.push_back(cmd);
        return CommandProxy {};
    }

    void checkCommand(const std::string& cmd) override
    {
        m_commands.push_back(cmd);
    }

    std::vector<std::string> checkOutput(const std::string& cmd) override
    {
        m_commands.push_back(cmd);
        const auto it = m_outputs.find(cmd);
        if (it == m_outputs.end()) {
            throw std::runtime_error(
                fmt::format("Unexpected checkOutput command: {}", cmd));
        }

        return it->second;
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

struct YesNoReply {
    bool value;
};

struct ListReply {
    std::string value;
};

struct FieldReply {
    std::vector<std::string> values;
};

struct CollectListReply {
    std::vector<std::string> values;
};

struct MultiSelectionReply {
    int status;
    std::vector<std::string> values;
};

struct BackReply { };

struct AbortReply { };

struct MenuSnapshot {
    std::string title;
    std::string message;
    std::vector<std::string> items;
};

struct FieldSnapshot {
    std::string title;
    std::string message;
    View::FieldEntries items;
};

struct TextSnapshot {
    std::string title;
    std::string message;
    std::string text;
};

using Response = std::variant<YesNoReply, ListReply, FieldReply,
    CollectListReply, MultiSelectionReply, BackReply, AbortReply>;

auto responseKind(const Response& response) -> std::string_view
{
    return std::visit(
        [](const auto& value) -> std::string_view {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, YesNoReply>) {
                return "yesNoQuestion";
            } else if constexpr (std::is_same_v<T, ListReply>) {
                return "listMenu";
            } else if constexpr (std::is_same_v<T, FieldReply>) {
                return "fieldMenu";
            } else if constexpr (std::is_same_v<T, CollectListReply>) {
                return "collectListMenu";
            } else if constexpr (std::is_same_v<T, MultiSelectionReply>) {
                return "checkboxSelectionMenu";
            } else if constexpr (std::is_same_v<T, BackReply>) {
                return "back";
            } else if constexpr (std::is_same_v<T, AbortReply>) {
                return "abort";
            } else {
                return "unknown";
            }
        },
        response);
}

struct ScriptedViewState {
    std::deque<Response> responses;
    std::vector<std::string> messages;
    std::vector<MenuSnapshot> listMenus;
    std::vector<MenuSnapshot> multiSelectionMenus;
    std::vector<FieldSnapshot> fieldMenus;
    std::vector<TextSnapshot> scrollableMessages;
    bool allowProgressMenu = false;
};

class ScriptedView final : public View {
private:
    std::shared_ptr<ScriptedViewState> m_state;

    template <typename T> auto pop(std::string_view where) -> T
    {
        if (m_state->responses.empty()) {
            throw std::runtime_error(
                fmt::format("No scripted response available for {}", where));
        }

        if (std::holds_alternative<AbortReply>(m_state->responses.front())) {
            m_state->responses.pop_front();
            throw ViewAbortRequested("scripted abort");
        }
        if (std::holds_alternative<BackReply>(m_state->responses.front())) {
            m_state->responses.pop_front();
            throw ViewBackRequested("scripted back");
        }

        if (const auto value = std::get_if<T>(&m_state->responses.front())) {
            auto out = *value;
            m_state->responses.pop_front();
            return out;
        }

        throw std::runtime_error(fmt::format(
            "Unexpected scripted response type for {}: got {} after "
            "prompt '{}'",
            where, responseKind(m_state->responses.front()),
            m_state->messages.empty() ? "" : m_state->messages.back()));
    }

    void recordMessage(const char* title, const char* message)
    {
        m_state->messages.emplace_back(fmt::format("{}|{}",
            title == nullptr ? "" : title, message == nullptr ? "" : message));
    }

public:
    explicit ScriptedView(std::shared_ptr<ScriptedViewState> state)
        : m_state(std::move(state))
    {
    }

    void abort() override
    {
        throw ViewAbortRequested("abort called on scripted view");
    }

    void helpMessage(const char* message) override
    {
        recordMessage("help", message);
    }

    void message(const char* message) override
    {
        recordMessage(nullptr, message);
    }

    void message(const char* title, const char* message) override
    {
        recordMessage(title, message);
    }

    void fatalMessage(const char* title, const char* message) override
    {
        recordMessage(title, message);
        throw std::runtime_error(
            fmt::format("fatalMessage invoked: {}", message));
    }

    void okCancelMessage(const char* message) override
    {
        recordMessage("okcancel", message);
    }

    void okCancelMessage(const char* title, const char* message) override
    {
        recordMessage(title, message);
    }

    void okCancelMessagePairs(const char* title, const char* message,
        const FieldEntries& pairs) override
    {
        recordMessage(title, message);
        for (const auto& [key, value] : pairs) {
            recordMessage(key.c_str(), value.c_str());
        }
    }

    void scrollableMessage(const char* title, const char* message,
        const char* text, const char* /*helpMessage*/) override
    {
        recordMessage(title, message);
        m_state->scrollableMessages.emplace_back(TextSnapshot {
            .title = title == nullptr ? "" : title,
            .message = message == nullptr ? "" : message,
            .text = text == nullptr ? "" : text,
        });
    }

    std::pair<int, std::vector<std::string>> checkboxSelectionMenu(
        const char* title, const char* message, const char* /*help*/,
        MultipleSelectionEntries items) override
    {
        recordMessage(title, message);
        m_state->multiSelectionMenus.emplace_back(MenuSnapshot {
            .title = title == nullptr ? "" : title,
            .message = message == nullptr ? "" : message,
            .items =
                [&items] {
                    std::vector<std::string> values;
                    values.reserve(items.size());
                    for (const auto& item : items) {
                        values.emplace_back(std::get<0>(item));
                    }
                    return values;
                }(),
        });
        const auto reply = pop<MultiSelectionReply>("checkboxSelectionMenu");
        for (const auto& value : reply.values) {
            const auto selected = std::ranges::find_if(items,
                [&](const auto& item) { return std::get<0>(item) == value; });
            if (selected == items.end()) {
                throw std::runtime_error(fmt::format(
                    "Scripted multiple selection '{}' not present in menu",
                    value));
            }
        }
        return { reply.status, reply.values };
    }

    std::string listMenuImpl(const char* title, const char* message,
        const std::vector<std::string>& items,
        const char* /*helpMessage*/) override
    {
        recordMessage(title, message);
        m_state->listMenus.emplace_back(MenuSnapshot {
            .title = title == nullptr ? "" : title,
            .message = message == nullptr ? "" : message,
            .items = items,
        });
        const auto reply = pop<ListReply>("listMenu");
        if (std::find(items.begin(), items.end(), reply.value) == items.end()) {
            throw std::runtime_error(
                fmt::format("Scripted list selection '{}' not present in menu",
                    reply.value));
        }

        return reply.value;
    }

    std::vector<std::string> collectListMenuImpl(const char* title,
        const char* message, const std::vector<std::string>& /*items*/,
        const char* /*helpMessage*/,
        ListButtonCallback /*addCallback*/) override
    {
        recordMessage(title, message);
        return pop<CollectListReply>("collectListMenu").values;
    }

    FieldEntries fieldMenuImpl(const char* title, const char* message,
        const FieldEntries& items, const char* /*helpMessage*/) override
    {
        recordMessage(title, message);
        m_state->fieldMenus.emplace_back(FieldSnapshot {
            .title = title == nullptr ? "" : title,
            .message = message == nullptr ? "" : message,
            .items = items,
        });
        const auto reply = pop<FieldReply>("fieldMenu");
        if (reply.values.size() != items.size()) {
            throw std::runtime_error(
                fmt::format("Expected {} field values, got {}", items.size(),
                    reply.values.size()));
        }

        FieldEntries out = items;
        for (std::size_t i = 0; i < out.size(); ++i) {
            out[i].second = reply.values[i];
        }

        return out;
    }

    bool progressMenu(const char* title, const char* message,
        CommandProxy&& /*command*/, ProgressCallback /*fPercent*/) override
    {
        recordMessage(title, message);
        if (!m_state->allowProgressMenu) {
            throw std::runtime_error(
                "Unexpected progressMenu call in scripted view");
        }

        return true;
    }

    bool yesNoQuestion(const char* title, const char* message,
        const char* /*helpMessage*/) override
    {
        recordMessage(title, message);
        return pop<YesNoReply>("yesNoQuestion").value;
    }
};

auto yesNo(bool value) -> Response { return YesNoReply { value }; }

auto select(std::string value) -> Response
{
    return ListReply { std::move(value) };
}

auto fields(std::initializer_list<std::string> values) -> Response
{
    return FieldReply { std::vector<std::string>(values) };
}

auto collect(std::initializer_list<std::string> values) -> Response
{
    return CollectListReply { std::vector<std::string>(values) };
}

auto multi(int status, std::initializer_list<std::string> values) -> Response
{
    return MultiSelectionReply { status, std::vector<std::string>(values) };
}

auto abortSelection() -> Response { return AbortReply {}; }

auto backSelection() -> Response { return BackReply {}; }

auto isUsableQuestionnaireInterface(const std::string& interface) -> bool
{
    if (interface == "lo" || interface.starts_with("ib")) {
        return false;
    }

    try {
        static_cast<void>(Connection::fetchAddress(interface));
        static_cast<void>(Network::fetchSubnetMask(interface));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

auto usableHostInterfaces() -> std::vector<std::string>
{
    std::vector<std::string> usable;
    for (const auto& interface : Connection::fetchInterfaces()) {
        if (isUsableQuestionnaireInterface(interface)) {
            usable.push_back(interface);
        }
    }

    return usable;
}

auto usableInfinibandInterfaces() -> std::vector<std::string>
{
    std::vector<std::string> usable;
    for (const auto& interface : Connection::fetchInterfaces()) {
        if (!interface.starts_with("ib")) {
            continue;
        }

        try {
            static_cast<void>(Connection::fetchAddress(interface));
            static_cast<void>(Network::fetchSubnetMask(interface));
            usable.push_back(interface);
        } catch (const std::exception&) {
        }
    }

    return usable;
}

auto hasUsableInfinibandInterface() -> bool
{
    return !usableInfinibandInterfaces().empty();
}

constexpr std::string_view zone1970TabCommand
    = R"(bash -c "test -r /usr/share/zoneinfo/zone1970.tab && cat /usr/share/zoneinfo/zone1970.tab || true")";
constexpr std::string_view zone1970TabEnv = "OPENCATTUS_ZONE1970_TAB";
constexpr std::string_view localeMetadataCommand = "locale -av";
constexpr std::string_view advancedLocaleChoice = "Advanced / legacy locales";

void configureZone1970Fixture(const ScriptedRunner::Outputs& outputs)
{
    const auto path = std::filesystem::temp_directory_path()
        / "opencattus-test-zone1970.tab";

    std::ofstream file(path, std::ios::trunc);
    const auto output = outputs.find(std::string(zone1970TabCommand));
    if (output != outputs.end()) {
        for (const auto& line : output->second) {
            file << line << '\n';
        }
    }
    file.close();

    setenv(zone1970TabEnv.data(), path.c_str(), 1);
}

void initializePresenterTestEnvironment(
    ScriptedRunner::Outputs outputs = ScriptedRunner::Outputs {},
    bool dryRun = false)
{
    configureZone1970Fixture(outputs);

    opencattus::Singleton<const Options>::init(
        std::make_unique<const Options>(Options {
            .dryRun = dryRun,
            .enableTUI = true,
        }));
    std::unique_ptr<IRunner> runner
        = std::make_unique<ScriptedRunner>(std::move(outputs));
    opencattus::Singleton<IRunner>::init(std::move(runner));
}

auto defaultRunnerOutputs() -> ScriptedRunner::Outputs
{
    return {
        { std::string(zone1970TabCommand),
            {
                "# country\tcoordinates\tTZ\tcomments",
                "BR\t-2332-04637\tAmerica/Sao_Paulo\tBrazil southeast",
                "FR\t+4852+00220\tEurope/Paris",
            } },
        { "timedatectl list-timezones --no-pager",
            { "America/Sao_Paulo", "Brazil/East", "Europe/Paris" } },
        { "locale -a", { "en_US.utf8", "pt_BR.utf8" } },
        { std::string(localeMetadataCommand),
            {
                "locale: en_US.utf8      directory: /usr/lib/locale/en_US.utf8",
                " language | American English",
                "territory | United States",
                "locale: pt_BR.utf8      directory: /usr/lib/locale/pt_BR.utf8",
                " language | Brazilian Portuguese",
                "territory | Brazil",
            } },
    };
}

auto createTestIsoDirectory(std::string_view stem) -> std::filesystem::path
{
    const auto dir
        = std::filesystem::temp_directory_path() / fmt::format("{}-isos", stem);
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "Rocky-9.6-x86_64-dvd.iso").close();
    return dir;
}

auto createEmptyIsoDirectory(std::string_view stem) -> std::filesystem::path
{
    const auto dir
        = std::filesystem::temp_directory_path() / fmt::format("{}-isos", stem);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

auto messageIndices(const std::vector<std::string>& messages,
    std::string_view prefix) -> std::vector<std::size_t>
{
    std::vector<std::size_t> indices;
    for (std::size_t i = 0; i < messages.size(); ++i) {
        if (messages[i].starts_with(prefix)) {
            indices.push_back(i);
        }
    }

    return indices;
}

auto firstMessageIndex(const std::vector<std::string>& messages,
    std::string_view prefix) -> std::size_t
{
    const auto indices = messageIndices(messages, prefix);
    if (indices.empty()) {
        throw std::runtime_error(
            fmt::format("Did not find prompt starting with '{}'", prefix));
    }

    return indices.front();
}

auto firstMenuByMessage(const std::vector<MenuSnapshot>& menus,
    std::string_view prefix) -> const MenuSnapshot&
{
    const auto it = std::ranges::find_if(menus, [prefix](const auto& menu) {
        return menu.message.starts_with(prefix);
    });
    if (it == menus.end()) {
        throw std::runtime_error(
            fmt::format("Did not find menu starting with '{}'", prefix));
    }

    return *it;
}

auto firstFieldMenuByMessage(const std::vector<FieldSnapshot>& menus,
    std::string_view prefix) -> const FieldSnapshot&
{
    const auto it = std::ranges::find_if(menus, [prefix](const auto& menu) {
        return menu.message.starts_with(prefix);
    });
    if (it == menus.end()) {
        throw std::runtime_error(
            fmt::format("Did not find field menu starting with '{}'", prefix));
    }

    return *it;
}

auto firstScrollableMessageByMessage(const std::vector<TextSnapshot>& messages,
    std::string_view prefix) -> const TextSnapshot&
{
    const auto it
        = std::ranges::find_if(messages, [prefix](const auto& message) {
              return message.message.starts_with(prefix);
          });
    if (it == messages.end()) {
        throw std::runtime_error(fmt::format(
            "Did not find scrollable message starting with '{}'", prefix));
    }

    return *it;
}

void seedClusterMetadata(Cluster& cluster)
{
    cluster.setName("demo");
    cluster.setCompanyName("acme");
    cluster.setAdminMail("admin@example.com");
    cluster.getHeadnode().setHostname(std::string_view { "headnode" });
    cluster.setDomainName("cluster.example.com");
    cluster.setTimezone("America/Sao_Paulo");
    cluster.setLocale("en_US.utf8");
}

void addHeadnodeNetwork(Cluster& cluster, Network::Profile profile,
    std::string_view interface, std::string_view networkAddress,
    std::string_view connectionAddress, std::string_view subnetMask,
    std::string_view domainName,
    const std::vector<std::string>& nameservers = {},
    std::optional<std::string_view> gateway = std::nullopt)
{
    auto network = std::make_unique<Network>(profile, Network::Type::Ethernet);
    network->setSubnetMask(std::string(subnetMask));
    network->setAddress(std::string(networkAddress));
    if (gateway.has_value()) {
        network->setGateway(std::string(gateway.value()));
    }
    network->setDomainName(std::string(domainName));
    if (!nameservers.empty()) {
        network->setNameservers(nameservers);
    }

    Connection connection(network.get());
    connection.setInterface(interface);
    connection.setAddress(std::string(connectionAddress));

    cluster.getHeadnode().addConnection(std::move(connection));
    cluster.addNetwork(std::move(network));
}

} // namespace

TEST_SUITE("opencattus::presenter::tui")
{
    TEST_CASE("mail questionnaire stores postfix SASL settings on the model")
    {
        initializePresenterTestEnvironment(defaultRunnerOutputs());

        auto model = std::make_unique<Cluster>();
        seedClusterMetadata(*model);

        auto state = std::make_shared<ScriptedViewState>();
        state->responses = {
            yesNo(true),
            select("SASL"),
            fields({ "mail.cluster.example.com" }),
            yesNo(true),
            fields({ "/etc/pki/tls/certs/cluster.example.com.cer",
                "/etc/pki/tls/private/cluster.example.com.key" }),
            fields(
                { "smtp.example.com", "587", "relayUser", "examplePassword" }),
        };

        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);
        PresenterMailSystem(model, view);

        REQUIRE(model->getMailSystem().has_value());
        const auto& mailSystem = model->getMailSystem().value();
        CHECK(mailSystem.getProfile() == Postfix::Profile::SASL);
        CHECK(mailSystem.getHostname().value() == "headnode");
        CHECK(mailSystem.getDomain().value() == "cluster.example.com");
        REQUIRE(mailSystem.getDestination().has_value());
        CHECK(mailSystem.getDestination().value()
            == std::vector<std::string> { "mail.cluster.example.com" });
        CHECK(mailSystem.getSMTPServer().value() == "smtp.example.com");
        CHECK(mailSystem.getPort().value() == 587);
        CHECK(mailSystem.getUsername().value() == "relayUser");
        CHECK(mailSystem.getPassword().value() == "examplePassword");
        CHECK(mailSystem.getCertFile().value()
            == std::filesystem::path(
                "/etc/pki/tls/certs/cluster.example.com.cer"));
        CHECK(mailSystem.getKeyFile().value()
            == std::filesystem::path(
                "/etc/pki/tls/private/cluster.example.com.key"));
    }

    TEST_CASE("mail questionnaire keeps TLS certificate overrides optional")
    {
        initializePresenterTestEnvironment(defaultRunnerOutputs());

        auto model = std::make_unique<Cluster>();
        seedClusterMetadata(*model);

        auto state = std::make_shared<ScriptedViewState>();
        state->responses = {
            yesNo(true),
            select("Local"),
            fields({ "" }),
            yesNo(false),
        };

        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);
        PresenterMailSystem(model, view);

        REQUIRE(model->getMailSystem().has_value());
        const auto& mailSystem = model->getMailSystem().value();
        CHECK(mailSystem.getProfile() == Postfix::Profile::Local);
        CHECK_FALSE(mailSystem.getDestination().has_value());
        CHECK_FALSE(mailSystem.getCertFile().has_value());
        CHECK_FALSE(mailSystem.getKeyFile().has_value());
    }

    TEST_CASE(
        "time questionnaire fails cleanly when no timezones are available")
    {
        initializePresenterTestEnvironment({
            { std::string(zone1970TabCommand), {} },
            { "timedatectl list-timezones --no-pager", {} },
            { "locale -a", { "en_US.utf8" } },
        });

        auto model = std::make_unique<Cluster>();
        auto state = std::make_shared<ScriptedViewState>();
        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);

        CHECK_THROWS_WITH_AS(PresenterTime(model, view),
            doctest::Contains("No timezones were discovered on this system"),
            std::runtime_error);
    }

    TEST_CASE(
        "locale questionnaire fails cleanly when no locales are available")
    {
        initializePresenterTestEnvironment({
            { std::string(zone1970TabCommand),
                { "BR\t-2332-04637\tAmerica/Sao_Paulo\tBrazil southeast",
                    "FR\t+4852+00220\tEurope/Paris" } },
            { "timedatectl list-timezones --no-pager",
                { "America/Sao_Paulo", "Europe/Paris" } },
            { "locale -a", {} },
        });

        auto model = std::make_unique<Cluster>();
        auto state = std::make_shared<ScriptedViewState>();
        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);

        CHECK_THROWS_WITH_AS(PresenterLocale(model, view),
            doctest::Contains("No locales were discovered on this system"),
            std::runtime_error);
    }

    TEST_CASE("time questionnaire prefers canonical zone1970 timezones")
    {
        initializePresenterTestEnvironment({
            { std::string(zone1970TabCommand),
                {
                    "# country\tcoordinates\tTZ\tcomments",
                    "BR\t-2332-04637\tAmerica/Sao_Paulo\tBrazil southeast",
                    "FR\t+4852+00220\tEurope/Paris",
                } },
            { "timedatectl list-timezones --no-pager",
                { "America/Sao_Paulo", "Brazil/East", "Europe/Paris" } },
            { "locale -a", { "en_US.utf8" } },
        });

        auto model = std::make_unique<Cluster>();
        auto state = std::make_shared<ScriptedViewState>();
        state->responses = {
            select("America"),
            select("Sao_Paulo"),
            collect({ "0.pool.ntp.org" }),
        };
        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);

        PresenterTime(model, view);

        REQUIRE(state->listMenus.size() >= 2);
        CHECK(std::ranges::find(state->listMenus[0].items, "America")
            != state->listMenus[0].items.end());
        CHECK(std::ranges::find(state->listMenus[0].items, "Brazil")
            == state->listMenus[0].items.end());
        CHECK(model->getTimezone().getTimezone() == "America/Sao_Paulo");
    }

    TEST_CASE("time questionnaire falls back to timedatectl timezones")
    {
        initializePresenterTestEnvironment({
            { std::string(zone1970TabCommand), {} },
            { "timedatectl list-timezones --no-pager",
                { "UTC", "America/Sao_Paulo" } },
            { "locale -a", { "en_US.utf8" } },
        });

        auto model = std::make_unique<Cluster>();
        auto state = std::make_shared<ScriptedViewState>();
        state->responses = {
            select("UTC"),
            collect({ "0.pool.ntp.org" }),
        };
        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);

        PresenterTime(model, view);

        CHECK(model->getTimezone().getTimezone() == "UTC");
    }

    TEST_CASE(
        "time questionnaire drills into nested timezone paths alphabetically")
    {
        initializePresenterTestEnvironment({
            { std::string(zone1970TabCommand),
                {
                    "AR\t-3124-06411\tAmerica/Argentina/Cordoba",
                    "BR\t-1259-03831\tAmerica/Bahia",
                    "BR\t-2332-04637\tAmerica/Sao_Paulo",
                    "AR\t-3436-05827\tAmerica/Argentina/Buenos_Aires",
                } },
            { "timedatectl list-timezones --no-pager",
                { "America/Sao_Paulo" } },
            { "locale -a", { "en_US.utf8" } },
        });

        auto model = std::make_unique<Cluster>();
        auto state = std::make_shared<ScriptedViewState>();
        state->responses = {
            select("America"),
            select("Argentina"),
            select("Buenos_Aires"),
            collect({ "0.pool.ntp.org" }),
        };
        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);

        PresenterTime(model, view);

        REQUIRE(state->listMenus.size() >= 3);
        CHECK(state->listMenus[1].items
            == std::vector<std::string> { "Argentina", "Bahia", "Sao_Paulo" });
        CHECK(state->listMenus[2].items
            == std::vector<std::string> { "Buenos_Aires", "Cordoba" });
        CHECK(model->getTimezone().getTimezone()
            == "America/Argentina/Buenos_Aires");
    }

    TEST_CASE("locale questionnaire groups UTF-8 locales by language")
    {
        initializePresenterTestEnvironment({
            { std::string(zone1970TabCommand),
                { "BR\t-2332-04637\tAmerica/Sao_Paulo\tBrazil southeast" } },
            { "timedatectl list-timezones --no-pager",
                { "America/Sao_Paulo" } },
            { "locale -a",
                { "C", "C.utf8", "en_US", "en_US.iso885915", "en_US.utf8",
                    "pt_BR.utf8", "POSIX" } },
            { std::string(localeMetadataCommand),
                {
                    "locale: en_US.utf8      directory: "
                    "/usr/lib/locale/en_US.utf8",
                    " language | American English",
                    "territory | United States",
                    "locale: pt_BR.utf8      directory: "
                    "/usr/lib/locale/pt_BR.utf8",
                    " language | Brazilian Portuguese",
                    "territory | Brazil",
                } },
        });

        auto model = std::make_unique<Cluster>();
        auto state = std::make_shared<ScriptedViewState>();
        state->responses = { select("English (en)") };
        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);

        PresenterLocale(model, view);

        const auto& menu = firstMenuByMessage(
            state->listMenus, "Choose the default locale language");
        CHECK(
            std::ranges::find(menu.items, "English (en)") != menu.items.end());
        CHECK(std::ranges::find(menu.items, "Portuguese (pt)")
            != menu.items.end());
        CHECK(std::ranges::find(menu.items, std::string(advancedLocaleChoice))
            != menu.items.end());
        CHECK(std::ranges::find(menu.items, "C/POSIX") == menu.items.end());
        CHECK(std::ranges::find(menu.items, "en_US.iso885915")
            == menu.items.end());
        CHECK(model->getLocale() == "en_US.utf8");
    }

    TEST_CASE("locale questionnaire asks region when a language has choices")
    {
        initializePresenterTestEnvironment({
            { std::string(zone1970TabCommand),
                { "BR\t-2332-04637\tAmerica/Sao_Paulo\tBrazil southeast" } },
            { "timedatectl list-timezones --no-pager",
                { "America/Sao_Paulo" } },
            { "locale -a", { "en_GB.utf8", "en_US.utf8", "pt_BR.utf8" } },
            { std::string(localeMetadataCommand),
                {
                    "locale: en_GB.utf8      directory: "
                    "/usr/lib/locale/en_GB.utf8",
                    " language | British English",
                    "territory | United Kingdom",
                    "locale: en_US.utf8      directory: "
                    "/usr/lib/locale/en_US.utf8",
                    " language | American English",
                    "territory | United States",
                    "locale: pt_BR.utf8      directory: "
                    "/usr/lib/locale/pt_BR.utf8",
                    " language | Brazilian Portuguese",
                    "territory | Brazil",
                } },
        });

        auto model = std::make_unique<Cluster>();
        auto state = std::make_shared<ScriptedViewState>();
        state->responses = {
            select("English (en)"),
            select("en_GB.UTF-8"),
        };
        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);

        PresenterLocale(model, view);

        const auto& menu = firstMenuByMessage(
            state->listMenus, "Choose the regional UTF-8 locale");
        CHECK(std::ranges::find(menu.items, "en_GB.UTF-8") != menu.items.end());
        CHECK(model->getLocale() == "en_GB.utf8");
    }

    TEST_CASE("locale questionnaire keeps legacy locales behind advanced")
    {
        initializePresenterTestEnvironment({
            { std::string(zone1970TabCommand),
                { "BR\t-2332-04637\tAmerica/Sao_Paulo\tBrazil southeast" } },
            { "timedatectl list-timezones --no-pager",
                { "America/Sao_Paulo" } },
            { "locale -a", { "en_US", "en_US.iso885915", "en_US.utf8" } },
            { std::string(localeMetadataCommand),
                {
                    "locale: en_US.utf8      directory: "
                    "/usr/lib/locale/en_US.utf8",
                    " language | American English",
                    "territory | United States",
                } },
        });

        auto model = std::make_unique<Cluster>();
        auto state = std::make_shared<ScriptedViewState>();
        state->responses = {
            select(std::string(advancedLocaleChoice)),
            select("en_US.iso885915"),
        };
        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);

        PresenterLocale(model, view);

        const auto& menu = firstMenuByMessage(
            state->listMenus, "Choose an advanced or legacy locale");
        CHECK(std::ranges::find(menu.items, "en_US.iso885915")
            != menu.items.end());
        CHECK(std::ranges::find(menu.items, "en_US.utf8") == menu.items.end());
        CHECK(model->getLocale() == "en_US.iso885915");
    }

    TEST_CASE("locale questionnaire uses language metadata for unknown codes")
    {
        initializePresenterTestEnvironment({
            { std::string(zone1970TabCommand),
                { "BR\t-2332-04637\tAmerica/Sao_Paulo\tBrazil southeast" } },
            { "timedatectl list-timezones --no-pager",
                { "America/Sao_Paulo" } },
            { "locale -a", { "uk_UA", "uk_UA.utf8" } },
            { std::string(localeMetadataCommand),
                {
                    "locale: uk_UA.utf8      directory: "
                    "/usr/lib/locale/uk_UA.utf8",
                    " language | Ukrainian",
                    "territory | Ukraine",
                } },
        });

        auto model = std::make_unique<Cluster>();
        auto state = std::make_shared<ScriptedViewState>();
        state->responses = { select("Ukrainian (uk)") };
        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);

        PresenterLocale(model, view);

        const auto& menu = firstMenuByMessage(
            state->listMenus, "Choose the default locale language");
        CHECK(std::ranges::find(menu.items, "Ukrainian (uk)")
            != menu.items.end());
        CHECK(std::ranges::find(menu.items, "uk locales") == menu.items.end());
        CHECK(model->getLocale() == "uk_UA.utf8");
    }

    TEST_CASE("dry-run ISO download skips the progress UI and keeps the "
              "planned image path")
    {
        initializePresenterTestEnvironment(defaultRunnerOutputs(), true);

        auto model = std::make_unique<Cluster>();
        model->getHeadnode().setOS(
            OS(OS::Distro::RHEL, OS::Platform::el9, 7, OS::Arch::x86_64));
        auto state = std::make_shared<ScriptedViewState>();
        state->responses = {
            yesNo(true),
            select("Rocky Linux"),
            fields({ "9.7", "x86_64" }),
        };

        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);
        PresenterNodesOperationalSystem(model, view);

        CHECK(state->responses.empty());
        const auto& versionFields = firstFieldMenuByMessage(state->fieldMenus,
            "Enter the distribution version and architecture");
        CHECK(versionFields.items[0].second == "9.7");
        CHECK(versionFields.items[1].second == "x86_64");
        CHECK(model->getDiskImage().getPath()
            == std::filesystem::path("/root/Rocky-9.7-x86_64-dvd.iso"));
        REQUIRE(model->getPendingDiskImageDownloadURL().has_value());
        CHECK(model->getPendingDiskImageDownloadURL().value()
            == "https://download.rockylinux.org/pub/rocky/9/isos/x86_64/"
               "Rocky-9.7-x86_64-dvd.iso");
        CHECK(model->getComputeNodeOS().getVersion() == "9.7");
    }

    TEST_CASE("ISO download choice is scheduled until preflight confirms")
    {
        initializePresenterTestEnvironment(defaultRunnerOutputs());

        auto model = std::make_unique<Cluster>();
        auto state = std::make_shared<ScriptedViewState>();
        state->responses = {
            yesNo(true),
            select("Rocky Linux"),
            fields({ "9.6", "x86_64" }),
        };

        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);
        PresenterNodesOperationalSystem(model, view);

        CHECK(state->responses.empty());
        CHECK(model->getDiskImage().getPath()
            == std::filesystem::path("/root/Rocky-9.6-x86_64-dvd.iso"));
        REQUIRE(model->getPendingDiskImageDownloadURL().has_value());
        CHECK(model->getPendingDiskImageDownloadURL().value()
            == "https://download.rockylinux.org/pub/rocky/9/isos/x86_64/"
               "Rocky-9.6-x86_64-dvd.iso");
    }

    TEST_CASE("RHEL download choice retries instead of aborting the TUI")
    {
        initializePresenterTestEnvironment(defaultRunnerOutputs(), true);

        auto model = std::make_unique<Cluster>();
        auto state = std::make_shared<ScriptedViewState>();
        state->responses = {
            yesNo(true),
            select("Red Hat Enterprise Linux"),
            select("Rocky Linux"),
            fields({ "9.6", "x86_64" }),
        };

        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);
        PresenterNodesOperationalSystem(model, view);

        CHECK(state->responses.empty());
        CHECK(model->getComputeNodeOS().getDistro() == OS::Distro::Rocky);
        CHECK(std::ranges::any_of(state->messages, [](const auto& message) {
            return message
                == "Compute node OS settings|Unfortunately, we do "
                   "not support downloading Red Hat Enterprise Linux yet.\n"
                   "Please download the ISO yourself and put in an "
                   "appropriate location.";
        }));
    }

    TEST_CASE("existing ISO path retries when the path is not a readable "
              "directory")
    {
        initializePresenterTestEnvironment(defaultRunnerOutputs(), true);

        const auto invalidPath = tempPath("opencattus-tui-iso-file", "txt");
        std::ofstream(invalidPath).close();
        const auto isoDir = createTestIsoDirectory("opencattus-tui-iso-retry");

        auto model = std::make_unique<Cluster>();
        auto state = std::make_shared<ScriptedViewState>();
        state->responses = {
            yesNo(false),
            fields({ invalidPath.string() }),
            fields({ isoDir.string() }),
            select("Rocky Linux"),
            select("Rocky-9.6-x86_64-dvd.iso"),
            fields({ "9.6", "x86_64" }),
        };

        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);
        PresenterNodesOperationalSystem(model, view);

        CHECK(state->responses.empty());
        CHECK(model->getDiskImage().getPath()
            == isoDir / "Rocky-9.6-x86_64-dvd.iso");
        CHECK(std::ranges::any_of(state->messages, [](const auto& message) {
            return message
                == "Compute node OS settings|The specified path is "
                   "not a readable directory";
        }));

        std::filesystem::remove(invalidPath);
        std::filesystem::remove_all(isoDir);
    }

    TEST_CASE(
        "existing ISO path explains unmatched distro and retries directory")
    {
        initializePresenterTestEnvironment(defaultRunnerOutputs(), true);

        const auto emptyDir
            = createEmptyIsoDirectory("opencattus-tui-empty-iso-retry");
        const auto isoDir
            = createTestIsoDirectory("opencattus-tui-iso-after-empty-retry");

        auto model = std::make_unique<Cluster>();
        model->getHeadnode().setOS(
            OS(OS::Distro::RHEL, OS::Platform::el9, 6, OS::Arch::x86_64));
        auto state = std::make_shared<ScriptedViewState>();
        state->responses = {
            yesNo(false),
            fields({ emptyDir.string() }),
            select("Rocky Linux"),
            yesNo(false),
            fields({ isoDir.string() }),
            select("Rocky Linux"),
            select("Rocky-9.6-x86_64-dvd.iso"),
            fields({ "9.6", "x86_64" }),
        };

        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);
        PresenterNodesOperationalSystem(model, view);

        CHECK(state->responses.empty());
        CHECK(model->getDiskImage().getPath()
            == isoDir / "Rocky-9.6-x86_64-dvd.iso");
        CHECK(std::ranges::any_of(state->messages, [](const auto& message) {
            return message.contains("Looked for: *.iso filenames containing "
                                    "\"Rocky\"")
                && message.contains("Example: Rocky-9.6-x86_64-dvd.iso");
        }));

        std::filesystem::remove_all(emptyDir);
        std::filesystem::remove_all(isoDir);
    }

    TEST_CASE("existing ISO path can switch to downloading after no match")
    {
        initializePresenterTestEnvironment(defaultRunnerOutputs(), true);

        const auto emptyDir
            = createEmptyIsoDirectory("opencattus-tui-empty-iso-download");

        auto model = std::make_unique<Cluster>();
        auto state = std::make_shared<ScriptedViewState>();
        state->responses = {
            yesNo(false),
            fields({ emptyDir.string() }),
            select("Rocky Linux"),
            yesNo(true),
            fields({ "9.6", "x86_64" }),
        };

        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);
        PresenterNodesOperationalSystem(model, view);

        CHECK(state->responses.empty());
        CHECK(model->getDiskImage().getPath()
            == std::filesystem::path("/root/Rocky-9.6-x86_64-dvd.iso"));
        REQUIRE(model->getPendingDiskImageDownloadURL().has_value());
        CHECK(model->getComputeNodeOS().getDistro() == OS::Distro::Rocky);

        std::filesystem::remove_all(emptyDir);
    }

    TEST_CASE("PBS queue questionnaire dumps PBS settings without SLURM "
              "placeholders")
    {
        initializePresenterTestEnvironment(defaultRunnerOutputs(), true);

        const auto outputPath
            = tempPath("opencattus-tui-pbs-answerfile", "ini");
        const auto diskImagePath = tempPath("opencattus-tui-pbs-iso", "iso");
        std::ofstream(diskImagePath).close();

        auto model = std::make_unique<Cluster>();
        seedClusterMetadata(*model);
        addHeadnodeNetwork(*model, Network::Profile::External, "eno1",
            "192.168.124.0", "192.168.124.10", "255.255.255.0",
            "external.cluster.example.com");
        addHeadnodeNetwork(*model, Network::Profile::Management, "eno2",
            "192.168.30.0", "192.168.30.254", "255.255.255.0",
            "cluster.example.com");
        model->setDiskImage(diskImagePath);
        model->setComputeNodeOS(
            OS(OS::Distro::Rocky, OS::Platform::el9, 6, OS::Arch::x86_64));

        auto state = std::make_shared<ScriptedViewState>();
        state->responses = {
            select("PBS"),
            select("Scatter"),
        };

        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);
        PresenterQueueSystem(model, view);

        REQUIRE(model->getQueueSystem().has_value());
        CHECK(model->getQueueSystem().value()->getKind()
            == QueueSystem::Kind::PBS);
        const auto& pbs
            = dynamic_cast<PBS*>(model->getQueueSystem().value().get());
        CHECK(pbs->getExecutionPlace() == PBS::ExecutionPlace::Scatter);
        CHECK(
            model->getQueueSystem().value()->getDefaultQueue() == "execution");

        model->dumpData(outputPath);
        const auto dumped = opencattus::services::files::read(outputPath);
        CHECK(dumped.contains("[pbs]"));
        CHECK(dumped.contains("execution_place=Scatter"));
        CHECK_FALSE(dumped.contains("[slurm]"));
        CHECK_FALSE(dumped.contains("unused"));

        std::filesystem::remove(outputPath);
        std::filesystem::remove(diskImagePath);
    }

    TEST_CASE("compute questionnaire presenters populate the cluster model")
    {
        initializePresenterTestEnvironment(defaultRunnerOutputs());

        auto model = std::make_unique<Cluster>();
        model->getHeadnode().setOS(
            OS(OS::Distro::RHEL, OS::Platform::el9, 6, OS::Arch::x86_64));
        seedClusterMetadata(*model);
        addHeadnodeNetwork(*model, Network::Profile::External, "eno1",
            "192.168.124.0", "192.168.124.10", "255.255.255.0",
            "external.cluster.example.com", { "1.1.1.1" });
        addHeadnodeNetwork(*model, Network::Profile::Management, "eno2",
            "192.168.30.0", "192.168.30.254", "255.255.255.0",
            "cluster.example.com", { "9.9.9.9" });

        const auto isoDir = createTestIsoDirectory("opencattus-tui-compute");
        const auto outputPath
            = tempPath("opencattus-tui-compute-answerfile", "ini");

        auto state = std::make_shared<ScriptedViewState>();
        state->responses = {
            yesNo(false),
            fields({ isoDir.string() }),
            select("Rocky Linux"),
            select("Rocky-9.6-x86_64-dvd.iso"),
            fields({ "9.6", "x86_64" }),
            select("confluent"),
            multi(1, { "cuda" }),
            fields({ "n", "2", "192.168.30.101", "", "labroot", "labroot" }),
            fields(
                { "2", "16", "1", "65536", "admin", "secret", "1", "115200" }),
            fields({ "2" }),
            fields({ "52:54:00:00:20:11", "172.16.0.11" }),
            fields({ "52:54:00:00:20:12", "172.16.0.12" }),
            select("SLURM"),
            fields({ "batch", "dbroot", "slurmdb" }),
            yesNo(true),
            select("SASL"),
            fields({ "mail.cluster.example.com" }),
            yesNo(true),
            fields({ "/etc/pki/tls/certs/cluster.example.com.cer",
                "/etc/pki/tls/private/cluster.example.com.key" }),
            fields(
                { "smtp.example.com", "587", "relayUser", "examplePassword" }),
        };

        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);

        PresenterNodesOperationalSystem(model, view);
        PresenterProvisioner(model, view);
        PresenterRepository(model, view);
        PresenterNodes(model, view);
        PresenterQueueSystem(model, view);
        PresenterMailSystem(model, view);

        CHECK(state->responses.empty());
        CHECK(model->getProvisioner() == Cluster::Provisioner::Confluent);
        CHECK(model->getHeadnode().getOS().getDistro() == OS::Distro::RHEL);
        CHECK(model->getComputeNodeOS().getDistro() == OS::Distro::Rocky);
        CHECK(model->getComputeNodeOS().getVersion() == "9.6");
        REQUIRE(model->getEnabledRepositories().has_value());
        CHECK(model->getEnabledRepositories().value()
            == std::vector<std::string> { "cuda" });
        CHECK(model->getNodes().size() == 2);
        CHECK(model->slurmMariaDBRootPassword == "dbroot");
        CHECK(model->slurmDBPassword == "slurmdb");
        CHECK(model->slurmStoragePassword == "slurmdb");
        REQUIRE(model->getMailSystem().has_value());
        CHECK(model->getMailSystem()->getProfile() == Postfix::Profile::SASL);
        CHECK(model->getMailSystem()->getSMTPServer().value()
            == "smtp.example.com");
        CHECK(model->getMailSystem()->getUsername().value() == "relayUser");

        model->dumpData(outputPath);
        AnswerFile answerfile(outputPath);

        CHECK(answerfile.system.provisioner == "confluent");
        CHECK(answerfile.system.version == "9.6");
        REQUIRE(answerfile.repositories.enabled.has_value());
        CHECK(answerfile.repositories.enabled.value()
            == std::vector<std::string> {
                "cuda",
            });
        CHECK(answerfile.slurm.partition_name == "batch");
        CHECK(answerfile.slurm.mariadb_root_password == "dbroot");
        REQUIRE(answerfile.postfix.enabled);
        CHECK(answerfile.postfix.profile == Postfix::Profile::SASL);
        CHECK(answerfile.postfix.destination
            == std::vector<std::string> { "mail.cluster.example.com" });
        CHECK(answerfile.postfix.cert_file
            == std::filesystem::path(
                "/etc/pki/tls/certs/cluster.example.com.cer"));
        CHECK(answerfile.postfix.key_file
            == std::filesystem::path(
                "/etc/pki/tls/private/cluster.example.com.key"));
        REQUIRE(answerfile.postfix.smtp.has_value());
        CHECK(answerfile.postfix.smtp->server == "smtp.example.com");
        CHECK(answerfile.postfix.smtp->port == 587);
        REQUIRE(answerfile.postfix.smtp->sasl.has_value());
        CHECK(answerfile.postfix.smtp->sasl->username == "relayUser");
        CHECK(answerfile.postfix.smtp->sasl->password == "examplePassword");
        CHECK(answerfile.nodes.nodes.size() == 2);
        REQUIRE(answerfile.nodes.nodes[0].hostname.has_value());
        CHECK(answerfile.nodes.nodes[0].hostname.value() == "n01");
        REQUIRE(answerfile.nodes.nodes[1].hostname.has_value());
        CHECK(answerfile.nodes.nodes[1].hostname.value() == "n02");

        std::filesystem::remove_all(isoDir);
        std::filesystem::remove(outputPath);
    }

    TEST_CASE(
        "repository questionnaire stores selected repositories on the model")
    {
        initializePresenterTestEnvironment(defaultRunnerOutputs());

        auto model = std::make_unique<Cluster>();
        const auto os
            = OS(OS::Distro::Rocky, OS::Platform::el9, 6, OS::Arch::x86_64);
        model->setComputeNodeOS(os);

        auto state = std::make_shared<ScriptedViewState>();
        state->responses = {
            multi(1, { "cuda", "oneAPI" }),
        };
        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);

        PresenterRepository(model, view);

        REQUIRE(state->multiSelectionMenus.size() == 1);
        CHECK(state->multiSelectionMenus[0].items
            == std::vector<std::string> { "cuda", "beegfs", "elrepo", "grafana",
                "influxdata", "oneAPI", "nvhpc", "rpmfusion", "zfs",
                "zabbix" });
        REQUIRE(model->getEnabledRepositories().has_value());
        CHECK(model->getEnabledRepositories().value()
            == std::vector<std::string> { "cuda", "oneAPI" });
    }

    TEST_CASE("repository questionnaire expands BeeGFS monitoring dependencies")
    {
        initializePresenterTestEnvironment(defaultRunnerOutputs());

        auto model = std::make_unique<Cluster>();
        const auto os
            = OS(OS::Distro::Rocky, OS::Platform::el9, 6, OS::Arch::x86_64);
        model->setComputeNodeOS(os);

        auto state = std::make_shared<ScriptedViewState>();
        state->responses = {
            multi(1, { "beegfs" }),
        };
        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);

        PresenterRepository(model, view);

        REQUIRE(model->getEnabledRepositories().has_value());
        CHECK(model->getEnabledRepositories().value()
            == std::vector<std::string> { "beegfs", "grafana", "influxdata" });
    }

    TEST_CASE("repository questionnaire stop aborts the questionnaire")
    {
        initializePresenterTestEnvironment(defaultRunnerOutputs());

        auto model = std::make_unique<Cluster>();
        const auto os
            = OS(OS::Distro::Rocky, OS::Platform::el9, 6, OS::Arch::x86_64);
        model->setComputeNodeOS(os);

        auto state = std::make_shared<ScriptedViewState>();
        state->responses = {
            multi(2, {}),
        };
        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);

        CHECK_THROWS_AS(PresenterRepository(model, view), ViewAbortRequested);
        CHECK_FALSE(model->getEnabledRepositories().has_value());
    }

    TEST_CASE("network questionnaires hide already consumed interfaces while "
              "keeping service and management sharing available")
    {
        initializePresenterTestEnvironment(defaultRunnerOutputs());

        const auto interfaces = usableHostInterfaces();
        if (interfaces.size() < 2) {
            MESSAGE("Skipping PresenterNetwork menu test: need at least two "
                    "interfaces");
            return;
        }

        auto model = std::make_unique<Cluster>();
        auto state = std::make_shared<ScriptedViewState>();
        state->responses = {
            select(interfaces[0]),
            fields({ "192.168.124.10", "255.255.255.0", "192.168.124.1",
                "external.cluster.example.com", "1.1.1.1" }),
            select(interfaces[1]),
            fields({ "192.168.30.254", "255.255.255.0", "192.168.30.1",
                "cluster.example.com", "9.9.9.9" }),
            select(interfaces[1]),
            fields({ "172.16.10.254", "255.255.255.0", "",
                "service.cluster.example.com", "1.1.1.1" }),
        };
        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);

        NetworkCreator nc;
        PresenterNetwork(model, view, nc, Network::Profile::External,
            Network::Type::Ethernet);
        PresenterNetwork(model, view, nc, Network::Profile::Management,
            Network::Type::Ethernet);
        PresenterNetwork(model, view, nc, Network::Profile::Service,
            Network::Type::Ethernet);

        REQUIRE(state->listMenus.size() == 3);
        const auto& managementMenu = firstMenuByMessage(state->listMenus,
            "Select your Management (Ethernet) network interface");
        CHECK(std::ranges::find(managementMenu.items, interfaces[0])
            == managementMenu.items.end());
        CHECK(std::ranges::find(managementMenu.items, interfaces[1])
            != managementMenu.items.end());

        const auto& serviceMenu = firstMenuByMessage(state->listMenus,
            "Select your Service (Ethernet) network interface");
        CHECK(std::ranges::find(serviceMenu.items, interfaces[0])
            == serviceMenu.items.end());
        CHECK(std::ranges::find(serviceMenu.items, interfaces[1])
            != serviceMenu.items.end());
    }

    TEST_CASE("service network questionnaire accepts an empty gateway")
    {
        initializePresenterTestEnvironment(defaultRunnerOutputs());

        const auto interfaces = usableHostInterfaces();
        if (interfaces.size() < 2) {
            MESSAGE("Skipping PresenterNetwork TUI test: need at least two "
                    "interfaces");
            return;
        }

        auto model = std::make_unique<Cluster>();
        auto state = std::make_shared<ScriptedViewState>();
        state->responses = {
            select(interfaces[1]),
            fields({ "172.16.10.254", "255.255.255.0", "",
                "service.cluster.example.com", "1.1.1.1, 9.9.9.9" }),
        };
        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);

        NetworkCreator nc;
        CHECK(nc.addNetworkInformation(NetworkCreatorData {
            .profile = Network::Profile::External,
            .type = Network::Type::Ethernet,
            .interface = interfaces[0],
            .address = "192.168.124.10",
            .subnetMask = "255.255.255.0",
            .gateway = "192.168.124.1",
            .name = "external.cluster.example.com",
            .domains = { boost::asio::ip::make_address("1.1.1.1") },
        }));

        PresenterNetwork(model, view, nc, Network::Profile::Service,
            Network::Type::Ethernet);
        nc.saveNetworksToModel(*model);

        CHECK(state->responses.empty());
        const auto& service = model->getNetwork(Network::Profile::Service);
        CHECK(service.getAddress().to_string() == "172.16.10.0");
        CHECK(service.getGateway().is_unspecified());
        CHECK(service.getDomainName() == "service.cluster.example.com");
        CHECK(service.getNameservers()
            == std::vector<boost::asio::ip::address> {
                boost::asio::ip::make_address("1.1.1.1"),
                boost::asio::ip::make_address("9.9.9.9"),
            });
    }

    TEST_CASE("network questionnaire rejects a gateway outside the selected "
              "subnet")
    {
        initializePresenterTestEnvironment(defaultRunnerOutputs());

        const auto interfaces = usableHostInterfaces();
        if (interfaces.size() < 2) {
            MESSAGE("Skipping PresenterNetwork gateway subnet test: need at "
                    "least two interfaces");
            return;
        }

        auto model = std::make_unique<Cluster>();
        auto state = std::make_shared<ScriptedViewState>();
        state->responses = {
            select(interfaces[1]),
            fields({ "192.168.30.254", "255.255.255.0", "10.10.10.1",
                "cluster.example.com", "9.9.9.9" }),
            fields({ "192.168.30.254", "255.255.255.0", "192.168.30.1",
                "cluster.example.com", "9.9.9.9" }),
        };
        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);

        NetworkCreator nc;
        CHECK(nc.addNetworkInformation(NetworkCreatorData {
            .profile = Network::Profile::External,
            .type = Network::Type::Ethernet,
            .interface = interfaces[0],
            .address = "192.168.124.10",
            .subnetMask = "255.255.255.0",
            .gateway = "192.168.124.1",
            .name = "external.cluster.example.com",
            .domains = { boost::asio::ip::make_address("1.1.1.1") },
        }));

        PresenterNetwork(model, view, nc, Network::Profile::Management,
            Network::Type::Ethernet);
        nc.saveNetworksToModel(*model);

        CHECK(state->responses.empty());
        CHECK(std::ranges::any_of(state->messages, [](const auto& message) {
            return message
                == "Network settings|Gateway must be inside the selected "
                   "subnet";
        }));

        const auto& management
            = model->getNetwork(Network::Profile::Management);
        CHECK(management.getGateway().to_string() == "192.168.30.1");
    }

    TEST_CASE(
        "service network sharing management interface must use a separate "
        "subnet")
    {
        initializePresenterTestEnvironment(defaultRunnerOutputs());

        const auto interfaces = usableHostInterfaces();
        if (interfaces.empty()) {
            MESSAGE(
                "Skipping PresenterNetwork shared service subnet test: need "
                "a usable interface");
            return;
        }

        auto model = std::make_unique<Cluster>();
        auto state = std::make_shared<ScriptedViewState>();
        state->responses = {
            select(interfaces[0]),
            fields({ "172.21.1.200", "255.255.255.0", "", "cluster.example.com",
                "9.9.9.9" }),
            fields({ "192.168.200.103", "255.255.255.0", "",
                "service.cluster.example.com", "9.9.9.9" }),
        };
        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);

        NetworkCreator nc;
        CHECK(nc.addNetworkInformation(NetworkCreatorData {
            .profile = Network::Profile::Management,
            .type = Network::Type::Ethernet,
            .interface = interfaces[0],
            .address = "172.21.1.103",
            .subnetMask = "255.255.255.0",
            .gateway = "172.21.1.1",
            .name = "cluster.example.com",
            .domains = { boost::asio::ip::make_address("9.9.9.9") },
        }));

        PresenterNetwork(model, view, nc, Network::Profile::Service,
            Network::Type::Ethernet);
        nc.saveNetworksToModel(*model);

        CHECK(state->responses.empty());
        CHECK(std::ranges::any_of(state->messages, [](const auto& message) {
            return message
                == "Network settings|The service network must use a separate "
                   "subnet from the management network";
        }));

        const auto& service = model->getNetwork(Network::Profile::Service);
        CHECK(service.getAddress().to_string() == "192.168.200.0");
        CHECK(model->getHeadnode()
                  .getConnection(Network::Profile::Service)
                  .getAddress()
                  .to_string()
            == "192.168.200.103");
    }

    TEST_CASE("management and service network questionnaires reuse known "
              "defaults")
    {
        initializePresenterTestEnvironment(defaultRunnerOutputs());

        const auto interfaces = usableHostInterfaces();
        if (interfaces.size() < 2) {
            MESSAGE("Skipping PresenterNetwork defaults test: need at least "
                    "two interfaces");
            return;
        }

        auto model = std::make_unique<Cluster>();
        seedClusterMetadata(*model);
        auto state = std::make_shared<ScriptedViewState>();
        state->responses = {
            select(interfaces[1]),
            fields({ "192.168.30.254", "255.255.255.0", "192.168.30.1",
                "cluster.example.com", "9.9.9.9" }),
            select(interfaces[1]),
            fields({ "172.16.10.254", "255.255.255.0", "",
                "cluster.example.com", "9.9.9.9" }),
        };
        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);

        NetworkCreator nc;
        PresenterNetwork(model, view, nc, Network::Profile::Management,
            Network::Type::Ethernet);

        const auto& managementMenu = firstFieldMenuByMessage(
            state->fieldMenus, "Enter the required network details");
        CHECK(managementMenu.items[3].second == "cluster.example.com");

        PresenterNetwork(model, view, nc, Network::Profile::Service,
            Network::Type::Ethernet);

        REQUIRE(state->fieldMenus.size() >= 2);
        const auto& serviceMenu = state->fieldMenus[1];
        CHECK(serviceMenu.items[2].second.empty());
        CHECK(serviceMenu.items[3].second == "cluster.example.com");
        CHECK(serviceMenu.items[4].second == "9.9.9.9");
    }

    TEST_CASE("infiniband questionnaire persists an explicit OFED version")
    {
        initializePresenterTestEnvironment(defaultRunnerOutputs());

        const auto interfaces = usableInfinibandInterfaces();
        if (interfaces.empty()) {
            MESSAGE("Skipping PresenterInfiniband TUI test: need a usable "
                    "Infiniband interface");
            return;
        }

        auto model = std::make_unique<Cluster>();
        auto state = std::make_shared<ScriptedViewState>();
        state->responses = {
            yesNo(true),
            select("DOCA"),
            fields({ "latest-3.2-LTS" }),
            select(interfaces.front()),
            fields({ "172.16.0.254", "255.255.255.0", "",
                "application.cluster.example.com", "1.1.1.1" }),
        };
        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);

        NetworkCreator nc;
        PresenterInfiniband(model, view, nc);
        nc.saveNetworksToModel(*model);

        REQUIRE(model->getOFED().has_value());
        CHECK(model->getOFED()->getKind() == OFED::Kind::Doca);
        CHECK(model->getOFED()->getVersion() == "latest-3.2-LTS");
    }

    TEST_CASE("compute node entry retries instead of throwing on invalid node "
              "definitions")
    {
        initializePresenterTestEnvironment(defaultRunnerOutputs());

        auto model = std::make_unique<Cluster>();
        addHeadnodeNetwork(*model, Network::Profile::Management, "eno2",
            "192.168.30.0", "192.168.30.254", "255.255.255.0",
            "cluster.example.com", { "9.9.9.9" });

        auto state = std::make_shared<ScriptedViewState>();
        state->responses = {
            fields({ "n", "2", "192.168.30.101", "", "labroot", "labroot" }),
            fields(
                { "1", "8", "2", "32768", "admin", "secret", "1", "115200" }),
            fields({ "1" }),
            fields({ "bad-mac", "172.16.0.11" }),
            fields({ "52:54:00:00:20:11", "172.16.0.11" }),
        };
        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);

        PresenterNodes(model, view);

        REQUIRE(model->getNodes().size() == 1);
        CHECK(model->getNodes().front().getHostname() == "n01");
        CHECK(std::ranges::any_of(state->messages, [](const auto& message) {
            return message.starts_with(
                "Compute nodes settings|Invalid node definition for n01:");
        }));
    }

    TEST_CASE("compute node questionnaire leaves BMC pattern blank without a "
              "service network")
    {
        initializePresenterTestEnvironment(defaultRunnerOutputs());

        auto model = std::make_unique<Cluster>();
        addHeadnodeNetwork(*model, Network::Profile::Management, "eno2",
            "192.168.30.0", "192.168.30.254", "255.255.255.0",
            "cluster.example.com", { "9.9.9.9" });

        auto state = std::make_shared<ScriptedViewState>();
        state->responses = {
            fields({ "n", "2", "192.168.30.101", "", "labroot", "labroot" }),
            fields(
                { "1", "8", "2", "32768", "admin", "secret", "1", "115200" }),
            fields({ "1" }),
            fields({ "52:54:00:00:20:11", "192.168.30.201" }),
        };
        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);

        PresenterNodes(model, view);

        const auto& genericMenu = firstFieldMenuByMessage(
            state->fieldMenus, "Enter the compute nodes information");
        REQUIRE(genericMenu.items.size() == 6);
        CHECK(genericMenu.items[2].second == "192.168.30.101");
        CHECK(genericMenu.items[3].second.empty());
        REQUIRE(model->getNodes().size() == 1);
        REQUIRE(model->getNodes().front().getBMC().has_value());
        CHECK(model->getNodes().front().getBMC()->getAddress()
            == "192.168.30.201");
    }

    TEST_CASE("compute node questionnaire accepts nodes without BMC addresses")
    {
        initializePresenterTestEnvironment(defaultRunnerOutputs());

        auto model = std::make_unique<Cluster>();
        addHeadnodeNetwork(*model, Network::Profile::Management, "eno2",
            "192.168.30.0", "192.168.30.254", "255.255.255.0",
            "cluster.example.com", { "9.9.9.9" });

        auto state = std::make_shared<ScriptedViewState>();
        state->responses = {
            fields({ "n", "2", "192.168.30.101", "", "labroot", "labroot" }),
            fields(
                { "1", "8", "2", "32768", "admin", "secret", "1", "115200" }),
            fields({ "1" }),
            fields({ "52:54:00:00:20:11", "" }),
        };
        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);

        PresenterNodes(model, view);

        CHECK(state->responses.empty());
        REQUIRE(model->getNodes().size() == 1);
        CHECK_FALSE(model->getNodes().front().getBMC().has_value());
    }

    TEST_CASE(
        "compute node questionnaire rejects BMC addresses matching compute "
        "node addresses")
    {
        initializePresenterTestEnvironment(defaultRunnerOutputs());

        auto model = std::make_unique<Cluster>();
        addHeadnodeNetwork(*model, Network::Profile::Management, "eno2",
            "192.168.30.0", "192.168.30.254", "255.255.255.0",
            "cluster.example.com", { "9.9.9.9" });

        auto state = std::make_shared<ScriptedViewState>();
        state->responses = {
            fields({ "n", "2", "192.168.30.101", "192.168.30.101", "labroot",
                "labroot" }),
            fields({ "n", "2", "192.168.30.101", "192.168.30.201", "labroot",
                "labroot" }),
            fields(
                { "1", "8", "2", "32768", "admin", "secret", "1", "115200" }),
            fields({ "1" }),
            fields({ "52:54:00:00:20:11", "192.168.30.101" }),
            fields({ "52:54:00:00:20:11", "192.168.30.201" }),
        };
        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);

        PresenterNodes(model, view);

        CHECK(state->responses.empty());
        CHECK(std::ranges::any_of(state->messages, [](const auto& message) {
            return message
                == "Compute nodes settings|BMC first IP cannot match the "
                   "compute "
                   "node first IP";
        }));
        CHECK(std::ranges::any_of(state->messages, [](const auto& message) {
            return message
                == "Compute nodes settings|BMC IP address cannot match the "
                   "compute node IP address";
        }));
        REQUIRE(model->getNodes().size() == 1);
        REQUIRE(model->getNodes().front().getBMC().has_value());
        CHECK(model->getNodes().front().getBMC()->getAddress()
            == "192.168.30.201");
    }

    TEST_CASE("compute node questionnaire suggests node and BMC IP patterns")
    {
        initializePresenterTestEnvironment(defaultRunnerOutputs());

        auto model = std::make_unique<Cluster>();
        addHeadnodeNetwork(*model, Network::Profile::Management, "eno2",
            "192.168.30.0", "192.168.30.254", "255.255.255.0",
            "cluster.example.com", { "9.9.9.9" });
        addHeadnodeNetwork(*model, Network::Profile::Service, "eno2",
            "172.16.10.0", "172.16.10.254", "255.255.255.0",
            "service.cluster.example.com", { "1.1.1.1" });

        auto state = std::make_shared<ScriptedViewState>();
        state->responses = {
            fields({ "n", "2", "192.168.30.101", "172.16.10.101", "labroot",
                "labroot" }),
            fields(
                { "1", "8", "2", "32768", "admin", "secret", "1", "115200" }),
            fields({ "2" }),
            fields({ "52:54:00:00:20:11", "172.16.10.101" }),
            fields({ "52:54:00:00:20:12", "172.16.10.102" }),
        };
        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);

        PresenterNodes(model, view);

        const auto& genericMenu = firstFieldMenuByMessage(
            state->fieldMenus, "Enter the compute nodes information");
        REQUIRE(genericMenu.items.size() == 6);
        CHECK(genericMenu.items[2].second == "192.168.30.101");
        CHECK(genericMenu.items[3].second == "172.16.10.101");

        const auto firstNodeMenu = std::ranges::find_if(
            state->fieldMenus, [](const auto& menu) {
                return menu.message
                    == "Enter the management MAC and BMC IP address for node: "
                       "n01";
            });
        REQUIRE(firstNodeMenu != state->fieldMenus.end());
        REQUIRE(firstNodeMenu->items.size() == 2);
        CHECK(firstNodeMenu->items[1].second == "172.16.10.101");

        const auto secondNodeMenu = std::ranges::find_if(
            state->fieldMenus, [](const auto& menu) {
                return menu.message
                    == "Enter the management MAC and BMC IP address for node: "
                       "n02";
            });
        REQUIRE(secondNodeMenu != state->fieldMenus.end());
        REQUIRE(secondNodeMenu->items.size() == 2);
        CHECK(secondNodeMenu->items[1].second == "172.16.10.102");
    }

    TEST_CASE("presenter install can drive the questionnaire end to end")
    {
        initializePresenterTestEnvironment(defaultRunnerOutputs());

        const auto interfaces = usableHostInterfaces();
        if (interfaces.size() < 2) {
            MESSAGE("Skipping PresenterInstall TUI test: need at least two "
                    "interfaces");
            return;
        }

        auto model = std::make_unique<Cluster>();
        model->getHeadnode().setOS(
            OS(OS::Distro::RHEL, OS::Platform::el9, 6, OS::Arch::x86_64));
        const auto outputPath
            = tempPath("opencattus-tui-install-answerfile", "ini");

        auto state = std::make_shared<ScriptedViewState>();
        state->allowProgressMenu = true;
        state->responses = {
            fields({ "demo", "acme", "admin@example.com" }),
            select("Text"),
            select("America"),
            select("Sao_Paulo"),
            collect({ "0.pool.ntp.org", "1.pool.ntp.org" }),
            select("English (en)"),
            fields({ "headnode", "cluster.example.com" }),
            select(interfaces[0]),
            fields({ "192.168.124.10", "255.255.255.0", "192.168.124.1",
                "external.cluster.example.com", "1.1.1.1, 8.8.8.8" }),
            select(interfaces[1]),
            fields({ "192.168.30.254", "255.255.255.0", "192.168.30.1",
                "cluster.example.com", "9.9.9.9" }),
            yesNo(false),
            yesNo(true),
            select("Rocky Linux"),
            fields({ "9.6", "x86_64" }),
            select("confluent"),
            multi(1, { "serial-libs", "parallel-libs" }),
            multi(1, { "cuda" }),
            fields({ "n", "2", "192.168.30.101", "", "labroot", "labroot" }),
            fields(
                { "1", "8", "2", "32768", "admin", "secret", "1", "115200" }),
            fields({ "2" }),
            fields({ "52:54:00:00:20:11", "172.16.0.11" }),
            fields({ "52:54:00:00:20:12", "172.16.0.12" }),
            select("SLURM"),
            fields({ "batch", "dbroot", "slurmdb" }),
            yesNo(false),
        };

        if (hasUsableInfinibandInterface()) {
            state->responses.insert(
                state->responses.begin() + 12, yesNo(false));
        }

        std::vector<std::string> completedSteps;
        std::vector<std::vector<std::string>> callbackStates;
        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);
        PresenterInstall(
            model, view, [&](const std::vector<std::string>& steps) {
                completedSteps = steps;
                callbackStates.push_back(steps);
            });

        CHECK_FALSE(view);
        CHECK(state->responses.empty());
        CHECK(completedSteps
            == std::vector<std::string> { "general", "time", "locale",
                "hostname", "networking", "os", "provisioner", "ohpc",
                "repositories", "nodes", "queue", "mail", "preflight" });

        const auto generalScreen
            = firstMessageIndex(state->messages, "General cluster settings|");
        const auto timeScreen
            = firstMessageIndex(state->messages, "Time and clock settings|");
        const auto localeScreen
            = firstMessageIndex(state->messages, "Locale settings|");
        const auto hostScreen
            = firstMessageIndex(state->messages, "Hostname settings|");
        const auto networkScreens = messageIndices(
            state->messages, "Network settings|Configure the ");
        const auto servicePrompt = firstMessageIndex(state->messages,
            "Network settings|Do you want to configure a service network?");
        const auto osScreen
            = firstMessageIndex(state->messages, "Compute node OS settings|");
        const auto provisionerScreen
            = firstMessageIndex(state->messages, "Provisioner settings|");
        const auto ohpcScreen
            = firstMessageIndex(state->messages, "OpenHPC bundles|");
        const auto repositoryScreen
            = firstMessageIndex(state->messages, "Repositories|");
        const auto nodesScreen
            = firstMessageIndex(state->messages, "Compute nodes settings|");
        const auto queueScreen
            = firstMessageIndex(state->messages, "Queue system settings|");
        const auto mailScreen
            = firstMessageIndex(state->messages, "Mail system settings|");
        const auto preflightScreen
            = firstMessageIndex(state->messages, "Preflight validation|");
        const auto downloadScreen = firstMessageIndex(
            state->messages, "Compute node OS settings|Downloading ISO");

        CHECK(generalScreen < timeScreen);
        CHECK(timeScreen < localeScreen);
        CHECK(localeScreen < hostScreen);
        REQUIRE(networkScreens.size() == 2);
        CHECK(hostScreen < networkScreens[0]);
        CHECK(networkScreens[0] < networkScreens[1]);
        CHECK(networkScreens[1] < servicePrompt);
        if (hasUsableInfinibandInterface()) {
            const auto infinibandScreen = firstMessageIndex(state->messages,
                "InfiniBand settings|Do you have an InfiniBand fabric "
                "available?");
            CHECK(servicePrompt < infinibandScreen);
            CHECK(infinibandScreen < osScreen);
        } else {
            CHECK(messageIndices(state->messages, "InfiniBand settings|")
                    .empty());
            CHECK(servicePrompt < osScreen);
        }
        CHECK(osScreen < provisionerScreen);
        CHECK(provisionerScreen < ohpcScreen);
        CHECK(ohpcScreen < repositoryScreen);
        CHECK(repositoryScreen < nodesScreen);
        CHECK(nodesScreen < queueScreen);
        CHECK(queueScreen < mailScreen);
        CHECK(mailScreen < preflightScreen);
        CHECK(preflightScreen < downloadScreen);

        const auto& preflightMessage = firstScrollableMessageByMessage(
            state->scrollableMessages, "Review the installation plan");
        CHECK(preflightMessage.message
            == "Review the installation plan. Choosing OK will start "
               "modifying this system.");
        const auto& preflightText = preflightMessage.text;
        CHECK(preflightText.starts_with("Cluster"));
        CHECK(preflightText.contains("Cluster        demo"));
        CHECK(preflightText.contains("Headnode"));
        CHECK(preflightText.contains("RHEL 9.6 x86_64 with Confluent"));
        CHECK(preflightText.contains("Nodes"));
        CHECK(preflightText.contains("Rocky 9.6 x86_64"));
        CHECK_FALSE(preflightText.contains("Compatibility"));
        CHECK(preflightText.contains("ISO and OS     Rocky 9.6 from "
                                     "/root/Rocky-9.6-x86_64-dvd.iso"));
        CHECK(preflightText.contains(
            "/root/Rocky-9.6-x86_64-dvd.iso\n\nRepositories"));
        CHECK(preflightText.contains("\n\n[Networks]"));
        CHECK(preflightText.contains("External Ethernet"));
        CHECK(preflightText.contains("  Interface"));
        CHECK(preflightText.contains("  Host IP"));
        CHECK(preflightText.contains("  Network"));
        CHECK(preflightText.contains("  Gateway"));
        CHECK_FALSE(preflightText.contains("first BMC"));
        CHECK(preflightText.contains("Repositories   Optional: cuda"));
        CHECK(
            preflightText.contains("OpenHPC        base GNU compilers and MPI "
                                   "stacks"));
        CHECK(preflightText.contains(
            "serial scientific libs, parallel scientific libs"));
        CHECK(
            preflightText.contains("parallel scientific libs\n\nQueue system"));
        CHECK(preflightText.contains("Queue system   SLURM"));
        CHECK(preflightText.contains("Queue name     batch"));
        CHECK(preflightText.contains("\n\n[Nodes]"));
        CHECK(preflightText.contains("Hostname"));
        CHECK(preflightText.contains("Node IP"));
        CHECK(preflightText.contains("BMC IP"));
        for (std::size_t start = 0; start < preflightText.size();) {
            const auto end = preflightText.find('\n', start);
            const auto line = end == std::string::npos
                ? std::string_view(preflightText).substr(start)
                : std::string_view(preflightText).substr(start, end - start);
            if (line.starts_with("Hostname") || line.starts_with("n01")
                || line.starts_with("n02")) {
                CHECK(line.size() <= 61);
            }

            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
        CHECK(preflightText.contains("n01"));
        CHECK(preflightText.contains("192.168.30.101"));
        CHECK(preflightText.contains("172.16.0.11"));
        CHECK(preflightText.contains("52:54:00:00:20:11"));
        CHECK(preflightText.contains("n02"));
        CHECK(preflightText.contains("192.168.30.102"));
        CHECK(preflightText.contains("172.16.0.12"));
        CHECK(preflightText.contains("52:54:00:00:20:12"));

        CHECK(model->getName() == "demo");
        CHECK(model->getCompanyName() == "acme");
        CHECK(model->getAdminMail() == "admin@example.com");
        CHECK(model->getHeadnode().getHostname() == "headnode");
        CHECK(model->getDomainName() == "cluster.example.com");
        CHECK(model->getTimezone().getTimezone() == "America/Sao_Paulo");
        CHECK(model->getLocale() == "en_US.utf8");
        CHECK(model->getProvisioner() == Cluster::Provisioner::Confluent);
        CHECK(model->getHeadnode().getOS().getDistro() == OS::Distro::RHEL);
        CHECK(model->getComputeNodeOS().getDistro() == OS::Distro::Rocky);
        CHECK_FALSE(model->getPendingDiskImageDownloadURL().has_value());
        REQUIRE(model->getEnabledOpenHPCBundles().has_value());
        CHECK(model->getEnabledOpenHPCBundles().value()
            == std::vector<std::string> { "serial-libs", "parallel-libs" });
        REQUIRE(model->getEnabledRepositories().has_value());
        CHECK(model->getEnabledRepositories().value()
            == std::vector<std::string> { "cuda" });
        CHECK(model->getNodes().size() == 2);
        CHECK(model->getNetworks().size() == 2);

        model->dumpData(outputPath);
        AnswerFile answerfile(outputPath);

        CHECK(answerfile.information.cluster_name == "demo");
        CHECK(answerfile.hostname.hostname == "headnode");
        CHECK(answerfile.hostname.domain_name == "cluster.example.com");
        CHECK(answerfile.time.timezone == "America/Sao_Paulo");
        CHECK(answerfile.time.locale == "en_US.utf8");
        CHECK(answerfile.system.provisioner == "confluent");
        CHECK(answerfile.system.version == "9.6");
        REQUIRE(answerfile.ohpc.enabled.has_value());
        CHECK(answerfile.ohpc.enabled.value()
            == std::vector<std::string> { "serial-libs", "parallel-libs" });
        REQUIRE(answerfile.repositories.enabled.has_value());
        CHECK(answerfile.repositories.enabled.value()
            == std::vector<std::string> {
                "cuda",
            });
        REQUIRE(answerfile.external.con_ip_addr.has_value());
        CHECK(answerfile.external.con_ip_addr->to_string() == "192.168.124.10");
        REQUIRE(answerfile.management.con_ip_addr.has_value());
        CHECK(
            answerfile.management.con_ip_addr->to_string() == "192.168.30.254");
        CHECK(answerfile.nodes.nodes.size() == 2);
        CHECK(answerfile.slurm.partition_name == "batch");
        CHECK(answerfile.slurm.slurmdb_password == "slurmdb");

        std::filesystem::remove(outputPath);
    }

    TEST_CASE("presenter install propagates aborts from network prompts")
    {
        initializePresenterTestEnvironment(defaultRunnerOutputs());

        const auto interfaces = usableHostInterfaces();
        if (interfaces.empty()) {
            MESSAGE("Skipping PresenterInstall abort test: need at least one "
                    "interface");
            return;
        }

        auto model = std::make_unique<Cluster>();
        auto state = std::make_shared<ScriptedViewState>();
        state->responses = {
            fields({ "demo", "acme", "admin@example.com" }),
            select("Text"),
            select("America"),
            select("Sao_Paulo"),
            collect({ "0.pool.ntp.org", "1.pool.ntp.org" }),
            select("English (en)"),
            fields({ "headnode", "cluster.example.com" }),
            select(interfaces[0]),
            abortSelection(),
        };

        std::vector<std::string> completedSteps;
        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);

        CHECK_THROWS_AS(PresenterInstall(model, view,
                            [&](const std::vector<std::string>& steps) {
                                completedSteps = steps;
                            }),
            ViewAbortRequested);
        CHECK(completedSteps
            == std::vector<std::string> {
                "general",
                "time",
                "locale",
                "hostname",
            });
    }

    TEST_CASE("presenter install can drive the questionnaire end to end on "
              "dry-run")
    {
        initializePresenterTestEnvironment(defaultRunnerOutputs(), true);

        const auto interfaces = usableHostInterfaces();
        if (interfaces.size() < 2) {
            MESSAGE("Skipping PresenterInstall dry-run TUI test: need at least "
                    "two interfaces");
            return;
        }

        auto model = std::make_unique<Cluster>();
        model->getHeadnode().setOS(
            OS(OS::Distro::RHEL, OS::Platform::el9, 6, OS::Arch::x86_64));
        const auto outputPath
            = tempPath("opencattus-tui-install-dry-run-answerfile", "ini");

        auto state = std::make_shared<ScriptedViewState>();
        state->responses = {
            fields({ "demo", "acme", "admin@example.com" }),
            select("Text"),
            select("America"),
            select("Sao_Paulo"),
            collect({ "0.pool.ntp.org", "1.pool.ntp.org" }),
            select("English (en)"),
            fields({ "headnode", "cluster.example.com" }),
            select(interfaces[0]),
            fields({ "192.168.124.10", "255.255.255.0", "192.168.124.1",
                "external.cluster.example.com", "1.1.1.1, 8.8.8.8" }),
            select(interfaces[1]),
            fields({ "192.168.30.254", "255.255.255.0", "192.168.30.1",
                "cluster.example.com", "9.9.9.9" }),
            yesNo(false),
            yesNo(true),
            select("Rocky Linux"),
            fields({ "9.6", "x86_64" }),
            select("confluent"),
            multi(1, { "serial-libs", "parallel-libs" }),
            multi(1, { "cuda" }),
            fields({ "n", "2", "192.168.30.101", "", "labroot", "labroot" }),
            fields(
                { "1", "8", "2", "32768", "admin", "secret", "1", "115200" }),
            fields({ "2" }),
            fields({ "52:54:00:00:20:11", "172.16.0.11" }),
            fields({ "52:54:00:00:20:12", "172.16.0.12" }),
            select("SLURM"),
            fields({ "batch", "dbroot", "slurmdb" }),
            yesNo(false),
        };

        if (hasUsableInfinibandInterface()) {
            state->responses.insert(
                state->responses.begin() + 12, yesNo(false));
        }

        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);
        PresenterInstall(model, view);

        CHECK_FALSE(view);
        CHECK(state->responses.empty());
        CHECK(model->getTimezone().getTimezone() == "America/Sao_Paulo");
        CHECK(model->getLocale() == "en_US.utf8");
        CHECK(model->getProvisioner() == Cluster::Provisioner::Confluent);
        CHECK_FALSE(model->getPendingDiskImageDownloadURL().has_value());
        REQUIRE(model->getEnabledOpenHPCBundles().has_value());
        CHECK(model->getEnabledOpenHPCBundles().value()
            == std::vector<std::string> { "serial-libs", "parallel-libs" });
        REQUIRE(model->getEnabledRepositories().has_value());
        CHECK(model->getEnabledRepositories().value()
            == std::vector<std::string> { "cuda" });

        model->dumpData(outputPath);
        AnswerFile answerfile(outputPath);
        CHECK(answerfile.time.timezone == "America/Sao_Paulo");
        CHECK(answerfile.time.locale == "en_US.utf8");
        CHECK(answerfile.system.provisioner == "confluent");
        REQUIRE(answerfile.ohpc.enabled.has_value());
        CHECK(answerfile.ohpc.enabled.value()
            == std::vector<std::string> { "serial-libs", "parallel-libs" });

        std::filesystem::remove(outputPath);
    }

    TEST_CASE("presenter install rewinds to the previous step when Back is "
              "requested")
    {
        initializePresenterTestEnvironment(defaultRunnerOutputs());

        const auto interfaces = usableHostInterfaces();
        if (interfaces.size() < 2) {
            MESSAGE("Skipping PresenterInstall back-navigation test: need at "
                    "least two interfaces");
            return;
        }

        auto model = std::make_unique<Cluster>();
        model->getHeadnode().setOS(
            OS(OS::Distro::RHEL, OS::Platform::el9, 6, OS::Arch::x86_64));

        auto state = std::make_shared<ScriptedViewState>();
        state->allowProgressMenu = true;
        state->responses = {
            fields({ "demo", "acme", "admin@example.com" }),
            select("Text"),
            select("America"),
            backSelection(),
            fields({ "demo", "acme", "admin@example.com" }),
            select("Text"),
            select("America"),
            select("Sao_Paulo"),
            collect({ "0.pool.ntp.org", "1.pool.ntp.org" }),
            select("English (en)"),
            fields({ "headnode", "cluster.example.com" }),
            select(interfaces[0]),
            fields({ "192.168.124.10", "255.255.255.0", "192.168.124.1",
                "external.cluster.example.com", "1.1.1.1, 8.8.8.8" }),
            select(interfaces[1]),
            fields({ "192.168.30.254", "255.255.255.0", "192.168.30.1",
                "cluster.example.com", "9.9.9.9" }),
            yesNo(false),
            yesNo(true),
            select("Rocky Linux"),
            fields({ "9.6", "x86_64" }),
            select("confluent"),
            multi(1, { "serial-libs", "parallel-libs" }),
            multi(1, { "cuda" }),
            fields({ "n", "1", "192.168.30.101", "", "labroot", "labroot" }),
            fields(
                { "1", "8", "2", "32768", "admin", "secret", "1", "115200" }),
            fields({ "1" }),
            fields({ "52:54:00:00:20:11", "172.16.0.11" }),
            select("SLURM"),
            fields({ "batch", "dbroot", "slurmdb" }),
            yesNo(false),
        };

        if (hasUsableInfinibandInterface()) {
            state->responses.insert(
                state->responses.begin() + 16, yesNo(false));
        }

        std::vector<std::string> completedSteps;
        std::vector<std::vector<std::string>> callbackStates;
        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);
        PresenterInstall(
            model, view, [&](const std::vector<std::string>& steps) {
                completedSteps = steps;
                callbackStates.push_back(steps);
            });

        CHECK_FALSE(view);
        CHECK(state->responses.empty());
        CHECK(completedSteps
            == std::vector<std::string> { "general", "time", "locale",
                "hostname", "networking", "os", "provisioner", "ohpc",
                "repositories", "nodes", "queue", "mail", "preflight" });

        const auto generalPrompts
            = messageIndices(state->messages, "General cluster settings|");
        const auto timePrompts
            = messageIndices(state->messages, "Time and clock settings|");
        CHECK(generalPrompts.size() >= 2);
        CHECK(timePrompts.size() >= 2);
        CHECK(std::ranges::find(callbackStates, std::vector<std::string> {})
            != callbackStates.end());
        CHECK(model->getTimezone().getTimezone() == "America/Sao_Paulo");
    }
}
