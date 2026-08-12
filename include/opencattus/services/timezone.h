/*
 * Copyright 2022 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OPENCATTUS_TIMEZONE_H_
#define OPENCATTUS_TIMEZONE_H_

#include <list>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @class Timezone
 * @brief Manages the system timezone and time servers.
 *
 * This class provides functionalities to set and get the system timezone,
 * manage available timezones, and configure time servers.
 */
class Timezone {
private:
    std::string m_timezone;
    std::string m_timezoneArea;
    std::multimap<std::string, std::string> m_availableTimezones;
    std::multimap<std::string, std::string> m_availableTimezoneAreas;
    // TODO: IP/hostname parsing
    std::vector<std::string> m_timeservers;

public:
    Timezone();
    ~Timezone() = default;

    /**
     * @brief Sets the current timezone.
     *
     * @param timezone The timezone to set, as a string view.
     */
    void setTimezone(std::string_view);

    /**
     * @brief Gets the current timezone.
     *
     * @return The current timezone as a string view.
     */
    std::string_view getTimezone() const;

    /**
     * @brief Sets the system timezone.
     *
     * This function configures the system's timezone based on the current
     * settings.
     */
    void setSystemTimezone();

    /**
     * @brief Gets the list of available timezones.
     *
     * @return A multimap of available timezones.
     */
    std::multimap<std::string, std::string> getAvailableTimezones() const;

    /**
     * @brief Fetches the list of available timezones.
     *
     * This function retrieves the available timezones from the system or a
     * database.
     *
     * @return A multimap of available timezones.
     */
    std::multimap<std::string, std::string> fetchAvailableTimezones();

#ifdef BUILD_TESTING
    // Test-only seam: when set, fetchAvailableTimezones() parses the override
    // lines as the zone1970.tab contents instead of reading the host's
    // /usr/share/zoneinfo/zone1970.tab. An empty override still exercises the
    // timedatectl fallback through the runner singleton. Bare CI hosts carry
    // real tzdata state that would otherwise leak into tests expecting
    // synthetic timezones. Use Timezone::ScopedTestZone1970Tab for RAII
    // teardown rather than poking this directly.
    static std::optional<std::vector<std::string>> s_testZone1970Override;

    class ScopedTestZone1970Tab {
    public:
        explicit ScopedTestZone1970Tab(std::vector<std::string> lines)
            : m_previous(std::move(s_testZone1970Override))
        {
            s_testZone1970Override = std::move(lines);
        }
        ScopedTestZone1970Tab(const ScopedTestZone1970Tab&) = delete;
        ScopedTestZone1970Tab& operator=(const ScopedTestZone1970Tab&) = delete;
        ScopedTestZone1970Tab(ScopedTestZone1970Tab&&) = delete;
        ScopedTestZone1970Tab& operator=(ScopedTestZone1970Tab&&) = delete;
        ~ScopedTestZone1970Tab()
        {
            s_testZone1970Override = std::move(m_previous);
        }

    private:
        std::optional<std::vector<std::string>> m_previous;
    };
#endif

    void setTimezoneArea(std::string_view);
    std::string_view getTimezoneArea() const;

    void setTimeservers(const std::vector<std::string>& timeservers);
    void setTimeservers(const std::string& timeservers);
    std::vector<std::string> getTimeservers();
};

#endif // OPENCATTUS_TIMEZONE_H_
