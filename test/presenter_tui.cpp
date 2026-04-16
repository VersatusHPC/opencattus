/*
 * Copyright 2026 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <doctest/doctest.h>

#include <algorithm>
#include <deque>
#include <filesystem>
#include <fmt/core.h>
#include <fstream>
#include <list>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <opencattus/connection.h>
#include <opencattus/models/answerfile.h>
#include <opencattus/models/cluster.h>
#include <opencattus/patterns/singleton.h>
#include <opencattus/presenter/PresenterInfiniband.h>
#include <opencattus/presenter/PresenterInstall.h>
#include <opencattus/presenter/PresenterMailSystem.h>
#include <opencattus/presenter/PresenterNetwork.h>
#include <opencattus/presenter/PresenterNodes.h>
#include <opencattus/presenter/PresenterNodesOperationalSystem.h>
#include <opencattus/presenter/PresenterProvisioner.h>
#include <opencattus/presenter/PresenterQueueSystem.h>
#include <opencattus/services/options.h>
#include <opencattus/services/runner.h>
#include <opencattus/view/view.h>

namespace {

using opencattus::models::AnswerFile;
using opencattus::models::Cluster;
using opencattus::presenter::NetworkCreator;
using opencattus::presenter::NetworkCreatorData;
using opencattus::presenter::PresenterInfiniband;
using opencattus::presenter::PresenterInstall;
using opencattus::presenter::PresenterMailSystem;
using opencattus::presenter::PresenterNetwork;
using opencattus::presenter::PresenterNodes;
using opencattus::presenter::PresenterNodesOperationalSystem;
using opencattus::presenter::PresenterProvisioner;
using opencattus::presenter::PresenterQueueSystem;
using opencattus::services::CommandProxy;
using opencattus::services::IRunner;
using opencattus::services::Options;
using opencattus::services::Postfix;

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

using Response = std::variant<YesNoReply, ListReply, FieldReply,
    CollectListReply, MultiSelectionReply>;

struct ScriptedViewState {
    std::deque<Response> responses;
    std::vector<std::string> messages;
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

        if (const auto value = std::get_if<T>(&m_state->responses.front())) {
            auto out = *value;
            m_state->responses.pop_front();
            return out;
        }

        throw std::runtime_error(
            fmt::format("Unexpected scripted response type for {}", where));
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
        throw std::runtime_error("abort called on scripted view");
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

    std::pair<int, std::vector<std::string>> multipleSelectionMenu(
        const char* title, const char* message, const char* /*help*/,
        MultipleSelectionEntries /*items*/) override
    {
        recordMessage(title, message);
        const auto reply = pop<MultiSelectionReply>("multipleSelectionMenu");
        return { reply.status, reply.values };
    }

    std::string listMenuImpl(const char* title, const char* message,
        const std::vector<std::string>& items,
        const char* /*helpMessage*/) override
    {
        recordMessage(title, message);
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
        throw std::runtime_error(
            "Unexpected progressMenu call in scripted view");
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

auto hasInfinibandInterface() -> bool
{
    return std::ranges::any_of(Connection::fetchInterfaces(),
        [](const auto& interface) { return interface.starts_with("ib"); });
}

void initializePresenterTestEnvironment(
    ScriptedRunner::Outputs outputs = ScriptedRunner::Outputs {})
{
    opencattus::Singleton<const Options>::init(
        std::make_unique<const Options>(Options {
            .dryRun = false,
            .enableTUI = true,
        }));
    std::unique_ptr<IRunner> runner
        = std::make_unique<ScriptedRunner>(std::move(outputs));
    opencattus::Singleton<IRunner>::init(std::move(runner));
}

auto defaultRunnerOutputs() -> ScriptedRunner::Outputs
{
    return {
        { "timedatectl list-timezones --no-pager",
            { "America/Sao_Paulo", "Europe/Paris" } },
        { "locale -a", { "en_US.utf8", "pt_BR.utf8" } },
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
            fields({ "mail.cluster.example.com",
                "/etc/pki/tls/certs/cluster.example.com.cer",
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

    TEST_CASE("compute questionnaire presenters populate the cluster model")
    {
        initializePresenterTestEnvironment(defaultRunnerOutputs());

        auto model = std::make_unique<Cluster>();
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
            fields({ "n", "2", "192.168.30.101", "labroot", "labroot" }),
            fields(
                { "2", "16", "1", "65536", "admin", "secret", "1", "115200" }),
            fields({ "2" }),
            fields({ "52:54:00:00:20:11", "172.16.0.11" }),
            fields({ "52:54:00:00:20:12", "172.16.0.12" }),
            select("SLURM"),
            fields({ "batch", "dbroot", "slurmdb", "storagepass" }),
            yesNo(true),
            select("SASL"),
            fields({ "mail.cluster.example.com",
                "/etc/pki/tls/certs/cluster.example.com.cer",
                "/etc/pki/tls/private/cluster.example.com.key" }),
            fields(
                { "smtp.example.com", "587", "relayUser", "examplePassword" }),
        };

        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);

        PresenterNodesOperationalSystem(model, view);
        PresenterProvisioner(model, view);
        PresenterNodes(model, view);
        PresenterQueueSystem(model, view);
        PresenterMailSystem(model, view);

        CHECK(state->responses.empty());
        CHECK(model->getProvisioner() == Cluster::Provisioner::Confluent);
        CHECK(model->getHeadnode().getOS().getVersion() == "9.6");
        CHECK(model->getNodes().size() == 2);
        CHECK(model->slurmMariaDBRootPassword == "dbroot");
        CHECK(model->slurmDBPassword == "slurmdb");
        CHECK(model->slurmStoragePassword == "storagepass");
        REQUIRE(model->getMailSystem().has_value());
        CHECK(model->getMailSystem()->getProfile() == Postfix::Profile::SASL);
        CHECK(model->getMailSystem()->getSMTPServer().value()
            == "smtp.example.com");
        CHECK(model->getMailSystem()->getUsername().value() == "relayUser");

        model->dumpData(outputPath);
        AnswerFile answerfile(outputPath);

        CHECK(answerfile.system.provisioner == "confluent");
        CHECK(answerfile.system.version == "9.6");
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

    TEST_CASE("infiniband questionnaire persists an explicit OFED version")
    {
        initializePresenterTestEnvironment(defaultRunnerOutputs());

        if (!hasInfinibandInterface()) {
            MESSAGE("Skipping PresenterInfiniband TUI test: need an "
                    "Infiniband interface");
            return;
        }

        const auto interfaces = usableHostInterfaces();
        if (interfaces.empty()) {
            MESSAGE("Skipping PresenterInfiniband TUI test: need a usable "
                    "interface for the application network");
            return;
        }

        auto model = std::make_unique<Cluster>();
        auto state = std::make_shared<ScriptedViewState>();
        state->responses = {
            yesNo(true),
            select("Doca"),
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
        const auto isoDir = createTestIsoDirectory("opencattus-tui-install");
        const auto outputPath
            = tempPath("opencattus-tui-install-answerfile", "ini");

        auto state = std::make_shared<ScriptedViewState>();
        state->responses = {
            fields({ "demo", "acme", "admin@example.com" }),
            select("Text"),
            select("America"),
            select("Sao_Paulo"),
            collect({ "0.pool.ntp.org", "1.pool.ntp.org" }),
            select("en_US.utf8"),
            fields({ "headnode", "cluster.example.com" }),
            select(interfaces[0]),
            fields({ "192.168.124.10", "255.255.255.0", "192.168.124.1",
                "external.cluster.example.com", "1.1.1.1, 8.8.8.8" }),
            select(interfaces[1]),
            fields({ "192.168.30.254", "255.255.255.0", "192.168.30.1",
                "cluster.example.com", "9.9.9.9" }),
            yesNo(false),
            yesNo(false),
            fields({ isoDir.string() }),
            select("Rocky Linux"),
            select("Rocky-9.6-x86_64-dvd.iso"),
            fields({ "9.6", "x86_64" }),
            select("confluent"),
            fields({ "n", "2", "192.168.30.101", "labroot", "labroot" }),
            fields(
                { "1", "8", "2", "32768", "admin", "secret", "1", "115200" }),
            fields({ "2" }),
            fields({ "52:54:00:00:20:11", "172.16.0.11" }),
            fields({ "52:54:00:00:20:12", "172.16.0.12" }),
            select("SLURM"),
            fields({ "batch", "dbroot", "slurmdb", "storagepass" }),
            yesNo(false),
        };

        if (hasInfinibandInterface()) {
            state->responses.insert(
                state->responses.begin() + 12, yesNo(false));
        }

        std::unique_ptr<View> view = std::make_unique<ScriptedView>(state);
        PresenterInstall(model, view);

        CHECK_FALSE(view);
        CHECK(state->responses.empty());

        CHECK(model->getName() == "demo");
        CHECK(model->getCompanyName() == "acme");
        CHECK(model->getAdminMail() == "admin@example.com");
        CHECK(model->getHeadnode().getHostname() == "headnode");
        CHECK(model->getDomainName() == "cluster.example.com");
        CHECK(model->getTimezone().getTimezone() == "America/Sao_Paulo");
        CHECK(model->getLocale() == "en_US.utf8");
        CHECK(model->getProvisioner() == Cluster::Provisioner::Confluent);
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
        REQUIRE(answerfile.external.con_ip_addr.has_value());
        CHECK(answerfile.external.con_ip_addr->to_string() == "192.168.124.10");
        REQUIRE(answerfile.management.con_ip_addr.has_value());
        CHECK(
            answerfile.management.con_ip_addr->to_string() == "192.168.30.254");
        CHECK(answerfile.nodes.nodes.size() == 2);
        CHECK(answerfile.slurm.partition_name == "batch");
        CHECK(answerfile.slurm.slurmdb_password == "slurmdb");

        std::filesystem::remove_all(isoDir);
        std::filesystem::remove(outputPath);
    }
}
