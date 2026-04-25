/*
 * Copyright 2023 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fmt/format.h>

#include <opencattus/NFS.h>
#include <opencattus/const.h>
#include <opencattus/functions.h>
#include <opencattus/services/init.h>
#include <opencattus/services/log.h>
#include <opencattus/services/osservice.h>
#include <opencattus/services/scriptbuilder.h>
#include <opencattus/utils/formatters.h>
#include <opencattus/utils/singleton.h>
#include <string_view>

using opencattus::models::OS;

#ifdef BUILD_TESTING
#include <doctest/doctest.h>
#else
#define DOCTEST_CONFIG_DISABLE
#include <doctest/doctest.h>
#endif

namespace opencattus::services {
NFS::NFS(const std::string& directoryName, const std::string& directoryPath,
    const boost::asio::ip::address& address, const std::string& permissions)
    : m_directoryName(directoryName)
    , m_directoryPath(directoryPath)
    , m_permissions(permissions)
    , m_address(address)
{
    setFullPath();
}

void NFS::setFullPath()
{
    m_fullPath = fmt::format("{}/{}", m_directoryPath, m_directoryName);
}

opencattus::services::ScriptBuilder NFS::installScript(const OS& osinfo)
{
    using namespace opencattus;
    services::ScriptBuilder builder(osinfo);
    const auto nfsServerPackage
        = osinfo.getPackageType() == OS::PackageType::DEB
        ? std::string_view("nfs-kernel-server")
        : std::string_view("nfs-utils");
    const auto nfsServerServices
        = osinfo.getPackageType() == OS::PackageType::DEB
        ? std::string_view("rpcbind nfs-kernel-server")
        : std::string_view("rpcbind nfs-server");
    builder.addNewLine()
        .addCommand("# Variables")
        .addCommand("HEADNODE=$(hostname -s)")
        .addNewLine()
        .addCommand("# install packages")
        .addPackage(nfsServerPackage)
        .addNewLine()
        .addCommand("# Add exports to /etc/exports")
        .addLineToFile("/etc/exports", "^/home[[:space:]]",
            "/home *(rw,no_subtree_check,fsid={},no_root_squash)", 10)
        .addLineToFile("/etc/exports", "/opt/ohpc/pub",
            "/opt/ohpc/pub *(ro,no_subtree_check,fsid={})", 11)
        .addLineToFile(
            "/etc/exports", "/opt/spack", "/opt/spack *(ro,no_subtree_check)");
    if (utils::singleton::answerfile()->system.provisioner == "xcat") {
        builder
            .addLineToFile("/etc/exports", "/tftpboot",
                "/tftpboot *(rw,no_root_squash,sync,no_subtree_check)")
            .addLineToFile("/etc/exports", "/install",
                "/install *(rw,no_root_squash,sync,no_subtree_check)")
            .addNewLine();
    }
    builder.enableService(nfsServerServices)
        .addCommand("exportfs -a > /dev/null 2>&1 || :")
        .addNewLine()
        .addCommand(R"(# Update firewall rules
	if systemctl is-active --quiet firewalld.service; then
	    firewall-cmd --permanent --add-service={{nfs,mountd,rpc-bind}}
	    firewall-cmd --reload
	fi)");
    return builder;
}

opencattus::services::ScriptBuilder NFS::imageInstallScript(
    const OS& osinfo, const opencattus::services::XCAT::ImageInstallArgs& args)
{
    using namespace opencattus;
    services::ScriptBuilder builder(osinfo);
    const auto nfsClientPackage
        = osinfo.getPackageType() == OS::PackageType::DEB
        ? std::string_view("nfs-common")
        : std::string_view("nfs-utils");
    builder.addNewLine()
        .addCommand("# Define variables (for shell script execution)")
        .addCommand("IMAGE=\"{}\"", args.imageName)
        .addCommand("ROOTFS=\"{}\"", args.rootfs)
        .addCommand("POSTINSTALL=\"{}\"", args.postinstall)
        .addCommand("PKGLIST=\"{}\"", args.pkglist)
        .addCommand("HEADNODE=$(hostname -s)")
        .addNewLine()
        .addCommand("# Add autofs commands to postinstall")
        .addLineToFile("${POSTINSTALL}", "autofs",
            "chroot \\${{IMG_ROOTIMGDIR}} systemctl enable autofs")
        .addCommand("chmod +x \"${{POSTINSTALL}}\"")
        .addNewLine()
        .addCommand("# Add required packages to the image")
        .addLineToFile("${PKGLIST}", nfsClientPackage, "{}", nfsClientPackage)
        .addLineToFile("${PKGLIST}", "autofs", "autofs")
        .addNewLine()
        .addCommand("# Configure autofs")
        .addLineToFile(
            "${ROOTFS}/etc/auto.master", "/home", "/home   /etc/auto.home")
        .addLineToFile("${ROOTFS}/etc/auto.master", "/opt/ohpc/pub",
            "/opt/ohpc/pub   /etc/auto.ohpc")
        .addNewLine()
        .addLineToFile("${ROOTFS}/etc/auto.home", "home-map",
            "* -fstype=nfs,rw,no_subtree_check,no_root_squash "
            "${{HEADNODE}}:/home/&")
        .addNewLine()
        .addLineToFile("${ROOTFS}/etc/auto.ohpc", "ohpc-map",
            "* -fstype=nfs,ro,no_subtree_check ${{HEADNODE}}:/opt/ohpc/pub/&")
        .addNewLine()
        .addCommand("# Create mount points")
        .addCommand("mkdir -p ${{ROOTFS}}/home ${{ROOTFS}}/opt/ohpc/pub || :")
        .addNewLine()
        .addCommand("# Update xCAT configuration")
        .addCommand(
            "chdef -t osimage ${{IMAGE}} postinstall=\"${{POSTINSTALL}}\"");
    return builder;
}

TEST_SUITE_BEGIN("opencattus::services::NFS");

TEST_CASE("installScript")
{
    const OS osinfo
        = opencattus::models::OS(OS::Distro::Rocky, OS::Platform::el9, 5);
    opencattus::services::initializeSingletonsOptions(
        std::make_unique<const Options>());
    opencattus::Singleton<const models::AnswerFile>::init(
        []() -> std::unique_ptr<const models::AnswerFile> {
            auto answerfile = std::make_unique<models::AnswerFile>(
                "test/sample/answerfile/rocky9-xcat.ini");
            return answerfile;
        });
    const auto builder = NFS::installScript(osinfo);
    const auto scriptStr = builder.toString();
    const auto script = std::string_view(scriptStr);
    CHECK(script.contains("dnf install -y nfs-utils\n"));
    CHECK(script.contains("systemctl enable --now rpcbind nfs-server"));
    CHECK(script.contains("exportfs -a"));
    CHECK(script.contains("systemctl is-active --quiet firewalld.service"));
    CHECK(
        script.contains("/home *(rw,no_subtree_check,fsid=10,no_root_squash)"));
    CHECK(script.contains(R"(grep -q "^/home[[:space:]]" "/etc/exports")"));
    CHECK(script.contains("/opt/ohpc/pub *(ro,no_subtree_check,fsid=11)"));
    CHECK(script.contains("/opt/spack *(ro,no_subtree_check)"));
    CHECK(script.contains(
        "/tftpboot *(rw,no_root_squash,sync,no_subtree_check)"));
    CHECK(
        script.contains("/install *(rw,no_root_squash,sync,no_subtree_check)"));
}

TEST_CASE("installImageScript")
{
    const OS osinfo
        = opencattus::models::OS(OS::Distro::Rocky, OS::Platform::el9, 5);
    opencattus::Singleton<const models::AnswerFile>::init(
        []() -> std::unique_ptr<const models::AnswerFile> {
            auto answerfile = std::make_unique<models::AnswerFile>(
                "test/sample/answerfile/rocky9-xcat.ini");
            return answerfile;
        });
    const auto builder = NFS::imageInstallScript(osinfo,
        { .imageName = "rocky9.5-x86_64-netboot-compute",
            .rootfs = "/install/netboot/rocky9.5/x86_64/compute/rootimg",
            .postinstall = "/install/custom/netboot/compute.postinstall",
            .pkglist = "/install/custom/netboot/compute.otherpkglist" });
    const std::string script = builder.toString();
    CHECK(script.contains("HEADNODE="));
    CHECK(script.contains("ROOTFS="));
    CHECK(script.contains("PKGLIST="));
    CHECK(script.contains("systemctl enable autofs"));
    CHECK(script.contains(R"(echo "nfs-utils" >> "${PKGLIST}")"));
    CHECK(script.contains(R"(echo "autofs" >> "${PKGLIST}")"));
    CHECK(script.contains(
        R"(echo "/home   /etc/auto.home" >> "${ROOTFS}/etc/auto.master")"));
    CHECK(script.contains(
        R"(echo "/opt/ohpc/pub   /etc/auto.ohpc" >> "${ROOTFS}/etc/auto.master")"));
    CHECK(script.contains(
        R"(echo "* -fstype=nfs,rw,no_subtree_check,no_root_squash ${HEADNODE}:/home/&" >> "${ROOTFS}/etc/auto.home")"));
    CHECK(script.contains(
        R"(echo "* -fstype=nfs,ro,no_subtree_check ${HEADNODE}:/opt/ohpc/pub/&" >> "${ROOTFS}/etc/auto.ohpc")"));
    CHECK(script.contains(
        R"(chdef -t osimage ${IMAGE} postinstall="${POSTINSTALL}")"));
};

TEST_CASE("installImageScript uses Debian package names for Ubuntu images")
{
    const OS osinfo
        = opencattus::models::OS(OS::Distro::Ubuntu, OS::Platform::ubuntu2404, 0);
    const auto builder = NFS::imageInstallScript(osinfo,
        { .imageName = "ubuntu24.04-x86_64-netboot-compute",
            .rootfs = "/install/netboot/ubuntu24.04/x86_64/compute/rootimg",
            .postinstall = "/install/custom/netboot/compute.postinstall",
            .pkglist = "/install/custom/netboot/compute.otherpkglist" });
    const std::string script = builder.toString();
    CHECK(script.contains(R"(echo "nfs-common" >> "${PKGLIST}")"));
    CHECK_FALSE(script.contains(R"(echo "nfs-utils" >> "${PKGLIST}")"));
};

TEST_CASE("installScript uses Debian NFS server package names on Ubuntu")
{
    const OS osinfo
        = opencattus::models::OS(OS::Distro::Ubuntu, OS::Platform::ubuntu2404, 0);
    opencattus::services::initializeSingletonsOptions(
        std::make_unique<const Options>());
    opencattus::Singleton<const models::AnswerFile>::init(
        []() -> std::unique_ptr<const models::AnswerFile> {
            auto answerfile = std::make_unique<models::AnswerFile>(
                "test/sample/answerfile/rocky9-xcat.ini");
            return answerfile;
        });
    const auto builder = NFS::installScript(osinfo);
    const auto scriptStr = builder.toString();
    const auto script = std::string_view(scriptStr);

    CHECK(script.contains("DEBIAN_FRONTEND=noninteractive apt install -y "
                          "nfs-kernel-server"));
    CHECK(script.contains("systemctl enable --now rpcbind nfs-kernel-server"));
}

}

TEST_SUITE_END();
