/*
 * Copyright 2022 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OPENCATTUS_PRESENTERMAILSYSTEM_H_
#define OPENCATTUS_PRESENTERMAILSYSTEM_H_

#include <opencattus/presenter/Presenter.h>

namespace opencattus::presenter {
class PresenterMailSystem : public Presenter {
private:
    struct Messages {
        static constexpr const auto title = "Mail system settings";
        static constexpr const auto question
            = "Do you want to enable Postfix mail system?";
        static constexpr const auto help
            = "Enable Postfix when the head node should deliver local service "
              "mail or relay notifications through another mail server.";

        struct Profile {
            static constexpr const auto question
                = "Choose a profile for mail delivery";
            static constexpr const auto help
                = "Local keeps mail on the head node. Relay sends through an "
                  "SMTP server. SASL is relay mode with authentication.";
        };

        struct Common {
            static constexpr const auto question
                = "Enter optional Postfix delivery settings";
            static constexpr const auto help
                = "Additional domains may be left empty. TLS paths are only "
                  "needed when overriding the generated default certificate "
                  "configuration.";

            static constexpr const auto destination
                = "Additional domains (optional)";
            static constexpr const auto tlsOverrideQuestion
                = "Do you want to override the default TLS certificate paths?";
            static constexpr const auto tlsPathsQuestion
                = "Enter the TLS certificate paths to override the defaults";
            static constexpr const auto certFile = "TLS certificate file";
            static constexpr const auto keyFile = "TLS key file";
        };

        struct Relay {
            static constexpr const auto question
                = "Enter the destination MTA information to relay messages";
            static constexpr const auto help
                = "Enter the SMTP server that accepts relay mail from the head "
                  "node. Port 25 is typical for unauthenticated relay.";

            static constexpr const auto hostname = "SMTP server";
            static constexpr const auto port = "Port";
        };

        struct SASL {
            static constexpr const auto question
                = "Enter the mail server and user information to deliver "
                  "messages";
            static constexpr const auto help
                = "Enter the SMTP server and credentials for authenticated "
                  "mail delivery. Port 587 is typical for submission.";

            static constexpr const auto hostname = "SMTP server";
            static constexpr const auto port = "Port";
            static constexpr const auto username = "Username";
            static constexpr const auto password = "Password";
        };
    };

public:
    PresenterMailSystem(
        std::unique_ptr<Cluster>& model, std::unique_ptr<View>& view);
};

};

#endif // OPENCATTUS_PRESENTERMAILSYSTEM_H_
