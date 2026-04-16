/*
 * Copyright 2022 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <opencattus/functions.h>
#include <opencattus/presenter/PresenterMailSystem.h>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/lexical_cast.hpp>
#include <filesystem>

namespace {

auto parseCommaSeparatedValues(std::string_view raw) -> std::vector<std::string>
{
    std::vector<std::string> parts;
    std::vector<std::string> values;
    boost::split(parts, std::string(raw), boost::is_any_of(", "),
        boost::token_compress_on);

    for (auto& part : parts) {
        if (!part.empty()) {
            values.push_back(std::move(part));
        }
    }

    return values;
}

} // namespace

using opencattus::services::Postfix;

namespace opencattus::presenter {
PresenterMailSystem::PresenterMailSystem(
    std::unique_ptr<Cluster>& model, std::unique_ptr<View>& view)
    : Presenter(model, view)
{

    if (m_view->yesNoQuestion(
            Messages::title, Messages::question, Messages::help)) {

        Postfix::Profile mailSystemProfile
            = opencattus::utils::enums::ofStringOpt<Postfix::Profile>(
                m_view->listMenu(Messages::title, Messages::Profile::question,
                    opencattus::utils::enums::toStrings<Postfix::Profile>(),
                    Messages::Profile::help))
                  .value();
        m_model->setMailSystem(mailSystemProfile);
        auto& mailSystem = m_model->getMailSystem().value();
        mailSystem.setHostname(
            std::string(m_model->getHeadnode().getHostname()));
        mailSystem.setDomain(m_model->getDomainName());

        LOG_DEBUG("Enabled Postfix with profile: {}",
            opencattus::utils::enums::toString<Postfix::Profile>(
                mailSystemProfile));

        constexpr auto commonTitle = Messages::title;
        constexpr auto commonQuestion = Messages::Common::question;
        constexpr auto commonHelp = Messages::Common::help;
        const auto promptCommonFields = [&]() {
            auto commonFields
                = std::to_array<std::pair<std::string, std::string>>(
                    { { Messages::Common::destination, "" } });
            return m_view->fieldMenu(
                commonTitle, commonQuestion, commonFields, commonHelp);
        };
        const auto commonFields = promptCommonFields();

        if (auto destinations
            = parseCommaSeparatedValues(commonFields[0].second);
            !destinations.empty()) {
            mailSystem.setDestination(std::move(destinations));
        }

        if (m_view->yesNoQuestion(Messages::title,
                Messages::Common::tlsOverrideQuestion,
                Messages::Common::help)) {
            auto tlsFields = std::to_array<std::pair<std::string, std::string>>(
                { { Messages::Common::certFile, "" },
                    { Messages::Common::keyFile, "" } });
            tlsFields = m_view->fieldMenu(commonTitle,
                Messages::Common::tlsPathsQuestion, tlsFields, commonHelp);

            mailSystem.setCertFile(std::filesystem::path(tlsFields[0].second));
            mailSystem.setKeyFile(std::filesystem::path(tlsFields[1].second));
        }

        switch (mailSystemProfile) {
            case Postfix::Profile::Local: {
                break;
            }

            case Postfix::Profile::Relay: {
                constexpr auto relayQuestion = Messages::Relay::question;
                constexpr auto relayHelp = Messages::Relay::help;
                const auto promptRelayFields = [&]() {
                    auto relayFields
                        = std::to_array<std::pair<std::string, std::string>>(
                            { { "SMTP server", "" }, { "Port", "25" } });
                    return m_view->fieldMenu(
                        commonTitle, relayQuestion, relayFields, relayHelp);
                };
                const auto relayFields = promptRelayFields();

                std::size_t i { 0 };
                mailSystem.setSMTPServer(relayFields[i++].second);
                mailSystem.setPort(
                    boost::lexical_cast<uint16_t>(relayFields[i++].second));

                LOG_DEBUG("Set Postfix Relay: {}:{}",
                    mailSystem.getSMTPServer().value(),
                    mailSystem.getPort().value());

                break;
            }

            case Postfix::Profile::SASL: {
                constexpr auto saslQuestion = Messages::SASL::question;
                constexpr auto saslHelp = Messages::SASL::help;
                const auto promptSaslFields = [&]() {
                    auto saslFields
                        = std::to_array<std::pair<std::string, std::string>>(
                            { { "SMTP server", "" }, { "Port", "587" },
                                { "Username", "" }, { "Password", "" } });
                    return m_view->fieldMenu(
                        commonTitle, saslQuestion, saslFields, saslHelp);
                };
                const auto saslFields = promptSaslFields();

                std::size_t i { 0 };
                mailSystem.setSMTPServer(saslFields[i++].second);
                mailSystem.setPort(
                    boost::lexical_cast<uint16_t>(saslFields[i++].second));
                mailSystem.setUsername(saslFields[i++].second);
                mailSystem.setPassword(saslFields[i++].second);

                LOG_DEBUG(
                    "Set Postfix SASL: {}:{}\nUsername: {} | Password: {}",
                    mailSystem.getSMTPServer().value(),
                    mailSystem.getPort().value(),
                    mailSystem.getUsername().value(),
                    mailSystem.getPassword().value());

                break;
            }
        }

    } else {
        LOG_DEBUG("Postfix wasn't enabled")
    }
}

};
