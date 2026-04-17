/*
 * Copyright 2023 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <opencattus/functions.h>
#include <opencattus/presenter/PresenterNodesOperationalSystem.h>
#include <opencattus/services/log.h>
#include <opencattus/utils/string.h>

#include <algorithm>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/lexical_cast.hpp>
#include <filesystem>
#include <fmt/args.h>
#include <fmt/core.h>
#include <map>
#include <ranges>
#include <regex>
#include <string_view>

#ifdef BUILD_TESTING
#include <doctest/doctest.h>
#else
#define DOCTEST_CONFIG_DISABLE
#include <doctest/doctest.h>
#endif

namespace fs = std::filesystem;

namespace {

using OS = opencattus::models::OS;
using PresenterNodesVersionCombo
    = opencattus::presenter::PresenterNodesVersionCombo;

auto defaultVersionComboFor(OS::Distro /*distro*/) -> PresenterNodesVersionCombo
{
    return { 9, 6, OS::Arch::x86_64 };
}

auto parseVersionString(std::string_view raw)
    -> std::optional<std::pair<int, int>>
{
    const auto separator = raw.find('.');
    if (separator == std::string_view::npos) {
        return std::nullopt;
    }

    try {
        return std::pair<int, int> {
            std::stoi(std::string(raw.substr(0, separator))),
            std::stoi(std::string(raw.substr(separator + 1))),
        };
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

auto parseArchitecture(std::string_view raw) -> std::optional<OS::Arch>
{
    const auto normalized = opencattus::utils::string::lower(std::string(raw));
    if (normalized.contains("x86_64")) {
        return OS::Arch::x86_64;
    }

    if (normalized.contains("ppc64le")) {
        return OS::Arch::ppc64le;
    }

    return std::nullopt;
}

auto makeOperatingSystem(
    OS::Distro distro, const PresenterNodesVersionCombo& version)
{
    const auto [majorVersion, minorVersion, arch] = version;

    switch (majorVersion) {
        case 8:
            return OS(distro, OS::Platform::el8,
                static_cast<unsigned>(minorVersion), arch);
        case 9:
            return OS(distro, OS::Platform::el9,
                static_cast<unsigned>(minorVersion), arch);
        case 10:
            return OS(distro, OS::Platform::el10,
                static_cast<unsigned>(minorVersion), arch);
        default:
            throw std::runtime_error(fmt::format(
                "Unsupported OS version {}.{}", majorVersion, minorVersion));
    }
}

auto inferVersionComboFromIso(OS::Distro distro, std::string_view isoName)
    -> std::optional<PresenterNodesVersionCombo>
{
    const auto arch = parseArchitecture(isoName);
    if (!arch.has_value()) {
        return std::nullopt;
    }

    std::smatch match;
    const auto isoString = std::string(isoName);

    if (distro == OS::Distro::OL) {
        const std::regex olPattern(R"(R([0-9]+)-U([0-9]+))");
        if (!std::regex_search(isoString, match, olPattern)) {
            return std::nullopt;
        }

        return PresenterNodesVersionCombo { std::stoi(match[1].str()),
            std::stoi(match[2].str()), arch.value() };
    }

    const std::regex genericPattern(R"(([0-9]+)\.([0-9]+))");
    if (!std::regex_search(isoString, match, genericPattern)) {
        return std::nullopt;
    }

    return PresenterNodesVersionCombo { std::stoi(match[1].str()),
        std::stoi(match[2].str()), arch.value() };
}

auto isoMatchesDistro(const OS::Distro distro, std::string_view isoName) -> bool
{
    switch (distro) {
        case OS::Distro::RHEL:
            return isoName.contains("rhel");
        case OS::Distro::OL:
            return isoName.contains("OracleLinux");
        case OS::Distro::Rocky:
            return isoName.contains("Rocky");
        case OS::Distro::AlmaLinux:
            return isoName.contains("AlmaLinux");
    }

    return false;
}

auto isoSearchToken(const OS::Distro distro) -> std::string_view
{
    switch (distro) {
        case OS::Distro::RHEL:
            return "rhel";
        case OS::Distro::OL:
            return "OracleLinux";
        case OS::Distro::Rocky:
            return "Rocky";
        case OS::Distro::AlmaLinux:
            return "AlmaLinux";
    }

    return {};
}

auto exampleIsoName(const OS::Distro distro) -> std::string
{
    const auto [major, minor, arch] = defaultVersionComboFor(distro);
    const auto architecture = opencattus::utils::enums::toString(arch);

    switch (distro) {
        case OS::Distro::RHEL:
            return fmt::format(
                "rhel-{}.{}-{}-dvd.iso", major, minor, architecture);
        case OS::Distro::OL:
            return fmt::format(
                "OracleLinux-R{}-U{}-{}-dvd.iso", major, minor, architecture);
        case OS::Distro::Rocky:
            return fmt::format(
                "Rocky-{}.{}-{}-dvd.iso", major, minor, architecture);
        case OS::Distro::AlmaLinux:
            return fmt::format(
                "AlmaLinux-{}.{}-{}-dvd.iso", major, minor, architecture);
    }

    return {};
}

auto formatNoMatchingIsoMessage(
    const fs::path& directory, const OS::Distro distro) -> std::string
{
    return fmt::format(
        "No ISO matching the selected distribution was found in the "
        "provided directory.\n\nDirectory: {}\nLooked for: *.iso "
        "filenames containing \"{}\"\nExample: {}",
        directory.string(), isoSearchToken(distro), exampleIsoName(distro));
}

auto findMatchingIsos(const fs::path& isoRoot, OS::Distro distro)
    -> std::optional<std::vector<std::string>>
{
    std::error_code ec;
    if (!fs::is_directory(isoRoot, ec) || ec) {
        return std::nullopt;
    }

    std::vector<std::string> isos;
    fs::directory_iterator entry(isoRoot, ec);
    if (ec) {
        return std::nullopt;
    }

    for (const fs::directory_iterator end; entry != end; entry.increment(ec)) {
        if (ec) {
            return std::nullopt;
        }

        std::error_code fileEc;
        if (!entry->is_regular_file(fileEc) || fileEc) {
            continue;
        }

        const auto formattedIsoName = entry->path().filename().string();
        if (entry->path().extension() == ".iso"
            && isoMatchesDistro(distro, formattedIsoName)) {
            isos.emplace_back(formattedIsoName);
        }
    }

    return isos;
}

} // namespace

namespace opencattus::presenter {

std::string PresenterNodesOperationalSystem::getDownloadURL(
    OS::Distro distro, PresenterNodesVersionCombo version)
{
    auto [majorVersion, minorVersion, arch] = version;

    fmt::dynamic_format_arg_store<fmt::format_context> store;
    store.push_back(fmt::arg("arch", opencattus::utils::enums::toString(arch)));
    store.push_back(fmt::arg("major", majorVersion));
    store.push_back(fmt::arg("minor", minorVersion));

    switch (distro) {
        case OS::Distro::RHEL:
            throw std::runtime_error(
                "We does not support RHEL ISO download yet!");
        case OS::Distro::OL:
            return fmt::vformat("https://yum.oracle.com/ISOS/OracleLinux/"
                                "OL{major}/u{minor}/{arch}/"
                                "OracleLinux-R{major}-U{minor}-{arch}-dvd.iso",
                store);
        case OS::Distro::Rocky:
            return fmt::vformat(
                "https://download.rockylinux.org/pub/rocky/{major}/"
                "isos/{arch}/Rocky-{major}.{minor}-{arch}-dvd.iso",
                store);
        case OS::Distro::AlmaLinux:
            return fmt::vformat("https://repo.almalinux.org/almalinux/"
                                "{major}.{minor}/isos/{arch}/"
                                "AlmaLinux-{major}.{minor}-{arch}-dvd.iso",
                store);
    }

    return "?";
}

PresenterNodesVersionCombo PresenterNodesOperationalSystem::promptVersion(
    OS::Distro distro, std::optional<PresenterNodesVersionCombo> initial)
{
    auto [defaultMajor, defaultMinor, defaultArch]
        = initial.value_or(defaultVersionComboFor(distro));

    auto metadata = std::to_array<std::pair<std::string, std::string>>(
        { { Messages::OperationalSystemVersion::version,
              fmt::format("{}.{}", defaultMajor, defaultMinor) },
            { Messages::OperationalSystemVersion::architecture,
                opencattus::utils::enums::toString(defaultArch) } });

    while (true) {
        metadata = m_view->fieldMenu(Messages::title,
            Messages::OperationalSystemVersion::question, metadata,
            Messages::OperationalSystemVersion::help);

        const auto parsedVersion = parseVersionString(metadata[0].second);
        if (!parsedVersion.has_value()) {
            m_view->message(Messages::title,
                Messages::OperationalSystemVersion::invalidVersion);
            continue;
        }

        const auto parsedArch = parseArchitecture(metadata[1].second);
        if (!parsedArch.has_value()) {
            m_view->message(Messages::title,
                Messages::OperationalSystemVersion::invalidArch);
            continue;
        }

        return PresenterNodesVersionCombo { parsedVersion->first,
            parsedVersion->second, parsedArch.value() };
    }
}

PresenterNodesOperationalSystem::PresenterNodesOperationalSystem(
    std::unique_ptr<Cluster>& model, std::unique_ptr<View>& view)
    : Presenter(model, view)
{
    std::vector<std::string> distroNames;
    distroNames.emplace_back("Red Hat Enterprise Linux");
    distroNames.emplace_back("AlmaLinux");
    distroNames.emplace_back("Rocky Linux");
    distroNames.emplace_back("Oracle Linux");

    std::map<std::string, OS::Distro> distros;
    distros["Red Hat Enterprise Linux"] = OS::Distro::RHEL;
    distros["AlmaLinux"] = OS::Distro::AlmaLinux;
    distros["Rocky Linux"] = OS::Distro::Rocky;
    distros["Oracle Linux"] = OS::Distro::OL;

    const auto downloadSelectedDistro
        = [&](const std::string& distroName, OS::Distro distro) -> bool {
        if (distro == OS::Distro::RHEL) {
            m_view->message(
                Messages::title, Messages::OperationalSystemVersion::rhelError);
            return false;
        }

        const auto versioncombo = promptVersion(distro);
        std::string distroDownloadURL = getDownloadURL(distro, versioncombo);
        std::string isoName
            = distroDownloadURL.substr(distroDownloadURL.find_last_of('/') + 1);

        const auto opts
            = opencattus::Singleton<const opencattus::services::Options>::get();
        if (opts->dryRun) {
            LOG_INFO("Dry Run: Would download {} from {}",
                fmt::format("/root/{}", isoName), distroDownloadURL);
        } else {
            //@TODO Implement newt GUI progress bar
            auto command = Singleton<IRunner>::get()->executeCommandIter(
                fmt::format("wget -NP /root {}", distroDownloadURL),
                opencattus::services::Stream::Stderr);

            auto desc = fmt::format(
                Messages::OperationalSystemDownloadIso::Progress::download,
                distroName, distroDownloadURL);
            m_view->progressMenu(Messages::title, desc.c_str(),
                std::move(command),
                [&](opencattus::services::CommandProxy& cmd)
                    -> std::optional<double> {
                    auto out = cmd.getline();
                    if (!out) {
                        return std::nullopt;
                    }
                    std::string line = *out;

                    // If we have a line like ERROR 404: Not Found
                    // this means, obviously, that we did not found the URL.
                    if (line.contains("ERROR 404: Not Found")) {
                        LOG_ERROR("URL {} not found", distroDownloadURL);
                        return std::nullopt;
                    }

                    // Line example
                    //  <<<338950K .......... .......... ..........
                    //  ..........
                    //  ..........  3% 31.8M 10m40s>>

                    // TODO: (on the progress bar) maybe allow altering some
                    // menu parameters (like the text)
                    std::vector<std::string> slots;

                    boost::split(slots, line, boost::is_any_of("\t\r "),
                        boost::token_compress_on);

                    if (slots.size() <= 6) {
                        return std::make_optional(0.0);
                    }

                    auto num = slots[6].substr(0, slots[6].find_first_of('%'));

                    try {
                        return std::make_optional(
                            boost::lexical_cast<double>(num));
                    } catch (boost::bad_lexical_cast&) {
                        return std::make_optional(0.0);
                    }
                });
        }

        m_model->setDiskImage(fmt::format("/root/{}", isoName));
        m_model->setComputeNodeOS(makeOperatingSystem(distro, versioncombo));
        LOG_DEBUG("Selected ISO: {}", fmt::format("/root/{}", isoName))
        return true;
    };

    const auto downloadIso = m_view->yesNoQuestion(Messages::title,
        Messages::OperationalSystemDownloadIso::FirstStage::question,
        Messages::OperationalSystemDirectoryPath::help);

    if (downloadIso) {
        while (true) {
            auto distroToDownload = m_view->listMenu(Messages::title,
                Messages::OperationalSystemDownloadIso::SecondStage::question,
                distroNames,
                Messages::OperationalSystemDownloadIso::SecondStage::help);

            const auto selectedDistro = distros.find(distroToDownload);
            LOG_ASSERT(selectedDistro != distros.end(),
                "selected distribution missing");
            if (downloadSelectedDistro(
                    selectedDistro->first, selectedDistro->second)) {
                break;
            }
        }

    } else {
        auto isoDirectoryPath
            = std::to_array<std::pair<std::string, std::string>>(
                { { Messages::OperationalSystemDirectoryPath::field,
                    "/mnt/iso" } });

        bool selectedDiskImage = false;
        while (!selectedDiskImage) {
            while (true) {
                isoDirectoryPath = m_view->fieldMenu(Messages::title,
                    Messages::OperationalSystemDirectoryPath::question,
                    isoDirectoryPath,
                    Messages::OperationalSystemDirectoryPath::help);

                std::error_code ec;
                if (std::filesystem::is_directory(
                        isoDirectoryPath.data()->second, ec)
                    && !ec) {
                    break;
                }

                m_view->message(Messages::title,
                    std::filesystem::exists(isoDirectoryPath.data()->second)
                        ? Messages::OperationalSystemDirectoryPath::notReadable
                        : Messages::OperationalSystemDirectoryPath::
                              nonExistent);
            }

            LOG_DEBUG("ISO directory path set to {}",
                isoDirectoryPath.data()->second);

            while (true) {
                auto selectedDistroName = m_view->listMenu(Messages::title,
                    Messages::OperationalSystemDistro::question, distroNames,
                    Messages::OperationalSystemDistro::help);

                const auto selectedDistro = distros.find(selectedDistroName);
                LOG_ASSERT(selectedDistro != distros.end(),
                    "selected distribution missing");

                const auto isoRoot = fs::path(isoDirectoryPath.data()->second);
                auto isos = findMatchingIsos(isoRoot, selectedDistro->second);
                if (!isos.has_value()) {
                    m_view->message(Messages::title,
                        Messages::OperationalSystemDirectoryPath::notReadable);
                    break;
                }

                if (isos->empty()) {
                    const auto noneFoundMessage = formatNoMatchingIsoMessage(
                        isoRoot, selectedDistro->second);
                    m_view->message(Messages::title, noneFoundMessage.c_str());
                    if (m_view->yesNoQuestion(Messages::title,
                            Messages::OperationalSystem::downloadMissing,
                            Messages::OperationalSystem::help)
                        && downloadSelectedDistro(
                            selectedDistro->first, selectedDistro->second)) {
                        selectedDiskImage = true;
                    }
                    break;
                }

                auto selectedIso = m_view->listMenu(Messages::title,
                    Messages::OperationalSystem::question, isos.value(),
                    Messages::OperationalSystem::help);

                const auto selectedIsoPath
                    = fmt::format("{}/{}", isoRoot.string(), selectedIso);
                m_model->setDiskImage(selectedIsoPath);
                const auto versioncombo = promptVersion(selectedDistro->second,
                    inferVersionComboFromIso(
                        selectedDistro->second, selectedIso));
                m_model->setComputeNodeOS(
                    makeOperatingSystem(selectedDistro->second, versioncombo));
                LOG_DEBUG("Selected ISO: {}", selectedIsoPath);
                selectedDiskImage = true;
                break;
            }
        }
    }
}

}; // namespace opencattus::presenter

TEST_CASE("inferVersionComboFromIso parses Rocky ISO names")
{
    const auto inferred = inferVersionComboFromIso(
        OS::Distro::Rocky, "Rocky-9.6-x86_64-dvd.iso");

    REQUIRE(inferred.has_value());
    const PresenterNodesVersionCombo expected { 9, 6, OS::Arch::x86_64 };
    CHECK(inferred.value() == expected);
}

TEST_CASE("inferVersionComboFromIso parses Oracle Linux ISO names")
{
    const auto inferred = inferVersionComboFromIso(
        OS::Distro::OL, "OracleLinux-R9-U7-x86_64-dvd.iso");

    REQUIRE(inferred.has_value());
    const PresenterNodesVersionCombo expected { 9, 7, OS::Arch::x86_64 };
    CHECK(inferred.value() == expected);
}

TEST_CASE("makeOperatingSystem maps major versions to supported platforms")
{
    const auto os
        = makeOperatingSystem(OS::Distro::Rocky, { 10, 1, OS::Arch::x86_64 });

    CHECK(os.getPlatform() == OS::Platform::el10);
    CHECK(os.getVersion() == "10.1");
}
