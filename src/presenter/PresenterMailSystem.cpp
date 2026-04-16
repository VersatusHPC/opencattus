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

        auto commonFields = std::to_array<std::pair<std::string, std::string>>(
            { { Messages::Common::destination, "" },
                { Messages::Common::certFile, "" },
                { Messages::Common::keyFile, "" } });
        commonFields = m_view->fieldMenu(Messages::title,
            Messages::Common::question, commonFields, Messages::Common::help);

        if (auto destinations
            = parseCommaSeparatedValues(commonFields[0].second);
            !destinations.empty()) {
            mailSystem.setDestination(std::move(destinations));
        }

        if (!commonFields[1].second.empty()) {
            mailSystem.setCertFile(
                std::filesystem::path(commonFields[1].second));
        }

        if (!commonFields[2].second.empty()) {
            mailSystem.setKeyFile(
                std::filesystem::path(commonFields[2].second));
        }

        switch (mailSystemProfile) {
            case Postfix::Profile::Local: {
                break;
            }

            case Postfix::Profile::Relay: {
                auto fields
                    = std::to_array<std::pair<std::string, std::string>>(
                        { { Messages::Relay::hostname, "" },
                            { Messages::Relay::port, "25" } });

                fields = m_view->fieldMenu(Messages::title,
                    Messages::Relay::question, fields, Messages::Relay::help);

                std::size_t i { 0 };
                mailSystem.setSMTPServer(fields[i++].second);
                mailSystem.setPort(
                    boost::lexical_cast<uint16_t>(fields[i++].second));

                LOG_DEBUG("Set Postfix Relay: {}:{}",
                    mailSystem.getSMTPServer().value(),
                    mailSystem.getPort().value());

                break;
            }

            case Postfix::Profile::SASL: {
                auto fields
                    = std::to_array<std::pair<std::string, std::string>>(
                        { { Messages::SASL::hostname, "" },
                            { Messages::SASL::port, "587" },
                            { Messages::SASL::username, "" },
                            { Messages::SASL::password, "" } });

                fields = m_view->fieldMenu(Messages::title,
                    Messages::SASL::question, fields, Messages::SASL::help);

                std::size_t i { 0 };
                mailSystem.setSMTPServer(fields[i++].second);
                mailSystem.setPort(
                    boost::lexical_cast<uint16_t>(fields[i++].second));
                mailSystem.setUsername(fields[i++].second);
                mailSystem.setPassword(fields[i++].second);

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
