/*
 * Copyright 2023 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OPENCATTUS_PRESENTERNODESOPERATIONALSYSTEM_H_
#define OPENCATTUS_PRESENTERNODESOPERATIONALSYSTEM_H_

#include <opencattus/presenter/Presenter.h>
#include <optional>
#include <tuple>

namespace opencattus::presenter {

using PresenterNodesVersionCombo
    = std::tuple<int, int, OS::Arch>; // major, minor

class PresenterNodesOperationalSystem : public Presenter {
private:
    struct Messages {
        static constexpr const auto title = "Nodes operational system settings";

        struct OperationalSystemDownloadIso {
            struct FirstStage {
                static constexpr const auto question
                    = "You want to download a ISO for your node?";
                static constexpr const auto help
                    = "Choose 'YES' if you want to download a new one or 'NO' "
                      "if you already have an ISO.";
            };
            struct SecondStage {
                static constexpr const auto question
                    = "Choose an ISO to download";
                static constexpr const auto help
                    = Presenter::Messages::Placeholder::help;
            };
            struct Progress {
                static constexpr const auto download
                    = "Downloading ISO from {0}\nSource: {1}";
            };
        };

        struct OperationalSystemDirectoryPath {
            static constexpr const auto question
                = "Inform the directory where your operational system images "
                  "are";
            static constexpr const auto help
                = Presenter::Messages::Placeholder::help;
            static constexpr const auto field = "Path to ISOs directory:";

            static constexpr const auto nonExistent
                = "The specified directory do not exist";
            static constexpr const auto notReadable
                = "The specified path is not a readable directory";
        };

        struct OperationalSystemDistro {
            static constexpr const auto question
                = "Choose your operational system distro";
            static constexpr const auto help
                = Presenter::Messages::Placeholder::help;
        };

        struct OperationalSystemVersion {
            static constexpr const auto question
                = "Enter the distro version and architecture";
            static constexpr const auto rhelError
                = "Unfortunately, we do not support downloading Red Hat "
                  "Enterprise Linux yet.\n"
                  "Please download the ISO yourself and put in an appropriate "
                  "location.";
            static constexpr const auto invalidVersion
                = "Use the version format MAJOR.MINOR, for example 9.6";
            static constexpr const auto invalidArch
                = "Supported architectures are x86_64 and ppc64le";
            static constexpr const auto help
                = Presenter::Messages::Placeholder::help;

            static constexpr const auto version = "Version";
            static constexpr const auto architecture = "Architecture";
        };

        struct OperationalSystem {
            static constexpr const auto question
                = "Choose your operational system ISO";
            static constexpr const auto help
                = Presenter::Messages::Placeholder::help;
            static constexpr const auto noneFound
                = "No ISO matching the selected distro was found in the "
                  "provided directory";
            static constexpr const auto downloadMissing
                = "Download an ISO for this distro instead? Choose No to enter "
                  "another directory.";
        };
    };

    PresenterNodesVersionCombo promptVersion(OS::Distro distro,
        std::optional<PresenterNodesVersionCombo> initial = std::nullopt);
    std::string getDownloadURL(
        OS::Distro distro, PresenterNodesVersionCombo version);

public:
    PresenterNodesOperationalSystem(
        std::unique_ptr<Cluster>& model, std::unique_ptr<View>& view);
};

};

#endif // OPENCATTUS_PRESENTERNODESOPERATIONALSYSTEM_H_
