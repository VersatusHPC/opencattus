/*
 * Copyright 2026 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <opencattus/services/tui_session.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <list>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/lexical_cast.hpp>
#include <fmt/format.h>

#include <opencattus/connection.h>
#include <opencattus/mailsystem/postfix.h>
#include <opencattus/models/pbs.h>
#include <opencattus/models/slurm.h>
#include <opencattus/network.h>
#include <opencattus/services/files.h>
#include <opencattus/services/log.h>
#include <opencattus/utils/enums.h>
#include <opencattus/utils/string.h>

#ifdef BUILD_TESTING
#include <doctest/doctest.h>
#else
#define DOCTEST_CONFIG_DISABLE
#include <doctest/doctest.h>
#endif

namespace opencattus::services::tui {
namespace {
    using boost::asio::ip::address;
    using models::CPU;
    using models::Node;
    using models::OS;
    using models::PBS;
    using opencattus::services::files::KeyFile;

    constexpr std::string_view metadataGroup = "tui";
    constexpr std::string_view metadataDraft = "draft";
    constexpr std::string_view metadataCompletedSteps = "completed_steps";
    constexpr std::string_view metadataLastCompletedStep
        = "last_completed_step";
    constexpr std::string_view metadataFormatVersion = "format_version";
    constexpr std::string_view metadataPendingDiskImageDownloadURL
        = "pending_disk_image_download_url";

    auto optionalString(const KeyFile& file, std::string_view group,
        std::string_view key) -> std::optional<std::string>
    {
        if (!file.hasGroup(group)) {
            return std::nullopt;
        }

        auto value = file.getStringOpt(std::string(group), std::string(key));
        if (!value.has_value() || value->empty()) {
            return std::nullopt;
        }

        return value;
    }

    auto splitValues(std::string_view raw) -> std::vector<std::string>
    {
        std::vector<std::string> values;
        boost::split(values, std::string(raw), boost::is_any_of(", "),
            boost::token_compress_on);

        values.erase(std::remove_if(values.begin(), values.end(),
                         [](const auto& value) { return value.empty(); }),
            values.end());
        return values;
    }

    auto parseBool(std::string_view raw) -> bool
    {
        const auto normalized
            = opencattus::utils::string::lower(std::string(raw));
        return normalized == "1" || normalized == "true" || normalized == "yes";
    }

    auto parseAddress(std::string_view raw) -> std::optional<address>
    {
        try {
            return boost::asio::ip::make_address(std::string(raw));
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }

    auto parseSize(std::string_view raw) -> std::optional<std::size_t>
    {
        try {
            return boost::lexical_cast<std::size_t>(std::string(raw));
        } catch (const boost::bad_lexical_cast&) {
            return std::nullopt;
        }
    }

    auto parsePort(std::string_view raw) -> std::optional<std::uint16_t>
    {
        try {
            return boost::lexical_cast<std::uint16_t>(std::string(raw));
        } catch (const boost::bad_lexical_cast&) {
            return std::nullopt;
        }
    }

    auto parseDistro(std::string raw) -> std::optional<OS::Distro>
    {
        raw = opencattus::utils::string::lower(raw);
        if (raw == "alma") {
            raw = "almalinux";
        }

        return opencattus::utils::enums::ofStringOpt<OS::Distro>(
            raw, opencattus::utils::enums::Case::Insensitive);
    }

    void applyInformation(models::Cluster& model, const KeyFile& file)
    {
        if (const auto value
            = optionalString(file, "information", "cluster_name")) {
            model.setName(*value);
        }
        if (const auto value
            = optionalString(file, "information", "company_name")) {
            model.setCompanyName(*value);
        }
        if (const auto value
            = optionalString(file, "information", "administrator_email")) {
            model.setAdminMail(*value);
        }
    }

    void applyTime(models::Cluster& model, const KeyFile& file)
    {
        if (const auto value = optionalString(file, "time", "timezone")) {
            model.setTimezone(*value);
        }
        if (const auto value = optionalString(file, "time", "timeserver")) {
            model.getTimezone().setTimeservers(*value);
        }
        if (const auto value = optionalString(file, "time", "locale")) {
            model.setLocale(*value);
        }
    }

    void applyHostname(models::Cluster& model, const KeyFile& file)
    {
        if (const auto value = optionalString(file, "hostname", "hostname")) {
            model.getHeadnode().setHostname(*value);
        }
        if (const auto value
            = optionalString(file, "hostname", "domain_name")) {
            model.setDomainName(*value);
        }
    }

    auto modelHasNetwork(models::Cluster& model, Network::Profile profile)
        -> bool
    {
        try {
            static_cast<void>(model.getNetwork(profile));
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    void applyNetwork(models::Cluster& model, const KeyFile& file,
        std::string_view section, Network::Profile profile, Network::Type type)
    {
        if (!file.hasGroup(section) || modelHasNetwork(model, profile)) {
            return;
        }

        const auto interface = optionalString(file, section, "interface");
        const auto ipAddress = optionalString(file, section, "ip_address");
        const auto subnetMask = optionalString(file, section, "subnet_mask");
        if (!interface || !ipAddress || !subnetMask) {
            return;
        }

        const auto parsedIP = parseAddress(*ipAddress);
        const auto parsedSubnet = parseAddress(*subnetMask);
        if (!parsedIP || !parsedSubnet) {
            LOG_WARN(
                "Skipping incomplete TUI draft network section {}", section);
            return;
        }

        auto network = std::make_unique<Network>(profile, type);
        Connection connection(network.get());
        connection.setInterface(*interface);
        connection.setAddress(*parsedIP);

        if (const auto mac = optionalString(file, section, "mac_address")) {
            connection.setMAC(*mac);
        }

        network->setSubnetMask(*parsedSubnet);
        network->setAddress(network->calculateAddress(*parsedIP));

        if (const auto gateway = optionalString(file, section, "gateway")) {
            if (const auto parsedGateway = parseAddress(*gateway)) {
                network->setGateway(*parsedGateway);
            }
        }
        if (const auto domain = optionalString(file, section, "domain_name")) {
            network->setDomainName(*domain);
        }
        if (const auto nameservers
            = optionalString(file, section, "nameservers")) {
            network->setNameservers(splitValues(*nameservers));
        }

        model.addNetwork(std::move(network));
        model.getHeadnode().addConnection(std::move(connection));
    }

    void applyNetworks(models::Cluster& model, const KeyFile& file)
    {
        applyNetwork(model, file, "network_external",
            Network::Profile::External, Network::Type::Ethernet);
        applyNetwork(model, file, "network_management",
            Network::Profile::Management, Network::Type::Ethernet);
        applyNetwork(model, file, "network_service", Network::Profile::Service,
            Network::Type::Ethernet);
        applyNetwork(model, file, "network_application",
            Network::Profile::Application, Network::Type::Infiniband);
    }

    void applySystem(models::Cluster& model, const KeyFile& file)
    {
        if (const auto diskImage
            = optionalString(file, "system", "disk_image")) {
            if (const auto pendingURL = optionalString(
                    file, metadataGroup, metadataPendingDiskImageDownloadURL)) {
                model.setPendingDiskImageDownload(*diskImage, *pendingURL);
            } else {
                try {
                    model.setDiskImage(*diskImage);
                } catch (const std::exception& ex) {
                    LOG_WARN("Skipping TUI draft disk image {}: {}", *diskImage,
                        ex.what());
                }
            }
        }

        const auto distro = optionalString(file, "system", "distro");
        const auto version = optionalString(file, "system", "version");
        if (distro && version) {
            if (auto parsedDistro = parseDistro(*distro)) {
                OS nodeOS;
                nodeOS.setArch(OS::Arch::x86_64);
                nodeOS.setFamily(OS::Family::Linux);
                nodeOS.setDistro(*parsedDistro);
                if (const auto kernel
                    = optionalString(file, "system", "kernel")) {
                    nodeOS.setKernel(*kernel);
                }
                nodeOS.setVersion(*version);
                model.setComputeNodeOS(nodeOS);
            }
        }

        if (const auto provisioner
            = optionalString(file, "system", "provisioner")) {
            const auto normalized
                = opencattus::utils::string::lower(*provisioner);
            if (normalized == "xcat") {
                model.setProvisioner(models::Cluster::Provisioner::xCAT);
            } else if (normalized == "confluent") {
                model.setProvisioner(models::Cluster::Provisioner::Confluent);
            }
        }
    }

    void applyRepositories(models::Cluster& model, const KeyFile& file)
    {
        if (const auto enabled
            = optionalString(file, "repositories", "enabled")) {
            model.setEnabledRepositories(splitValues(*enabled));
        }
    }

    void applyOFED(models::Cluster& model, const KeyFile& file)
    {
        if (!file.hasGroup("ofed")) {
            return;
        }

        const auto kind = optionalString(file, "ofed", "kind");
        if (!kind) {
            return;
        }

        const auto parsedKind
            = opencattus::utils::enums::ofStringOpt<OFED::Kind>(
                *kind, opencattus::utils::enums::Case::Insensitive);
        if (!parsedKind) {
            return;
        }

        model.setOFED(*parsedKind,
            optionalString(file, "ofed", "version").value_or("latest"));
    }

    void applyGenericNode(models::Cluster& model, const KeyFile& file)
    {
        if (!file.hasGroup("node")) {
            return;
        }

        if (const auto value = optionalString(file, "node", "prefix")) {
            model.nodePrefix = *value;
        }
        if (const auto value = optionalString(file, "node", "padding")) {
            if (const auto parsed = parseSize(*value)) {
                model.nodePadding = *parsed;
            }
        }
        if (const auto value = optionalString(file, "node", "node_ip")) {
            if (const auto parsed = parseAddress(*value)) {
                model.nodeStartIP = *parsed;
            }
        }
        if (const auto value
            = optionalString(file, "node", "node_root_password")) {
            model.nodeRootPassword = *value;
        }
        if (const auto value = optionalString(file, "node", "sockets")) {
            if (const auto parsed = parseSize(*value)) {
                model.nodeSockets = *parsed;
            }
        }
        if (const auto value
            = optionalString(file, "node", "cores_per_socket")) {
            if (const auto parsed = parseSize(*value)) {
                model.nodeCoresPerSocket = *parsed;
            }
        }
        if (const auto value
            = optionalString(file, "node", "threads_per_core")) {
            if (const auto parsed = parseSize(*value)) {
                model.nodeThreadsPerCore = *parsed;
            }
        }
        if (const auto value = optionalString(file, "node", "real_memory")) {
            if (const auto parsed = parseSize(*value)) {
                model.nodeRealMemory = *parsed;
            }
        }
        if (const auto value = optionalString(file, "node", "bmc_username")) {
            model.nodeBMCUsername = *value;
        }
        if (const auto value = optionalString(file, "node", "bmc_password")) {
            model.nodeBMCPassword = *value;
        }
        if (const auto value = optionalString(file, "node", "bmc_serialport")) {
            if (const auto parsed = parseSize(*value)) {
                model.nodeBMCSerialPort = *parsed;
            }
        }
        if (const auto value
            = optionalString(file, "node", "bmc_serialspeed")) {
            if (const auto parsed = parseSize(*value)) {
                model.nodeBMCSerialSpeed = *parsed;
            }
        }

        model.nodeCPUsPerNode = model.nodeSockets * model.nodeCoresPerSocket
            * model.nodeThreadsPerCore;
    }

    auto applySingleNode(models::Cluster& model, const KeyFile& file,
        std::string_view section) -> bool
    {
        const auto hostname = optionalString(file, section, "hostname");
        const auto nodeIP = optionalString(file, section, "node_ip");
        const auto mac = optionalString(file, section, "mac_address");
        if (!hostname || !nodeIP || !mac) {
            return false;
        }

        Network* managementNetwork = nullptr;
        try {
            managementNetwork = &model.getNetwork(Network::Profile::Management);
        } catch (const std::exception&) {
            return false;
        }

        const auto parsedNodeIP = parseAddress(*nodeIP);
        if (!parsedNodeIP) {
            return false;
        }

        const auto sockets = parseSize(optionalString(file, section, "sockets")
                .value_or(std::to_string(model.nodeSockets)));
        const auto coresPerSocket
            = parseSize(optionalString(file, section, "cores_per_socket")
                    .value_or(std::to_string(model.nodeCoresPerSocket)));
        const auto threadsPerCore
            = parseSize(optionalString(file, section, "threads_per_core")
                    .value_or(std::to_string(model.nodeThreadsPerCore)));
        if (!sockets || !coresPerSocket || !threadsPerCore) {
            return false;
        }

        CPU cpu(*sockets, *coresPerSocket, *threadsPerCore);
        auto os = model.getComputeNodeOS();

        std::list<Connection> connections;
        auto& connection = connections.emplace_back(managementNetwork);
        connection.setMAC(*mac);
        connection.setAddress(*parsedNodeIP);

        std::optional<BMC> bmc = std::nullopt;
        if (const auto bmcAddress
            = optionalString(file, section, "bmc_address")) {
            bmc = BMC(*bmcAddress,
                optionalString(file, section, "bmc_username")
                    .value_or(model.nodeBMCUsername),
                optionalString(file, section, "bmc_password")
                    .value_or(model.nodeBMCPassword),
                parseSize(
                    optionalString(file, section, "bmc_serialport")
                        .value_or(std::to_string(model.nodeBMCSerialPort)))
                    .value_or(model.nodeBMCSerialPort),
                parseSize(
                    optionalString(file, section, "bmc_serialspeed")
                        .value_or(std::to_string(model.nodeBMCSerialSpeed)))
                    .value_or(model.nodeBMCSerialSpeed),
                BMC::kind::IPMI);
        }

        Node node(*hostname, os, cpu, std::move(connections), bmc);
        auto prefix = optionalString(file, section, "prefix");
        if (!prefix && !model.nodePrefix.empty()) {
            prefix = model.nodePrefix;
        }
        node.setPrefix(prefix);
        if (const auto padding = optionalString(file, section, "padding")) {
            node.setPadding(parseSize(*padding));
        } else if (model.nodePadding != 0) {
            node.setPadding(model.nodePadding);
        }
        node.setNodeStartIp(*parsedNodeIP);
        node.setMACAddress(*mac);
        auto nodeRootPassword
            = optionalString(file, section, "node_root_password");
        if (!nodeRootPassword && !model.nodeRootPassword.empty()) {
            nodeRootPassword = model.nodeRootPassword;
        }
        node.setNodeRootPassword(nodeRootPassword);

        model.addNode(node);
        return true;
    }

    void applyNodes(models::Cluster& model, const KeyFile& file)
    {
        applyGenericNode(model, file);

        std::size_t loaded = 0;
        for (const auto& group : file.listAllPrefixedEntries("node.")) {
            if (applySingleNode(model, file, group)) {
                ++loaded;
            }
        }

        if (loaded > 0) {
            model.nodeQuantity = loaded;
        }
    }

    void applyQueueSystem(models::Cluster& model, const KeyFile& file)
    {
        if (file.hasGroup("pbs")) {
            model.setQueueSystem(models::QueueSystem::Kind::PBS);
            const auto executionPlace
                = optionalString(file, "pbs", "execution_place")
                      .value_or("Shared");
            if (auto parsed
                = opencattus::utils::enums::ofStringOpt<PBS::ExecutionPlace>(
                    executionPlace,
                    opencattus::utils::enums::Case::Insensitive)) {
                const auto& pbs
                    = dynamic_cast<PBS*>(model.getQueueSystem().value().get());
                pbs->setExecutionPlace(*parsed);
            }
            return;
        }

        if (!file.hasGroup("slurm")) {
            return;
        }

        model.setQueueSystem(models::QueueSystem::Kind::SLURM);
        if (const auto& queue = model.getQueueSystem()) {
            queue.value()->setDefaultQueue(
                optionalString(file, "slurm", "partition_name")
                    .value_or("execution"));
        }
        model.slurmMariaDBRootPassword
            = optionalString(file, "slurm", "mariadb_root_password")
                  .value_or("");
        model.slurmDBPassword
            = optionalString(file, "slurm", "slurmdb_password").value_or("");
        model.slurmStoragePassword
            = optionalString(file, "slurm", "storage_password").value_or("");
    }

    void applyPostfix(models::Cluster& model, const KeyFile& file)
    {
        if (!file.hasGroup("postfix")) {
            return;
        }

        const auto rawProfile = optionalString(file, "postfix", "profile");
        if (!rawProfile) {
            return;
        }

        const auto profile
            = opencattus::utils::enums::ofStringOpt<Postfix::Profile>(
                *rawProfile, opencattus::utils::enums::Case::Insensitive);
        if (!profile) {
            return;
        }

        model.setMailSystem(*profile);
        auto& mailSystem = model.getMailSystem().value();
        mailSystem.setHostname(std::string(model.getHeadnode().getHostname()));
        mailSystem.setDomain(model.getDomainName());

        if (const auto destination
            = optionalString(file, "postfix", "destination")) {
            mailSystem.setDestination(splitValues(*destination));
        }
        if (const auto cert
            = optionalString(file, "postfix", "smtpd_tls_cert_file")) {
            mailSystem.setCertFile(std::filesystem::path(*cert));
        }
        if (const auto key
            = optionalString(file, "postfix", "smtpd_tls_key_file")) {
            mailSystem.setKeyFile(std::filesystem::path(*key));
        }

        const auto relaySection = *profile == Postfix::Profile::SASL
            ? "postfix.sasl"
            : "postfix.relay";
        if (*profile == Postfix::Profile::Relay
            || *profile == Postfix::Profile::SASL) {
            mailSystem.setSMTPServer(
                optionalString(file, relaySection, "server"));
            if (const auto port = optionalString(file, relaySection, "port")) {
                mailSystem.setPort(parsePort(*port));
            }
        }

        if (*profile == Postfix::Profile::SASL) {
            mailSystem.setUsername(
                optionalString(file, "postfix.sasl", "username"));
            mailSystem.setPassword(
                optionalString(file, "postfix.sasl", "password"));
        }
    }
}

auto defaultAnswerfilePath(const Options& options) -> std::filesystem::path
{
    if (!options.dumpAnswerfile.empty()) {
        return options.dumpAnswerfile;
    }

    return std::filesystem::current_path() / "opencattus-tui.ini";
}

auto defaultDraftPath(const Options& options) -> std::filesystem::path
{
    if (!options.tuiDraft.empty()) {
        return options.tuiDraft;
    }

    auto path = defaultAnswerfilePath(options);
    path.concat(".draft");
    return path;
}

auto loadDraftState(const std::filesystem::path& path) -> DraftState
{
    DraftState state;
    if (!files::exists(path)) {
        return state;
    }

    const KeyFile file(path);
    if (!file.hasGroup(metadataGroup)) {
        return state;
    }

    if (const auto draft = optionalString(file, metadataGroup, metadataDraft)) {
        state.draft = parseBool(*draft);
    }
    if (const auto completed
        = optionalString(file, metadataGroup, metadataCompletedSteps)) {
        state.completedSteps = splitValues(*completed);
    }
    state.lastCompletedStep
        = optionalString(file, metadataGroup, metadataLastCompletedStep);

    return state;
}

auto isDraftAnswerfile(const std::filesystem::path& path) -> bool
{
    return loadDraftState(path).draft;
}

auto completedStepSet(const DraftState& state) -> std::set<std::string>
{
    return { state.completedSteps.begin(), state.completedSteps.end() };
}

void applyDraftToModel(
    models::Cluster& model, const std::filesystem::path& path)
{
    if (!files::exists(path)) {
        return;
    }

    const KeyFile file(path);
    applyInformation(model, file);
    applyTime(model, file);
    applyHostname(model, file);
    applyNetworks(model, file);
    applySystem(model, file);
    applyRepositories(model, file);
    applyOFED(model, file);
    applyNodes(model, file);
    applyQueueSystem(model, file);
    applyPostfix(model, file);
}

void writeDraft(models::Cluster& model, const std::filesystem::path& path,
    const std::vector<std::string>& completedSteps, bool complete)
{
    model.dumpData(path);

    std::error_code permissionError;
    std::filesystem::permissions(path,
        std::filesystem::perms::owner_read
            | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace, permissionError);
    if (permissionError) {
        LOG_WARN("Failed to restrict TUI answerfile permissions on {}: {}",
            path.string(), permissionError.message());
    }

    KeyFile file(path);
    file.setString(std::string(metadataGroup),
        std::string(metadataFormatVersion), std::string("1"));
    file.setBoolean(
        std::string(metadataGroup), std::string(metadataDraft), !complete);
    file.setString(std::string(metadataGroup),
        std::string(metadataCompletedSteps),
        boost::algorithm::join(completedSteps, ", "));
    if (const auto pendingDownload = model.getPendingDiskImageDownloadURL()) {
        file.setString(std::string(metadataGroup),
            std::string(metadataPendingDiskImageDownloadURL), *pendingDownload);
    }
    if (!completedSteps.empty()) {
        file.setString(std::string(metadataGroup),
            std::string(metadataLastCompletedStep), completedSteps.back());
    }
    file.save();

    LOG_INFO("{} TUI answerfile {}", complete ? "Completed" : "Saved draft",
        path.string());
}

TEST_CASE("TUI draft state reads metadata without loading answerfile sections")
{
    const auto path = std::filesystem::temp_directory_path()
        / "opencattus-tui-session-test.ini";

    {
        KeyFile file(path, false);
        file.setBoolean("tui", "draft", true);
        file.setString("tui", "completed_steps", std::string("general, time"));
        file.setString("tui", "last_completed_step", std::string("time"));
        file.save();
    }

    const auto state = loadDraftState(path);
    CHECK(state.draft);
    CHECK(
        state.completedSteps == std::vector<std::string> { "general", "time" });
    REQUIRE(state.lastCompletedStep.has_value());
    CHECK(*state.lastCompletedStep == "time");

    files::remove(path);
}

TEST_CASE("TUI draft path defaults to the current directory")
{
    Options options {};

    CHECK(defaultDraftPath(options)
        == std::filesystem::current_path() / "opencattus-tui.ini.draft");
}

TEST_CASE("TUI answerfile path defaults to the current directory")
{
    Options options {};

    CHECK(defaultAnswerfilePath(options)
        == std::filesystem::current_path() / "opencattus-tui.ini");
}

TEST_CASE("TUI draft path follows the dump answerfile path")
{
    Options options {};
    options.dumpAnswerfile = "opencattus-tui.ini";

    CHECK(defaultDraftPath(options)
        == std::filesystem::path("opencattus-tui.ini.draft"));
}

TEST_CASE("TUI answerfile path follows the dump answerfile path")
{
    Options options {};
    options.dumpAnswerfile = "custom.ini";

    CHECK(
        defaultAnswerfilePath(options) == std::filesystem::path("custom.ini"));
}

TEST_CASE("TUI draft persists pending disk image download")
{
    const auto path = std::filesystem::temp_directory_path()
        / "opencattus-tui-pending-download-test.ini";

    models::Cluster model;
    model.setName("demo");
    model.setCompanyName("acme");
    model.setAdminMail("admin@example.com");
    model.getHeadnode().setHostname(std::string("headnode"));
    model.setDomainName("cluster.example.com");
    model.setLocale("en_US.utf8");
    model.setPendingDiskImageDownload("/root/Rocky-9.6-x86_64-dvd.iso",
        "https://download.rockylinux.org/pub/rocky/9/isos/x86_64/"
        "Rocky-9.6-x86_64-dvd.iso");

    writeDraft(model, path, { "os" });

    const KeyFile file(path);
    CHECK(file.getString("tui", "pending_disk_image_download_url")
        == "https://download.rockylinux.org/pub/rocky/9/isos/x86_64/"
           "Rocky-9.6-x86_64-dvd.iso");

    models::Cluster resumed;
    applyDraftToModel(resumed, path);
    CHECK(resumed.getDiskImage().getPath()
        == std::filesystem::path("/root/Rocky-9.6-x86_64-dvd.iso"));
    REQUIRE(resumed.getPendingDiskImageDownloadURL().has_value());
    CHECK(resumed.getPendingDiskImageDownloadURL().value()
        == "https://download.rockylinux.org/pub/rocky/9/isos/x86_64/"
           "Rocky-9.6-x86_64-dvd.iso");

    files::remove(path);
}

} // namespace opencattus::services::tui
