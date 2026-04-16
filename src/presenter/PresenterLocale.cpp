/*
 * Copyright 2022 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <opencattus/patterns/singleton.h>
#include <opencattus/presenter/PresenterLocale.h>

#include <algorithm>
#include <cctype>
#include <fmt/format.h>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace opencattus::presenter {

namespace {
    constexpr const auto advancedLocaleChoice = "Advanced / legacy locales";

    struct LocaleChoice {
        std::string label;
        std::string locale;
    };

    std::string lower(std::string value)
    {
        std::ranges::transform(value, value.begin(),
            [](unsigned char chr) { return std::tolower(chr); });
        return value;
    }

    bool isUtf8Locale(const std::string& locale)
    {
        const auto lowered = lower(locale);
        const auto dot = lowered.find('.');
        if (dot == std::string::npos) {
            return false;
        }

        const auto modifier = lowered.find('@', dot);
        const auto codeset = lowered.substr(dot + 1,
            modifier == std::string::npos ? std::string::npos
                                          : modifier - dot - 1);
        return codeset == "utf8" || codeset == "utf-8";
    }

    std::string localeWithoutCodeset(const std::string& locale)
    {
        const auto dot = locale.find('.');
        if (dot == std::string::npos) {
            return locale;
        }

        std::string base = locale.substr(0, dot);
        const auto modifier = locale.find('@', dot);
        if (modifier != std::string::npos) {
            base += locale.substr(modifier);
        }

        return base;
    }

    std::string normalizeUtf8Display(const std::string& locale)
    {
        const auto dot = locale.find('.');
        if (dot == std::string::npos) {
            return locale;
        }

        std::string display = locale.substr(0, dot) + ".UTF-8";
        const auto modifier = locale.find('@', dot);
        if (modifier != std::string::npos) {
            display += locale.substr(modifier);
        }

        return display;
    }

    std::string languageCode(const std::string& locale)
    {
        const auto base = localeWithoutCodeset(locale);
        const auto separator = base.find_first_of("_@.");
        return separator == std::string::npos ? base
                                              : base.substr(0, separator);
    }

    std::string languageLabel(const std::string& code)
    {
        if (code == "C" || code == "POSIX") {
            return "C/POSIX";
        }

        const auto lowered = lower(code);
        static const std::map<std::string, std::string> names = {
            { "de", "German" },
            { "en", "English" },
            { "es", "Spanish" },
            { "fr", "French" },
            { "it", "Italian" },
            { "ja", "Japanese" },
            { "ko", "Korean" },
            { "pt", "Portuguese" },
            { "ru", "Russian" },
            { "zh", "Chinese" },
        };

        const auto iter = names.find(lowered);
        if (iter != names.end()) {
            return fmt::format("{} ({})", iter->second, lowered);
        }

        return fmt::format("{} locales", code);
    }
}

PresenterLocale::PresenterLocale(
    std::unique_ptr<Cluster>& model, std::unique_ptr<View>& view)
    : Presenter(model, view)
{
    auto availableLocales
        = opencattus::Singleton<IRunner>::get()->checkOutput("locale -a");
    if (availableLocales.empty()) {
        m_view->fatalMessage(Messages::title,
            "No locales were discovered on this system. Verify that "
            "'locale -a' works and try again.");
    }

    std::map<std::string, std::vector<LocaleChoice>> localeGroups;
    std::vector<std::string> legacyLocales;
    for (const auto& locale : availableLocales) {
        if (isUtf8Locale(locale)) {
            localeGroups[languageLabel(languageCode(locale))].push_back(
                { normalizeUtf8Display(locale), locale });
        } else {
            legacyLocales.push_back(locale);
        }
    }

    if (localeGroups.empty()) {
        const auto& selectedLocale = m_view->listMenu(Messages::title,
            Messages::legacyQuestion, availableLocales, Messages::help);
        m_model->setLocale(selectedLocale);
        LOG_DEBUG("Locale set to: {}", selectedLocale)
        return;
    }

    std::vector<std::string> languageChoices;
    languageChoices.reserve(localeGroups.size() + 1);
    for (auto& [language, locales] : localeGroups) {
        std::ranges::sort(locales, {}, &LocaleChoice::label);
        languageChoices.push_back(language);
    }
    if (!legacyLocales.empty()) {
        languageChoices.emplace_back(advancedLocaleChoice);
    }

    const auto& selectedLanguage = m_view->listMenu(
        Messages::title, Messages::question, languageChoices, Messages::help);

    if (selectedLanguage == advancedLocaleChoice) {
        const auto& selectedLocale = m_view->listMenu(Messages::title,
            Messages::legacyQuestion, legacyLocales, Messages::help);
        m_model->setLocale(selectedLocale);
        LOG_DEBUG("Locale set to: {}", selectedLocale)
        return;
    }

    const auto& localeChoices = localeGroups.at(selectedLanguage);
    std::string selectedLocale;
    if (localeChoices.size() == 1) {
        selectedLocale = localeChoices.front().locale;
    } else {
        std::vector<std::string> regionChoices;
        std::unordered_map<std::string, std::string> localeByLabel;
        regionChoices.reserve(localeChoices.size());
        for (const auto& locale : localeChoices) {
            regionChoices.push_back(locale.label);
            localeByLabel.emplace(locale.label, locale.locale);
        }

        const auto& selectedRegion = m_view->listMenu(Messages::title,
            Messages::regionQuestion, regionChoices, Messages::help);
        selectedLocale = localeByLabel.at(selectedRegion);
    }

    m_model->setLocale(selectedLocale);
    LOG_DEBUG("Locale set to: {}", selectedLocale)
}

};
