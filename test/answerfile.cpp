/*
 * Copyright 2026 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <doctest/doctest.h>

#include <fstream>
#include <opencattus/connection.h>
#include <opencattus/models/answerfile.h>
#include <opencattus/models/cluster.h>
#include <opencattus/patterns/singleton.h>
#include <opencattus/services/files.h>
#include <opencattus/services/options.h>
#include <opencattus/services/runner.h>
#include <string>

namespace {
using opencattus::models::AnswerFile;
using opencattus::models::Cluster;
using opencattus::models::QueueSystem;
using opencattus::services::DryRunner;
using opencattus::services::Options;

void initializeOptionsSingleton()
{
    const char* argv[] = { "OpenCATTUS-tests", "--dry" };
    auto options = opencattus::services::options::factory(2, argv);
    opencattus::Singleton<const Options>::init(
        std::unique_ptr<const Options>(options.release()));
    std::unique_ptr<opencattus::services::IRunner> runner
        = std::make_unique<DryRunner>();
    opencattus::Singleton<opencattus::services::IRunner>::init(
        std::move(runner));
}

auto tempAnswerfilePath(std::string_view stem) -> std::filesystem::path
{
    return std::filesystem::temp_directory_path() / fmt::format("{}.ini", stem);
}

auto tempIsoPath(std::string_view stem) -> std::filesystem::path
{
    return std::filesystem::temp_directory_path() / fmt::format("{}.iso", stem);
}

auto firstHostInterfaces() -> std::vector<std::string>
{
    return Connection::fetchInterfaces();
}

void replaceInFile(const std::filesystem::path& path, std::string_view from,
    std::string_view to)
{
    auto contents = opencattus::services::files::read(path);
    const auto pos = contents.find(from);
    REQUIRE(pos != std::string::npos);
    contents.replace(pos, from.size(), to);
    opencattus::services::files::write(path, contents);
}

void writeAnswerfile(const std::filesystem::path& path,
    const std::filesystem::path& diskImagePath,
    std::string_view managementInterface, std::string_view serviceInterface,
    std::optional<std::string_view> applicationInterface = std::nullopt,
    bool includeServiceNetwork = true,
    std::string_view provisioner = "confluent",
    std::optional<std::string_view> serviceNameservers = std::nullopt,
    std::string_view distro = "rocky", std::string_view version = "9.7",
    std::optional<std::string_view> enabledRepositories = std::nullopt,
    bool includeNodeBMC = true)
{
    std::ofstream out(path);
    REQUIRE(out.is_open());

    out << "[information]\n"
        << "cluster_name=opencattus\n"
        << "company_name=opencattus-enterprises\n"
        << "administrator_email=foo@example.com\n\n"
        << "[time]\n"
        << "timezone=UTC\n"
        << "timeserver=pool.ntp.org\n"
        << "locale=en_US.utf8\n\n"
        << "[hostname]\n"
        << "hostname=opencattus\n"
        << "domain_name=cluster.example.com\n\n"
        << "[network_external]\n"
        << "interface=" << managementInterface << '\n'
        << "ip_address=192.168.124.10\n"
        << "subnet_mask=255.255.255.0\n"
        << "domain_name=cluster.external.example.com\n\n"
        << "[network_management]\n"
        << "interface=" << managementInterface << '\n'
        << "ip_address=192.168.30.254\n"
        << "subnet_mask=255.255.255.0\n"
        << "domain_name=cluster.management.example.com\n";

    if (includeServiceNetwork) {
        out << "\n[network_service]\n"
            << "interface=" << serviceInterface << '\n'
            << "ip_address=192.168.31.254\n"
            << "subnet_mask=255.255.255.0\n"
            << "domain_name=cluster.service.example.com\n";

        if (serviceNameservers.has_value()) {
            out << "nameservers=" << serviceNameservers.value() << '\n';
        }
    }

    if (applicationInterface.has_value()) {
        out << "\n[network_application]\n"
            << "interface=" << applicationInterface.value() << '\n'
            << "ip_address=172.16.0.254\n"
            << "subnet_mask=255.255.255.0\n"
            << "domain_name=cluster.application.example.com\n";
    }

    if (enabledRepositories.has_value()) {
        out << "\n[repositories]\n"
            << "enabled=" << enabledRepositories.value() << '\n';
    }

    out << "\n[slurm]\n"
        << "mariadb_root_password=LabMariadbRoot!23\n"
        << "slurmdb_password=LabSlurmDb!23\n"
        << "storage_password=LabStorage!23\n"
        << "partition_name=batch\n\n"
        << "[system]\n"
        << "disk_image=" << diskImagePath.string() << '\n'
        << "distro=" << distro << '\n'
        << "version=" << version << '\n'
        << "provisioner=" << provisioner << "\n\n"
        << "[node]\n"
        << "prefix=n\n"
        << "padding=2\n"
        << "node_ip=192.168.30.1\n"
        << "node_root_password=labroot\n"
        << "sockets=1\n"
        << "cpus_per_node=2\n"
        << "cores_per_socket=2\n"
        << "threads_per_core=1\n"
        << "real_memory=4096\n"
        << "bmc_username=admin\n"
        << "bmc_password=admin\n"
        << "bmc_serialport=0\n"
        << "bmc_serialspeed=9600\n\n"
        << "[node.1]\n"
        << "hostname=n01\n"
        << "mac_address=52:54:00:00:20:11\n"
        << "node_ip=192.168.30.1\n";

    if (includeNodeBMC) {
        out << "bmc_address=192.168.31.101\n";
    }
}

void appendOFEDSection(const std::filesystem::path& path, std::string_view kind,
    std::string_view version = "latest")
{
    std::ofstream out(path, std::ios::app);
    REQUIRE(out.is_open());
    out << "\n[ofed]\n"
        << "kind=" << kind << '\n'
        << "version=" << version << '\n';
}

void appendPostfixRelaySection(
    const std::filesystem::path& path, std::string_view port)
{
    std::ofstream out(path, std::ios::app);
    REQUIRE(out.is_open());
    out << "\n[postfix]\n"
        << "profile=Relay\n"
        << "destination=cluster.example.com\n\n"
        << "[postfix.relay]\n"
        << "server=smtp.example.com\n"
        << "port=" << port << '\n';
}

void appendPBSSection(const std::filesystem::path& path,
    std::string_view executionPlace = "Scatter")
{
    std::ofstream out(path, std::ios::app);
    REQUIRE(out.is_open());
    out << "\n[pbs]\n"
        << "execution_place=" << executionPlace << '\n';
}

void replaceSlurmWithPBS(const std::filesystem::path& path,
    std::string_view executionPlace = "Scatter")
{
    replaceInFile(path,
        "[slurm]\n"
        "mariadb_root_password=LabMariadbRoot!23\n"
        "slurmdb_password=LabSlurmDb!23\n"
        "storage_password=LabStorage!23\n"
        "partition_name=batch\n\n",
        fmt::format("[pbs]\nexecution_place={}\n\n", executionPlace));
}

void appendSecondNodeSection(const std::filesystem::path& path)
{
    std::ofstream out(path, std::ios::app);
    REQUIRE(out.is_open());
    out << "\n[node.2]\n"
        << "hostname=n02\n"
        << "mac_address=52:54:00:00:20:12\n"
        << "node_ip=192.168.30.2\n"
        << "bmc_address=192.168.31.102\n";
}
}

TEST_SUITE("opencattus::models::answerfile")
{
    TEST_CASE("loadOptions loads the service network section")
    {
        initializeOptionsSingleton();

        const auto interfaces = firstHostInterfaces();
        REQUIRE_FALSE(interfaces.empty());

        const auto answerfilePath
            = tempAnswerfilePath("opencattus-answerfile-service-network");
        const auto diskImagePath
            = tempIsoPath("opencattus-answerfile-service-network");
        std::ofstream(diskImagePath).close();
        writeAnswerfile(answerfilePath, diskImagePath, interfaces.front(),
            interfaces.front());

        try {
            AnswerFile answerfile(answerfilePath);

            REQUIRE(answerfile.service.con_interface.has_value());
            CHECK(
                answerfile.service.con_interface.value() == interfaces.front());
            REQUIRE(answerfile.service.con_ip_addr.has_value());
            CHECK(answerfile.service.con_ip_addr->to_string()
                == "192.168.31.254");
            REQUIRE(answerfile.service.domain_name.has_value());
            CHECK(answerfile.service.domain_name.value()
                == "cluster.service.example.com");
        } catch (const std::exception& e) {
            FAIL(std::string(e.what()));
        } catch (...) {
            FAIL("non-std exception while loading answerfile");
        }

        std::filesystem::remove(answerfilePath);
        std::filesystem::remove(diskImagePath);
    }

    TEST_CASE("loadOptions leaves the service network unset when the section "
              "is absent")
    {
        initializeOptionsSingleton();

        const auto interfaces = firstHostInterfaces();
        REQUIRE_FALSE(interfaces.empty());

        const auto answerfilePath
            = tempAnswerfilePath("opencattus-answerfile-no-service-network");
        const auto diskImagePath
            = tempIsoPath("opencattus-answerfile-no-service-network");
        std::ofstream(diskImagePath).close();
        writeAnswerfile(answerfilePath, diskImagePath, interfaces.front(),
            interfaces.front(), std::nullopt, false);

        try {
            AnswerFile answerfile(answerfilePath);

            CHECK_FALSE(answerfile.service.con_interface.has_value());
            CHECK_FALSE(answerfile.service.con_ip_addr.has_value());
            CHECK_FALSE(answerfile.service.domain_name.has_value());
            CHECK_FALSE(answerfile.service.nameservers.has_value());
        } catch (const std::exception& e) {
            FAIL(std::string(e.what()));
        } catch (...) {
            FAIL("non-std exception while loading answerfile without service "
                 "network");
        }

        std::filesystem::remove(answerfilePath);
        std::filesystem::remove(diskImagePath);
    }

    TEST_CASE("loadOptions splits nameservers for the service network")
    {
        initializeOptionsSingleton();

        const auto interfaces = firstHostInterfaces();
        REQUIRE_FALSE(interfaces.empty());

        const auto answerfilePath
            = tempAnswerfilePath("opencattus-answerfile-service-nameservers");
        const auto diskImagePath
            = tempIsoPath("opencattus-answerfile-service-nameservers");
        std::ofstream(diskImagePath).close();
        writeAnswerfile(answerfilePath, diskImagePath, interfaces.front(),
            interfaces.front(), std::nullopt, true, "confluent",
            "1.1.1.1, 8.8.8.8 9.9.9.9");

        try {
            AnswerFile answerfile(answerfilePath);

            REQUIRE(answerfile.service.nameservers.has_value());
            CHECK(answerfile.service.nameservers.value()
                == std::vector<std::string> {
                    "1.1.1.1", "8.8.8.8", "9.9.9.9" });
        } catch (const std::exception& e) {
            FAIL(std::string(e.what()));
        } catch (...) {
            FAIL("non-std exception while parsing service nameservers");
        }

        std::filesystem::remove(answerfilePath);
        std::filesystem::remove(diskImagePath);
    }

    TEST_CASE("loadOptions rejects non-contiguous subnet masks")
    {
        initializeOptionsSingleton();

        const auto interfaces = firstHostInterfaces();
        REQUIRE_FALSE(interfaces.empty());

        const auto answerfilePath
            = tempAnswerfilePath("opencattus-answerfile-invalid-mask");
        const auto diskImagePath
            = tempIsoPath("opencattus-answerfile-invalid-mask");
        std::ofstream(diskImagePath).close();
        writeAnswerfile(answerfilePath, diskImagePath, interfaces.front(),
            interfaces.front());
        replaceInFile(answerfilePath, "subnet_mask=255.255.255.0",
            "subnet_mask=255.0.255.0");

        CHECK_THROWS_WITH_AS(AnswerFile { answerfilePath },
            doctest::Contains(
                "Network section 'network_external' field 'subnet_mask' "
                "validation failed"),
            std::invalid_argument);

        std::filesystem::remove(answerfilePath);
        std::filesystem::remove(diskImagePath);
    }

    TEST_CASE("loadOptions rejects gateways outside the selected subnet")
    {
        initializeOptionsSingleton();

        const auto interfaces = firstHostInterfaces();
        REQUIRE_FALSE(interfaces.empty());

        const auto answerfilePath
            = tempAnswerfilePath("opencattus-answerfile-gateway-outside");
        const auto diskImagePath
            = tempIsoPath("opencattus-answerfile-gateway-outside");
        std::ofstream(diskImagePath).close();
        writeAnswerfile(answerfilePath, diskImagePath, interfaces.front(),
            interfaces.front());
        replaceInFile(answerfilePath,
            "domain_name=cluster.management.example.com\n",
            "domain_name=cluster.management.example.com\n"
            "gateway=10.20.30.1\n");

        CHECK_THROWS_WITH_AS(AnswerFile { answerfilePath },
            doctest::Contains(
                "Network section 'network_management' field 'gateway' "
                "validation failed"),
            std::invalid_argument);

        std::filesystem::remove(answerfilePath);
        std::filesystem::remove(diskImagePath);
    }

    TEST_CASE("loadOptions rejects node and BMC address collisions")
    {
        initializeOptionsSingleton();

        const auto interfaces = firstHostInterfaces();
        REQUIRE_FALSE(interfaces.empty());

        const auto answerfilePath
            = tempAnswerfilePath("opencattus-answerfile-node-bmc-collision");
        const auto diskImagePath
            = tempIsoPath("opencattus-answerfile-node-bmc-collision");
        std::ofstream(diskImagePath).close();
        writeAnswerfile(answerfilePath, diskImagePath, interfaces.front(),
            interfaces.front());
        replaceInFile(answerfilePath, "bmc_address=192.168.31.101",
            "bmc_address=192.168.30.1");

        CHECK_THROWS_WITH_AS(AnswerFile { answerfilePath },
            doctest::Contains("Duplicate node/BMC address '192.168.30.1'"),
            std::invalid_argument);

        std::filesystem::remove(answerfilePath);
        std::filesystem::remove(diskImagePath);
    }

    TEST_CASE("loadOptions rejects duplicate node MAC addresses")
    {
        initializeOptionsSingleton();

        const auto interfaces = firstHostInterfaces();
        REQUIRE_FALSE(interfaces.empty());

        const auto answerfilePath
            = tempAnswerfilePath("opencattus-answerfile-duplicate-mac");
        const auto diskImagePath
            = tempIsoPath("opencattus-answerfile-duplicate-mac");
        std::ofstream(diskImagePath).close();
        writeAnswerfile(answerfilePath, diskImagePath, interfaces.front(),
            interfaces.front());
        appendSecondNodeSection(answerfilePath);
        replaceInFile(answerfilePath, "mac_address=52:54:00:00:20:12",
            "mac_address=52:54:00:00:20:11");

        CHECK_THROWS_WITH_AS(AnswerFile { answerfilePath },
            doctest::Contains("Duplicate mac_address '52:54:00:00:20:11'"),
            std::invalid_argument);

        std::filesystem::remove(answerfilePath);
        std::filesystem::remove(diskImagePath);
    }

    TEST_CASE("loadOptions rejects unsupported provisioners")
    {
        initializeOptionsSingleton();

        const auto interfaces = firstHostInterfaces();
        REQUIRE_FALSE(interfaces.empty());

        const auto answerfilePath
            = tempAnswerfilePath("opencattus-answerfile-invalid-provisioner");
        const auto diskImagePath
            = tempIsoPath("opencattus-answerfile-invalid-provisioner");
        std::ofstream(diskImagePath).close();
        writeAnswerfile(answerfilePath, diskImagePath, interfaces.front(),
            interfaces.front(), std::nullopt, true, "warewulf");

        CHECK_THROWS_WITH_AS(AnswerFile { answerfilePath },
            doctest::Contains(
                "Section 'system' field 'provisioner' validation failed"),
            std::invalid_argument);

        std::filesystem::remove(answerfilePath);
        std::filesystem::remove(diskImagePath);
    }

    TEST_CASE("loadOptions rejects invalid generic node numeric fields")
    {
        initializeOptionsSingleton();

        const auto interfaces = firstHostInterfaces();
        REQUIRE_FALSE(interfaces.empty());

        const auto answerfilePath
            = tempAnswerfilePath("opencattus-answerfile-invalid-node-number");
        const auto diskImagePath
            = tempIsoPath("opencattus-answerfile-invalid-node-number");
        std::ofstream(diskImagePath).close();
        writeAnswerfile(answerfilePath, diskImagePath, interfaces.front(),
            interfaces.front());
        replaceInFile(answerfilePath, "sockets=1", "sockets=two");

        CHECK_THROWS_WITH_AS(AnswerFile { answerfilePath },
            doctest::Contains(
                "Section 'node' field 'sockets' validation failed"),
            std::invalid_argument);

        std::filesystem::remove(answerfilePath);
        std::filesystem::remove(diskImagePath);
    }

    TEST_CASE("loadOptions rejects invalid node-specific numeric fields")
    {
        initializeOptionsSingleton();

        const auto interfaces = firstHostInterfaces();
        REQUIRE_FALSE(interfaces.empty());

        const auto answerfilePath
            = tempAnswerfilePath("opencattus-answerfile-invalid-node-override");
        const auto diskImagePath
            = tempIsoPath("opencattus-answerfile-invalid-node-override");
        std::ofstream(diskImagePath).close();
        writeAnswerfile(answerfilePath, diskImagePath, interfaces.front(),
            interfaces.front());
        replaceInFile(answerfilePath,
            "[node.1]\n"
            "hostname=n01\n"
            "mac_address=52:54:00:00:20:11\n"
            "node_ip=192.168.30.1\n",
            "[node.1]\n"
            "hostname=n01\n"
            "mac_address=52:54:00:00:20:11\n"
            "node_ip=192.168.30.1\n"
            "sockets=two\n");

        CHECK_THROWS_WITH_AS(AnswerFile { answerfilePath },
            doctest::Contains(
                "Section 'node.1' field 'sockets' validation failed"),
            std::invalid_argument);

        std::filesystem::remove(answerfilePath);
        std::filesystem::remove(diskImagePath);
    }

    TEST_CASE("loadOptions rejects invalid postfix profiles")
    {
        initializeOptionsSingleton();

        const auto interfaces = firstHostInterfaces();
        REQUIRE_FALSE(interfaces.empty());

        const auto answerfilePath = tempAnswerfilePath(
            "opencattus-answerfile-invalid-postfix-profile");
        const auto diskImagePath
            = tempIsoPath("opencattus-answerfile-invalid-postfix-profile");
        std::ofstream(diskImagePath).close();
        writeAnswerfile(answerfilePath, diskImagePath, interfaces.front(),
            interfaces.front());

        {
            std::ofstream out(answerfilePath, std::ios::app);
            REQUIRE(out.is_open());
            out << "\n[postfix]\n"
                << "profile=SMTP\n";
        }

        CHECK_THROWS_WITH_AS(AnswerFile { answerfilePath },
            doctest::Contains(
                "Section 'postfix' field 'profile' validation failed"),
            std::invalid_argument);

        std::filesystem::remove(answerfilePath);
        std::filesystem::remove(diskImagePath);
    }

    TEST_CASE("loadOptions rejects invalid postfix relay ports")
    {
        initializeOptionsSingleton();

        const auto interfaces = firstHostInterfaces();
        REQUIRE_FALSE(interfaces.empty());

        const auto answerfilePath
            = tempAnswerfilePath("opencattus-answerfile-invalid-postfix-port");
        const auto diskImagePath
            = tempIsoPath("opencattus-answerfile-invalid-postfix-port");
        std::ofstream(diskImagePath).close();
        writeAnswerfile(answerfilePath, diskImagePath, interfaces.front(),
            interfaces.front());
        appendPostfixRelaySection(answerfilePath, "smtp");

        CHECK_THROWS_WITH_AS(AnswerFile { answerfilePath },
            doctest::Contains(
                "Section 'postfix.relay' field 'port' validation failed"),
            std::invalid_argument);

        std::filesystem::remove(answerfilePath);
        std::filesystem::remove(diskImagePath);
    }

    TEST_CASE("loadOptions rejects invalid OFED kinds")
    {
        initializeOptionsSingleton();

        const auto interfaces = firstHostInterfaces();
        REQUIRE_FALSE(interfaces.empty());

        const auto answerfilePath
            = tempAnswerfilePath("opencattus-answerfile-invalid-ofed-kind");
        const auto diskImagePath
            = tempIsoPath("opencattus-answerfile-invalid-ofed-kind");
        std::ofstream(diskImagePath).close();
        writeAnswerfile(answerfilePath, diskImagePath, interfaces.front(),
            interfaces.front());
        appendOFEDSection(answerfilePath, "roce", "latest");

        CHECK_THROWS_WITH_AS(AnswerFile { answerfilePath },
            doctest::Contains("Section 'ofed' field 'kind' validation failed"),
            std::invalid_argument);

        std::filesystem::remove(answerfilePath);
        std::filesystem::remove(diskImagePath);
    }

    TEST_CASE("loadOptions rejects mixed queue system sections")
    {
        initializeOptionsSingleton();

        const auto interfaces = firstHostInterfaces();
        REQUIRE_FALSE(interfaces.empty());

        const auto answerfilePath
            = tempAnswerfilePath("opencattus-answerfile-mixed-queues");
        const auto diskImagePath
            = tempIsoPath("opencattus-answerfile-mixed-queues");
        std::ofstream(diskImagePath).close();
        writeAnswerfile(answerfilePath, diskImagePath, interfaces.front(),
            interfaces.front());
        appendPBSSection(answerfilePath);

        CHECK_THROWS_WITH_AS(AnswerFile { answerfilePath },
            doctest::Contains("Queue system validation failed"),
            std::invalid_argument);

        std::filesystem::remove(answerfilePath);
        std::filesystem::remove(diskImagePath);
    }

    TEST_CASE("loadOptions rejects invalid PBS execution places")
    {
        initializeOptionsSingleton();

        const auto interfaces = firstHostInterfaces();
        REQUIRE_FALSE(interfaces.empty());

        const auto answerfilePath
            = tempAnswerfilePath("opencattus-answerfile-invalid-pbs");
        const auto diskImagePath
            = tempIsoPath("opencattus-answerfile-invalid-pbs");
        std::ofstream(diskImagePath).close();
        writeAnswerfile(answerfilePath, diskImagePath, interfaces.front(),
            interfaces.front());
        replaceSlurmWithPBS(answerfilePath, "RoundRobin");

        CHECK_THROWS_WITH_AS(AnswerFile { answerfilePath },
            doctest::Contains(
                "Section 'pbs' field 'execution_place' validation failed"),
            std::invalid_argument);

        std::filesystem::remove(answerfilePath);
        std::filesystem::remove(diskImagePath);
    }

    TEST_CASE("fillData loads PBS answerfiles")
    {
        initializeOptionsSingleton();

        const auto interfaces = firstHostInterfaces();
        REQUIRE_FALSE(interfaces.empty());

        const auto answerfilePath
            = tempAnswerfilePath("opencattus-answerfile-pbs");
        const auto diskImagePath = tempIsoPath("opencattus-answerfile-pbs");
        std::ofstream(diskImagePath).close();
        writeAnswerfile(answerfilePath, diskImagePath, interfaces.front(),
            interfaces.front());
        replaceSlurmWithPBS(answerfilePath);

        try {
            AnswerFile answerfile(answerfilePath);
            Cluster cluster;
            cluster.fillData(answerfile);

            REQUIRE(answerfile.pbs.enabled);
            CHECK_FALSE(answerfile.slurm.enabled);
            REQUIRE(cluster.getQueueSystem().has_value());
            CHECK(cluster.getQueueSystem().value()->getKind()
                == QueueSystem::Kind::PBS);
        } catch (const std::exception& e) {
            FAIL(std::string(e.what()));
        } catch (...) {
            FAIL("non-std exception while filling PBS queue system");
        }

        std::filesystem::remove(answerfilePath);
        std::filesystem::remove(diskImagePath);
    }

    TEST_CASE(
        "fillData keeps the service connection bound to the service interface")
    {
        initializeOptionsSingleton();

        const auto interfaces = firstHostInterfaces();
        REQUIRE(interfaces.size() >= 2);

        const auto answerfilePath
            = tempAnswerfilePath("opencattus-cluster-service-connection");
        const auto diskImagePath
            = tempIsoPath("opencattus-cluster-service-connection");
        std::ofstream(diskImagePath).close();
        writeAnswerfile(answerfilePath, diskImagePath, interfaces.front(),
            interfaces.front(), interfaces.back());

        try {
            AnswerFile answerfile(answerfilePath);
            Cluster cluster;
            cluster.fillData(answerfile);

            const auto serviceInterface
                = cluster.getHeadnode()
                      .getConnection(Network::Profile::Service)
                      .getInterface();
            const auto applicationInterface
                = cluster.getHeadnode()
                      .getConnection(Network::Profile::Application)
                      .getInterface();

            REQUIRE(serviceInterface.has_value());
            REQUIRE(applicationInterface.has_value());
            CHECK(serviceInterface.value() == interfaces.front());
            CHECK(applicationInterface.value() == interfaces.back());
        } catch (const std::exception& e) {
            FAIL(std::string(e.what()));
        } catch (...) {
            FAIL("non-std exception while filling cluster from answerfile");
        }

        std::filesystem::remove(answerfilePath);
        std::filesystem::remove(diskImagePath);
    }

    TEST_CASE("fillData keeps the selected repositories from the answerfile")
    {
        initializeOptionsSingleton();

        const auto interfaces = firstHostInterfaces();
        REQUIRE_FALSE(interfaces.empty());

        const auto answerfilePath
            = tempAnswerfilePath("opencattus-cluster-repositories");
        const auto diskImagePath
            = tempIsoPath("opencattus-cluster-repositories");
        std::ofstream(diskImagePath).close();
        writeAnswerfile(answerfilePath, diskImagePath, interfaces.front(),
            interfaces.front(), std::nullopt, true, "confluent", std::nullopt,
            "rocky", "9.7", "baseos, epel, cuda");

        try {
            AnswerFile answerfile(answerfilePath);
            Cluster cluster;
            cluster.fillData(answerfile);

            REQUIRE(cluster.getEnabledRepositories().has_value());
            CHECK(cluster.getEnabledRepositories().value()
                == std::vector<std::string> { "baseos", "epel", "cuda" });
        } catch (const std::exception& e) {
            FAIL(std::string(e.what()));
        } catch (...) {
            FAIL("non-std exception while filling cluster repositories");
        }

        std::filesystem::remove(answerfilePath);
        std::filesystem::remove(diskImagePath);
    }

    TEST_CASE("fillData accepts a planned disk image path during dry-run")
    {
        initializeOptionsSingleton();

        const auto interfaces = firstHostInterfaces();
        REQUIRE_FALSE(interfaces.empty());

        const auto answerfilePath
            = tempAnswerfilePath("opencattus-cluster-planned-disk-image");
        const auto diskImagePath
            = tempIsoPath("opencattus-cluster-planned-disk-image");
        std::filesystem::remove(diskImagePath);
        writeAnswerfile(answerfilePath, diskImagePath, interfaces.front(),
            interfaces.front());

        try {
            AnswerFile answerfile(answerfilePath);
            Cluster cluster;
            CHECK_NOTHROW(cluster.fillData(answerfile));
            CHECK(cluster.getDiskImage().getPath() == diskImagePath);
        } catch (const std::exception& e) {
            FAIL(std::string(e.what()));
        } catch (...) {
            FAIL("non-std exception while accepting a planned disk image path");
        }

        std::filesystem::remove(answerfilePath);
    }

    TEST_CASE("fillData still accepts xcat on Rocky Linux 8")
    {
        initializeOptionsSingleton();

        const auto interfaces = firstHostInterfaces();
        REQUIRE_FALSE(interfaces.empty());

        const auto answerfilePath
            = tempAnswerfilePath("opencattus-cluster-provisioner-xcat-el8");
        const auto diskImagePath
            = tempIsoPath("opencattus-cluster-provisioner-xcat-el8");
        std::ofstream(diskImagePath).close();
        writeAnswerfile(answerfilePath, diskImagePath, interfaces.front(),
            interfaces.front(), std::nullopt, true, "xcat", std::nullopt,
            "rocky", "8.10");

        try {
            AnswerFile answerfile(answerfilePath);
            Cluster cluster;
            cluster.fillData(answerfile);

            CHECK(cluster.getProvisioner() == Cluster::Provisioner::xCAT);
            CHECK(cluster.getComputeNodeOS().getPlatform()
                == opencattus::models::OS::Platform::el8);
        } catch (const std::exception& e) {
            FAIL(std::string(e.what()));
        } catch (...) {
            FAIL("non-std exception while filling cluster for Rocky Linux 8 "
                 "xCAT");
        }

        std::filesystem::remove(answerfilePath);
        std::filesystem::remove(diskImagePath);
    }

    TEST_CASE("fillData maps the Rocky Linux 9 answerfile provisioner into the "
              "cluster model")
    {
        initializeOptionsSingleton();

        const auto interfaces = firstHostInterfaces();
        REQUIRE_FALSE(interfaces.empty());

        const auto diskImagePath
            = tempIsoPath("opencattus-cluster-provisioner-mapping-el9");
        std::ofstream(diskImagePath).close();

        SUBCASE("xcat")
        {
            const auto answerfilePath
                = tempAnswerfilePath("opencattus-cluster-provisioner-xcat-el9");
            writeAnswerfile(answerfilePath, diskImagePath, interfaces.front(),
                interfaces.front(), std::nullopt, true, "xcat");

            try {
                AnswerFile answerfile(answerfilePath);
                Cluster cluster;
                cluster.fillData(answerfile);

                CHECK(cluster.getProvisioner() == Cluster::Provisioner::xCAT);
                CHECK(cluster.getComputeNodeOS().getPlatform()
                    == opencattus::models::OS::Platform::el9);
            } catch (const std::exception& e) {
                FAIL(std::string(e.what()));
            } catch (...) {
                FAIL("non-std exception while filling cluster for Rocky Linux "
                     "9 xCAT");
            }

            std::filesystem::remove(answerfilePath);
        }

        SUBCASE("confluent")
        {
            const auto answerfilePath = tempAnswerfilePath(
                "opencattus-cluster-provisioner-confluent-el9");
            writeAnswerfile(answerfilePath, diskImagePath, interfaces.front(),
                interfaces.front(), std::nullopt, true, "confluent");

            try {
                AnswerFile answerfile(answerfilePath);
                Cluster cluster;
                cluster.fillData(answerfile);

                CHECK(cluster.getProvisioner()
                    == Cluster::Provisioner::Confluent);
                CHECK(cluster.getComputeNodeOS().getPlatform()
                    == opencattus::models::OS::Platform::el9);
            } catch (const std::exception& e) {
                FAIL(std::string(e.what()));
            } catch (...) {
                FAIL("non-std exception while filling cluster for Rocky Linux "
                     "9 Confluent");
            }

            std::filesystem::remove(answerfilePath);
        }

        std::filesystem::remove(diskImagePath);
    }

    TEST_CASE("fillData rejects xcat on EL10")
    {
        initializeOptionsSingleton();

        const auto interfaces = firstHostInterfaces();
        REQUIRE_FALSE(interfaces.empty());

        const auto answerfilePath
            = tempAnswerfilePath("opencattus-cluster-provisioner-xcat-el10");
        const auto diskImagePath
            = tempIsoPath("opencattus-cluster-provisioner-xcat-el10");
        std::ofstream(diskImagePath).close();
        writeAnswerfile(answerfilePath, diskImagePath, interfaces.front(),
            interfaces.front(), std::nullopt, true, "xcat", std::nullopt,
            "rocky", "10.1");

        try {
            AnswerFile answerfile(answerfilePath);
            Cluster cluster;

            CHECK_THROWS_WITH(cluster.fillData(answerfile),
                doctest::Contains("xCAT is not supported on EL10"));
        } catch (const std::exception& e) {
            FAIL(std::string(e.what()));
        } catch (...) {
            FAIL("non-std exception while validating EL10 xCAT rejection");
        }

        std::filesystem::remove(answerfilePath);
        std::filesystem::remove(diskImagePath);
    }

    TEST_CASE("fillData accepts Ubuntu 24.04 compute nodes with xcat")
    {
        initializeOptionsSingleton();

        const auto interfaces = firstHostInterfaces();
        REQUIRE_FALSE(interfaces.empty());

        const auto answerfilePath
            = tempAnswerfilePath("opencattus-cluster-provisioner-ubuntu2404");
        const auto diskImagePath
            = tempIsoPath("opencattus-cluster-provisioner-ubuntu2404");
        std::ofstream(diskImagePath).close();
        writeAnswerfile(answerfilePath, diskImagePath, interfaces.front(),
            interfaces.front(), std::nullopt, true, "xcat", std::nullopt,
            "ubuntu", "24.04");

        try {
            AnswerFile answerfile(answerfilePath);
            Cluster cluster;
            cluster.fillData(answerfile);

            CHECK(cluster.getProvisioner() == Cluster::Provisioner::xCAT);
            CHECK(cluster.getComputeNodeOS().getDistro()
                == opencattus::models::OS::Distro::Ubuntu);
            CHECK(cluster.getComputeNodeOS().getPlatform()
                == opencattus::models::OS::Platform::ubuntu2404);
            CHECK(cluster.getComputeNodeOS().getVersion() == "24.04");
        } catch (const std::exception& e) {
            FAIL(std::string(e.what()));
        } catch (...) {
            FAIL("non-std exception while filling cluster for Ubuntu 24.04 "
                 "xCAT");
        }

        std::filesystem::remove(answerfilePath);
        std::filesystem::remove(diskImagePath);
    }

    TEST_CASE("fillData accepts xcat on Ubuntu 24.04 headnodes")
    {
        initializeOptionsSingleton();

        const auto interfaces = firstHostInterfaces();
        REQUIRE_FALSE(interfaces.empty());

        const auto answerfilePath = tempAnswerfilePath(
            "opencattus-cluster-provisioner-ubuntu2404-headnode-xcat");
        const auto diskImagePath = tempIsoPath(
            "opencattus-cluster-provisioner-ubuntu2404-headnode-xcat");
        std::ofstream(diskImagePath).close();
        writeAnswerfile(answerfilePath, diskImagePath, interfaces.front(),
            interfaces.front(), std::nullopt, true, "xcat", std::nullopt,
            "ubuntu", "24.04");

        try {
            AnswerFile answerfile(answerfilePath);
            Cluster cluster;
            cluster.getHeadnode().setOS(
                opencattus::models::OS(opencattus::models::OS::Distro::Ubuntu,
                    opencattus::models::OS::Platform::ubuntu2404, 0));

            CHECK_NOTHROW(cluster.fillData(answerfile));
            CHECK(cluster.getProvisioner() == Cluster::Provisioner::xCAT);
            CHECK(cluster.getHeadnode().getOS().getPlatform()
                == opencattus::models::OS::Platform::ubuntu2404);
        } catch (const std::exception& e) {
            FAIL(std::string(e.what()));
        } catch (...) {
            FAIL("non-std exception while validating Ubuntu 24.04 headnode "
                 "xCAT rejection");
        }

        std::filesystem::remove(answerfilePath);
        std::filesystem::remove(diskImagePath);
    }

    TEST_CASE("fillData accepts Ubuntu 24.04 compute nodes with confluent")
    {
        initializeOptionsSingleton();

        const auto interfaces = firstHostInterfaces();
        REQUIRE_FALSE(interfaces.empty());

        const auto answerfilePath = tempAnswerfilePath(
            "opencattus-cluster-provisioner-ubuntu2404-confluent");
        const auto diskImagePath
            = tempIsoPath("opencattus-cluster-provisioner-ubuntu2404-confluent");
        std::ofstream(diskImagePath).close();
        writeAnswerfile(answerfilePath, diskImagePath, interfaces.front(),
            interfaces.front(), std::nullopt, true, "confluent", std::nullopt,
            "ubuntu", "24.04");

        try {
            AnswerFile answerfile(answerfilePath);
            Cluster cluster;
            cluster.fillData(answerfile);

            CHECK(cluster.getProvisioner() == Cluster::Provisioner::Confluent);
            CHECK(cluster.getComputeNodeOS().getDistro()
                == opencattus::models::OS::Distro::Ubuntu);
            CHECK(cluster.getComputeNodeOS().getPlatform()
                == opencattus::models::OS::Platform::ubuntu2404);
            CHECK(cluster.getComputeNodeOS().getVersion() == "24.04");
        } catch (const std::exception& e) {
            FAIL(std::string(e.what()));
        } catch (...) {
            FAIL("non-std exception while filling cluster for Ubuntu 24.04 "
                 "Confluent");
        }

        std::filesystem::remove(answerfilePath);
        std::filesystem::remove(diskImagePath);
    }

    TEST_CASE("fillData accepts confluent on Rocky Linux 10")
    {
        initializeOptionsSingleton();

        const auto interfaces = firstHostInterfaces();
        REQUIRE_FALSE(interfaces.empty());

        const auto answerfilePath = tempAnswerfilePath(
            "opencattus-cluster-provisioner-confluent-el10");
        const auto diskImagePath
            = tempIsoPath("opencattus-cluster-provisioner-confluent-el10");
        std::ofstream(diskImagePath).close();
        writeAnswerfile(answerfilePath, diskImagePath, interfaces.front(),
            interfaces.front(), std::nullopt, true, "confluent", std::nullopt,
            "rocky", "10.1");

        try {
            AnswerFile answerfile(answerfilePath);
            Cluster cluster;
            cluster.getHeadnode().setOS(
                opencattus::models::OS(opencattus::models::OS::Distro::RHEL,
                    opencattus::models::OS::Platform::el9, 6));
            cluster.fillData(answerfile);

            CHECK(cluster.getProvisioner() == Cluster::Provisioner::Confluent);
            CHECK(cluster.getHeadnode().getOS().getDistro()
                == opencattus::models::OS::Distro::RHEL);
            CHECK(cluster.getHeadnode().getOS().getPlatform()
                == opencattus::models::OS::Platform::el9);
            CHECK(cluster.getComputeNodeOS().getDistro()
                == opencattus::models::OS::Distro::Rocky);
            CHECK(cluster.getComputeNodeOS().getPlatform()
                == opencattus::models::OS::Platform::el10);
        } catch (const std::exception& e) {
            FAIL(std::string(e.what()));
        } catch (...) {
            FAIL("non-std exception while filling cluster for Rocky Linux 10 "
                 "Confluent");
        }

        std::filesystem::remove(answerfilePath);
        std::filesystem::remove(diskImagePath);
    }

    TEST_CASE("fillData accepts AlmaLinux shorthand on EL10")
    {
        initializeOptionsSingleton();

        const auto interfaces = firstHostInterfaces();
        REQUIRE_FALSE(interfaces.empty());

        const auto answerfilePath = tempAnswerfilePath(
            "opencattus-cluster-provisioner-confluent-alma10");
        const auto diskImagePath
            = tempIsoPath("opencattus-cluster-provisioner-confluent-alma10");
        std::ofstream(diskImagePath).close();
        writeAnswerfile(answerfilePath, diskImagePath, interfaces.front(),
            interfaces.front(), std::nullopt, true, "confluent", std::nullopt,
            "alma", "10.1");

        try {
            AnswerFile answerfile(answerfilePath);
            CHECK(answerfile.system.distro
                == opencattus::models::OS::Distro::AlmaLinux);
            CHECK(answerfile.system.version == "10.1");
        } catch (const std::exception& e) {
            FAIL(std::string(e.what()));
        } catch (...) {
            FAIL("non-std exception while loading AlmaLinux shorthand "
                 "answerfile");
        }

        std::filesystem::remove(answerfilePath);
        std::filesystem::remove(diskImagePath);
    }

    TEST_CASE("dumpFile writes corrected metadata to the requested output path")
    {
        initializeOptionsSingleton();

        const auto interfaces = firstHostInterfaces();
        REQUIRE_FALSE(interfaces.empty());

        const auto sourcePath
            = tempAnswerfilePath("opencattus-answerfile-dump-source");
        const auto outputPath
            = tempAnswerfilePath("opencattus-answerfile-dump-output");
        const auto diskImagePath
            = tempIsoPath("opencattus-answerfile-dump-source");
        std::ofstream(diskImagePath).close();
        writeAnswerfile(sourcePath, diskImagePath, interfaces.front(),
            interfaces.front(), std::nullopt, true, "xcat");

        try {
            AnswerFile answerfile(sourcePath);
            std::filesystem::remove(outputPath);

            answerfile.dumpFile(outputPath);

            REQUIRE(std::filesystem::exists(outputPath));
            const auto dumped = opencattus::services::files::read(outputPath);
            CHECK(dumped.contains("administrator_email=foo@example.com"));
            CHECK_FALSE(
                dumped.contains("admm_keyfilestrator_email=foo@example.com"));
            CHECK(dumped.contains("provisioner=xcat"));
        } catch (const std::exception& e) {
            FAIL(std::string(e.what()));
        } catch (...) {
            FAIL("non-std exception while dumping answerfile metadata");
        }

        std::filesystem::remove(sourcePath);
        std::filesystem::remove(outputPath);
        std::filesystem::remove(diskImagePath);
    }

    TEST_CASE("dumpData preserves headnode connection addresses instead of "
              "network base addresses")
    {
        initializeOptionsSingleton();

        const auto interfaces = firstHostInterfaces();
        REQUIRE_FALSE(interfaces.empty());

        const auto sourcePath
            = tempAnswerfilePath("opencattus-cluster-dump-source");
        const auto outputPath
            = tempAnswerfilePath("opencattus-cluster-dump-output");
        const auto diskImagePath
            = tempIsoPath("opencattus-cluster-dump-source");
        std::ofstream(diskImagePath).close();
        writeAnswerfile(sourcePath, diskImagePath, interfaces.front(),
            interfaces.front(), std::nullopt, true, "confluent");

        try {
            AnswerFile answerfile(sourcePath);
            Cluster cluster;
            cluster.fillData(answerfile);

            std::filesystem::remove(outputPath);
            cluster.dumpData(outputPath);

            REQUIRE(std::filesystem::exists(outputPath));
            const auto dumped = opencattus::services::files::read(outputPath);
            CHECK(dumped.contains("ip_address=192.168.30.254"));
            CHECK(dumped.contains("ip_address=192.168.124.10"));
            CHECK_FALSE(dumped.contains("ip_address=192.168.30.0"));
            CHECK_FALSE(dumped.contains("ip_address=192.168.124.0"));
            CHECK(dumped.contains("provisioner=confluent"));
        } catch (const std::exception& e) {
            FAIL(std::string(e.what()));
        } catch (...) {
            FAIL("non-std exception while dumping cluster data");
        }

        std::filesystem::remove(sourcePath);
        std::filesystem::remove(outputPath);
        std::filesystem::remove(diskImagePath);
    }

    TEST_CASE("dumpData rewrites existing answerfiles with deterministic "
              "section order")
    {
        initializeOptionsSingleton();

        const auto interfaces = firstHostInterfaces();
        REQUIRE_FALSE(interfaces.empty());

        const auto sourcePath
            = tempAnswerfilePath("opencattus-cluster-dump-order-source");
        const auto outputPath
            = tempAnswerfilePath("opencattus-cluster-dump-order-output");
        const auto diskImagePath = tempIsoPath("opencattus-cluster-dump-order");
        std::ofstream(diskImagePath).close();
        writeAnswerfile(sourcePath, diskImagePath, interfaces.front(),
            interfaces.front(), std::nullopt, true, "confluent");
        appendSecondNodeSection(sourcePath);

        {
            std::ofstream staleOutput(outputPath);
            REQUIRE(staleOutput.is_open());
            staleOutput << "[postfix]\n"
                        << "profile=Relay\n\n"
                        << "[network_service]\n"
                        << "interface=stale0\n\n"
                        << "[node.2]\n"
                        << "hostname=stale02\n\n";
        }

        try {
            AnswerFile sourceAnswerfile(sourcePath);
            Cluster cluster;
            cluster.fillData(sourceAnswerfile);

            cluster.dumpData(outputPath);

            const auto dumped = opencattus::services::files::read(outputPath);
            const auto networkService = dumped.find("[network_service]");
            const auto nodeDefaults = dumped.find("[node]");
            const auto node1 = dumped.find("[node.1]");
            const auto node2 = dumped.find("[node.2]");

            REQUIRE(networkService != std::string::npos);
            REQUIRE(nodeDefaults != std::string::npos);
            REQUIRE(node1 != std::string::npos);
            REQUIRE(node2 != std::string::npos);
            CHECK(networkService < nodeDefaults);
            CHECK(nodeDefaults < node1);
            CHECK(node1 < node2);
            CHECK_FALSE(dumped.contains("[postfix]"));
            CHECK_FALSE(dumped.contains("stale02"));
        } catch (const std::exception& e) {
            FAIL(std::string(e.what()));
        } catch (...) {
            FAIL("non-std exception while checking dumped answerfile order");
        }

        std::filesystem::remove(sourcePath);
        std::filesystem::remove(outputPath);
        std::filesystem::remove(diskImagePath);
    }

    TEST_CASE("dumpData round-trips service and application network sections")
    {
        initializeOptionsSingleton();

        const auto interfaces = firstHostInterfaces();
        REQUIRE(interfaces.size() >= 2);

        const auto sourcePath
            = tempAnswerfilePath("opencattus-cluster-roundtrip-source");
        const auto outputPath
            = tempAnswerfilePath("opencattus-cluster-roundtrip-output");
        const auto diskImagePath = tempIsoPath("opencattus-cluster-roundtrip");
        std::ofstream(diskImagePath).close();
        writeAnswerfile(sourcePath, diskImagePath, interfaces.front(),
            interfaces.front(), interfaces.back(), true, "confluent",
            "1.1.1.1, 8.8.8.8 9.9.9.9", "rocky", "10.1");

        try {
            AnswerFile sourceAnswerfile(sourcePath);
            Cluster cluster;
            cluster.fillData(sourceAnswerfile);

            std::filesystem::remove(outputPath);
            cluster.dumpData(outputPath);

            AnswerFile dumpedAnswerfile(outputPath);

            CHECK(dumpedAnswerfile.system.provisioner == "confluent");
            CHECK(dumpedAnswerfile.system.version == "10.1");

            REQUIRE(dumpedAnswerfile.service.con_interface.has_value());
            CHECK(dumpedAnswerfile.service.con_interface.value()
                == interfaces.front());
            REQUIRE(dumpedAnswerfile.service.con_ip_addr.has_value());
            CHECK(dumpedAnswerfile.service.con_ip_addr->to_string()
                == "192.168.31.254");
            REQUIRE(dumpedAnswerfile.service.domain_name.has_value());
            CHECK(dumpedAnswerfile.service.domain_name.value()
                == "cluster.service.example.com");
            REQUIRE(dumpedAnswerfile.service.nameservers.has_value());
            CHECK(dumpedAnswerfile.service.nameservers.value()
                == std::vector<std::string> {
                    "1.1.1.1", "8.8.8.8", "9.9.9.9" });

            REQUIRE(dumpedAnswerfile.application.con_interface.has_value());
            CHECK(dumpedAnswerfile.application.con_interface.value()
                == interfaces.back());
            REQUIRE(dumpedAnswerfile.application.con_ip_addr.has_value());
            CHECK(dumpedAnswerfile.application.con_ip_addr->to_string()
                == "172.16.0.254");
            REQUIRE(dumpedAnswerfile.application.domain_name.has_value());
            CHECK(dumpedAnswerfile.application.domain_name.value()
                == "cluster.application.example.com");
        } catch (const std::exception& e) {
            FAIL(std::string(e.what()));
        } catch (...) {
            FAIL("non-std exception while round-tripping cluster answerfile "
                 "data");
        }

        std::filesystem::remove(sourcePath);
        std::filesystem::remove(outputPath);
        std::filesystem::remove(diskImagePath);
    }

    TEST_CASE("fillData maps the legacy mellanox OFED answerfile kind to DOCA")
    {
        initializeOptionsSingleton();

        const auto interfaces = firstHostInterfaces();
        REQUIRE_FALSE(interfaces.empty());

        const auto answerfilePath
            = tempAnswerfilePath("opencattus-answerfile-ofed-legacy-alias");
        const auto diskImagePath
            = tempIsoPath("opencattus-answerfile-ofed-legacy-alias");
        std::ofstream(diskImagePath).close();
        writeAnswerfile(answerfilePath, diskImagePath, interfaces.front(),
            interfaces.front());
        appendOFEDSection(answerfilePath, "mellanox", "latest-3.2-LTS");

        try {
            AnswerFile answerfile(answerfilePath);
            Cluster cluster;
            cluster.fillData(answerfile);

            REQUIRE(cluster.getOFED().has_value());
            CHECK(cluster.getOFED()->getKind() == OFED::Kind::Doca);
            CHECK(cluster.getOFED()->getVersion() == "latest-3.2-LTS");
        } catch (const std::exception& e) {
            FAIL(std::string(e.what()));
        } catch (...) {
            FAIL("non-std exception while mapping legacy OFED alias");
        }

        std::filesystem::remove(answerfilePath);
        std::filesystem::remove(diskImagePath);
    }

    TEST_CASE("fillData accepts the explicit doca OFED answerfile kind")
    {
        initializeOptionsSingleton();

        const auto interfaces = firstHostInterfaces();
        REQUIRE_FALSE(interfaces.empty());

        const auto answerfilePath
            = tempAnswerfilePath("opencattus-answerfile-ofed-doca");
        const auto diskImagePath
            = tempIsoPath("opencattus-answerfile-ofed-doca");
        std::ofstream(diskImagePath).close();
        writeAnswerfile(answerfilePath, diskImagePath, interfaces.front(),
            interfaces.front());
        appendOFEDSection(answerfilePath, "doca", "latest");

        try {
            AnswerFile answerfile(answerfilePath);
            Cluster cluster;
            cluster.fillData(answerfile);

            REQUIRE(cluster.getOFED().has_value());
            CHECK(cluster.getOFED()->getKind() == OFED::Kind::Doca);
            CHECK(cluster.getOFED()->getVersion() == "latest");
        } catch (const std::exception& e) {
            FAIL(std::string(e.what()));
        } catch (...) {
            FAIL("non-std exception while loading explicit DOCA OFED kind");
        }

        std::filesystem::remove(answerfilePath);
        std::filesystem::remove(diskImagePath);
    }

    TEST_CASE("dumpData round-trips OFED settings")
    {
        initializeOptionsSingleton();

        const auto interfaces = firstHostInterfaces();
        REQUIRE_FALSE(interfaces.empty());

        const auto sourcePath
            = tempAnswerfilePath("opencattus-cluster-ofed-source");
        const auto outputPath
            = tempAnswerfilePath("opencattus-cluster-ofed-output");
        const auto diskImagePath = tempIsoPath("opencattus-cluster-ofed");
        std::ofstream(diskImagePath).close();
        writeAnswerfile(
            sourcePath, diskImagePath, interfaces.front(), interfaces.front());
        appendOFEDSection(sourcePath, "doca", "latest-3.2-LTS");

        try {
            AnswerFile sourceAnswerfile(sourcePath);
            Cluster cluster;
            cluster.fillData(sourceAnswerfile);

            std::filesystem::remove(outputPath);
            cluster.dumpData(outputPath);

            AnswerFile dumpedAnswerfile(outputPath);

            CHECK(dumpedAnswerfile.ofed.enabled);
            CHECK(dumpedAnswerfile.ofed.kind == "doca");
            REQUIRE(dumpedAnswerfile.ofed.version.has_value());
            CHECK(dumpedAnswerfile.ofed.version.value() == "latest-3.2-LTS");
        } catch (const std::exception& e) {
            FAIL(std::string(e.what()));
        } catch (...) {
            FAIL("non-std exception while round-tripping OFED settings");
        }

        std::filesystem::remove(sourcePath);
        std::filesystem::remove(outputPath);
        std::filesystem::remove(diskImagePath);
    }

    TEST_CASE("loadOptions accepts nodes without BMC addresses")
    {
        initializeOptionsSingleton();

        const auto interfaces = firstHostInterfaces();
        REQUIRE_FALSE(interfaces.empty());

        const auto answerfilePath
            = tempAnswerfilePath("opencattus-answerfile-node-without-bmc");
        const auto diskImagePath
            = tempIsoPath("opencattus-answerfile-node-without-bmc");
        std::ofstream(diskImagePath).close();
        writeAnswerfile(answerfilePath, diskImagePath, interfaces.front(),
            interfaces.front(), std::nullopt, true, "confluent", std::nullopt,
            "rocky", "9.7", std::nullopt, false);

        AnswerFile answerfile(answerfilePath);

        REQUIRE(answerfile.nodes.nodes.size() == 1);
        CHECK_FALSE(answerfile.nodes.nodes[0].bmc_address.has_value());

        Cluster cluster;
        CHECK_NOTHROW(cluster.fillData(answerfile));
        REQUIRE(cluster.getNodes().size() == 1);
        CHECK_FALSE(cluster.getNodes().front().getBMC().has_value());

        std::filesystem::remove(answerfilePath);
        std::filesystem::remove(diskImagePath);
    }
}
