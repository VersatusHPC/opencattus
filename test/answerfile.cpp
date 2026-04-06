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
#include <opencattus/services/options.h>
#include <string>

namespace {
using opencattus::models::AnswerFile;
using opencattus::models::Cluster;
using opencattus::services::Options;

void initializeOptionsSingleton()
{
    const char* argv[] = { "OpenCATTUS-tests", "--dry" };
    auto options = opencattus::services::options::factory(2, argv);
    opencattus::Singleton<const Options>::init(
        std::unique_ptr<const Options>(options.release()));
}

auto tempAnswerfilePath(std::string_view stem) -> std::filesystem::path
{
    return std::filesystem::temp_directory_path()
        / fmt::format("{}.ini", stem);
}

auto tempIsoPath(std::string_view stem) -> std::filesystem::path
{
    return std::filesystem::temp_directory_path()
        / fmt::format("{}.iso", stem);
}

auto firstHostInterfaces() -> std::vector<std::string>
{
    return Connection::fetchInterfaces();
}

void writeAnswerfile(const std::filesystem::path& path,
    const std::filesystem::path& diskImagePath,
    std::string_view managementInterface, std::string_view serviceInterface,
    std::optional<std::string_view> applicationInterface = std::nullopt,
    bool includeServiceNetwork = true,
    std::string_view provisioner = "confluent",
    std::optional<std::string_view> serviceNameservers = std::nullopt,
    std::string_view distro = "rocky",
    std::string_view version = "9.7")
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
        << "node_ip=192.168.30.1\n"
        << "bmc_address=192.168.31.101\n";
}

void appendOFEDSection(const std::filesystem::path& path,
    std::string_view kind, std::string_view version = "latest")
{
    std::ofstream out(path, std::ios::app);
    REQUIRE(out.is_open());
    out << "\n[ofed]\n"
        << "kind=" << kind << '\n'
        << "version=" << version << '\n';
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
        writeAnswerfile(
            answerfilePath, diskImagePath, interfaces.front(), interfaces.front());

        try {
            AnswerFile answerfile(answerfilePath);

            REQUIRE(answerfile.service.con_interface.has_value());
            CHECK(answerfile.service.con_interface.value() == interfaces.front());
            REQUIRE(answerfile.service.con_ip_addr.has_value());
            CHECK(
                answerfile.service.con_ip_addr->to_string() == "192.168.31.254");
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

    TEST_CASE("loadOptions leaves the service network unset when the section is absent")
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
            FAIL("non-std exception while loading answerfile without service network");
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

    TEST_CASE("fillData keeps the service connection bound to the service interface")
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
                = cluster.getHeadnode().getConnection(Network::Profile::Service)
                      .getInterface();
            const auto applicationInterface = cluster.getHeadnode()
                                                  .getConnection(
                                                      Network::Profile::Application)
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
            CHECK(cluster.getHeadnode().getOS().getPlatform()
                == opencattus::models::OS::Platform::el8);
        } catch (const std::exception& e) {
            FAIL(std::string(e.what()));
        } catch (...) {
            FAIL("non-std exception while filling cluster for Rocky Linux 8 xCAT");
        }

        std::filesystem::remove(answerfilePath);
        std::filesystem::remove(diskImagePath);
    }

    TEST_CASE("fillData maps the Rocky Linux 9 answerfile provisioner into the cluster model")
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

                CHECK(cluster.getProvisioner()
                    == Cluster::Provisioner::xCAT);
                CHECK(cluster.getHeadnode().getOS().getPlatform()
                    == opencattus::models::OS::Platform::el9);
            } catch (const std::exception& e) {
                FAIL(std::string(e.what()));
            } catch (...) {
                FAIL("non-std exception while filling cluster for Rocky Linux 9 xCAT");
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
                CHECK(cluster.getHeadnode().getOS().getPlatform()
                    == opencattus::models::OS::Platform::el9);
            } catch (const std::exception& e) {
                FAIL(std::string(e.what()));
            } catch (...) {
                FAIL(
                    "non-std exception while filling cluster for Rocky Linux 9 Confluent");
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
            interfaces.front(), std::nullopt, true, "confluent",
            std::nullopt, "rocky", "10.1");

        try {
            AnswerFile answerfile(answerfilePath);
            Cluster cluster;
            cluster.fillData(answerfile);

            CHECK(cluster.getProvisioner()
                == Cluster::Provisioner::Confluent);
            CHECK(cluster.getHeadnode().getOS().getDistro()
                == opencattus::models::OS::Distro::Rocky);
            CHECK(cluster.getHeadnode().getOS().getPlatform()
                == opencattus::models::OS::Platform::el10);
        } catch (const std::exception& e) {
            FAIL(std::string(e.what()));
        } catch (...) {
            FAIL("non-std exception while filling cluster for Rocky Linux 10 Confluent");
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
            const auto dumped
                = opencattus::services::files::read(outputPath);
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

    TEST_CASE("dumpData preserves headnode connection addresses instead of network base addresses")
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
            const auto dumped
                = opencattus::services::files::read(outputPath);
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

    TEST_CASE("dumpData round-trips service and application network sections")
    {
        initializeOptionsSingleton();

        const auto interfaces = firstHostInterfaces();
        REQUIRE(interfaces.size() >= 2);

        const auto sourcePath
            = tempAnswerfilePath("opencattus-cluster-roundtrip-source");
        const auto outputPath
            = tempAnswerfilePath("opencattus-cluster-roundtrip-output");
        const auto diskImagePath
            = tempIsoPath("opencattus-cluster-roundtrip");
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
            FAIL("non-std exception while round-tripping cluster answerfile data");
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
        writeAnswerfile(
            answerfilePath, diskImagePath, interfaces.front(), interfaces.front());
        appendOFEDSection(answerfilePath, "mellanox", "latest-2.9-LTS");

        try {
            AnswerFile answerfile(answerfilePath);
            Cluster cluster;
            cluster.fillData(answerfile);

            REQUIRE(cluster.getOFED().has_value());
            CHECK(cluster.getOFED()->getKind() == OFED::Kind::Doca);
            CHECK(cluster.getOFED()->getVersion() == "latest-2.9-LTS");
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
        writeAnswerfile(
            answerfilePath, diskImagePath, interfaces.front(), interfaces.front());
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
}
