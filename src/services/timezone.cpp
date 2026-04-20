/*
 * Copyright 2022 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fmt/format.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <opencattus/functions.h>
#include <opencattus/services/log.h>
#include <opencattus/services/options.h>
#include <opencattus/services/timezone.h>
#include <opencattus/utils/singleton.h>
#include <string>
#include <string_view>
#include <vector>

using namespace opencattus;

namespace {
constexpr std::string_view zone1970TabEnv = "OPENCATTUS_ZONE1970_TAB";
constexpr std::string_view zone1970TabPath = "/usr/share/zoneinfo/zone1970.tab";
constexpr std::string_view timedatectlTimezoneCommand
    = "timedatectl list-timezones --no-pager";

std::vector<std::string> parseZone1970Tab(const std::vector<std::string>& lines)
{
    std::vector<std::string> zones;
    for (const auto& line : lines) {
        if (line.empty() || line.starts_with('#')) {
            continue;
        }

        const auto firstTab = line.find('\t');
        if (firstTab == std::string::npos) {
            continue;
        }

        const auto secondTab = line.find('\t', firstTab + 1);
        if (secondTab == std::string::npos) {
            continue;
        }

        const auto thirdTab = line.find('\t', secondTab + 1);
        zones.emplace_back(line.substr(secondTab + 1,
            thirdTab == std::string::npos ? std::string::npos
                                          : thirdTab - secondTab - 1));
    }

    return zones;
}

void insertTimezone(
    std::multimap<std::string, std::string>& timezones, const std::string& tz)
{
    if (tz.empty()) {
        return;
    }

    const auto slash = tz.find('/');
    if (slash == std::string::npos) {
        timezones.insert({ tz, "" });
        return;
    }

    timezones.insert({ tz.substr(0, slash), tz.substr(slash + 1) });
}

std::filesystem::path systemZone1970TabPath()
{
    if (const auto* overridePath = std::getenv(zone1970TabEnv.data());
        overridePath != nullptr && std::string_view(overridePath).size() > 0) {
        return overridePath;
    }

    return zone1970TabPath;
}

std::vector<std::string> readZone1970Tab()
{
    const auto path = systemZone1970TabPath();
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_DEBUG("Unable to read {}, falling back to timedatectl",
            path.string());
        return {};
    }

    std::vector<std::string> lines;
    for (std::string line; std::getline(file, line);) {
        lines.emplace_back(std::move(line));
    }

    if (file.bad()) {
        LOG_WARN("Error while reading {}, falling back to timedatectl",
            path.string());
        return {};
    }

    return parseZone1970Tab(lines);
}
}

Timezone::Timezone()
    : m_availableTimezones { fetchAvailableTimezones() }
{
}

// TODO: Check against m_availableTimezones and throw if not found
void Timezone::setTimezone(std::string_view tz) { m_timezone = tz; }

std::string_view Timezone::getTimezone() const { return m_timezone; }

void Timezone::setTimezoneArea(std::string_view tz) { m_timezoneArea = tz; }

std::string_view Timezone::getTimezoneArea() const { return m_timezoneArea; }

void Timezone::setSystemTimezone()
{
    LOG_DEBUG("Setting system timezone to {}\n", m_timezone)
    auto runner = opencattus::Singleton<functions::IRunner>::get();
    runner->executeCommand(
        fmt::format("timedatectl set timezone {}", m_timezone));
}

std::multimap<std::string, std::string> Timezone::getAvailableTimezones() const
{
    return m_availableTimezones;
}

std::multimap<std::string, std::string> Timezone::fetchAvailableTimezones()
{
    std::multimap<std::string, std::string> timezones {};

    LOG_DEBUG("Fetching available system timezones")
    auto runner = opencattus::Singleton<functions::IRunner>::get();
    auto output = readZone1970Tab();
    if (output.empty()) {
        output = runner->checkOutput(std::string(timedatectlTimezoneCommand));
    }

    for (const std::string& tz : output) {
        insertTimezone(timezones, tz);
    }

    return timezones;
}

void Timezone::setTimeservers(const std::vector<std::string>& timeservers)
{
    m_timeservers.reserve(timeservers.size());

    for (const auto& timeserver : timeservers)
        m_timeservers.emplace_back(timeserver);
}

// TODO: Check for correctness in timeservers (use hostname/IP check)
// TODO: Remove std::stringstream
// std::stringstream does not support string_view
void Timezone::setTimeservers(const std::string& timeservers)
{
    std::stringstream stream { timeservers };

    while (stream.good()) {
        std::string substring;
        std::getline(stream, substring, ',');

        // Remove spaces from substring
        substring.erase(std::remove(substring.begin(), substring.end(), ' '),
            substring.end());

        m_timeservers.emplace_back(substring);
    }
}

std::vector<std::string> Timezone::getTimeservers() { return m_timeservers; }
