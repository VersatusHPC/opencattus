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
        static constexpr const auto title = "Compute node OS settings";

        struct OperationalSystemDownloadIso {
            struct FirstStage {
                static constexpr const auto question
                    = "Download an ISO for the compute nodes?";
                static constexpr const auto help
                    = "Choose Yes to download a supported installation DVD, "
                      "or No to select an ISO already available on this host.";
            };
            struct SecondStage {
                static constexpr const auto question
                    = "Choose an ISO to download";
                static constexpr const auto help
                    = "Select the distribution installed on compute nodes. Red "
                      "Hat Enterprise Linux must be provided as a local ISO.";
            };
            struct Progress {
                static constexpr const auto download
                    = "Downloading ISO from {0}\nSource: {1}";
            };
        };

        struct OperationalSystemDirectoryPath {
            static constexpr const auto question
                = "Enter the directory containing installation ISO images";
            static constexpr const auto help
                = "Provide a readable directory path. The next screen lists "
                  "matching ISO files for the selected distribution.";
            static constexpr const auto field = "Path to ISOs directory:";

            static constexpr const auto nonExistent
                = "The specified directory does not exist";
            static constexpr const auto notReadable
                = "The specified path is not a readable directory";
        };

        struct OperationalSystemDistro {
            static constexpr const auto question
                = "Choose the operating system distribution";
            static constexpr const auto help
                = "Select the distribution family for the compute node image. "
                  "This controls ISO matching, repository selection, and "
                  "provisioner defaults.";
        };

        struct OperationalSystemVersion {
            static constexpr const auto question
                = "Enter the distribution version and architecture";
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
                = "Enter the OS version and CPU architecture for the compute "
                  "node image. The version should match the ISO filename.";

            static constexpr const auto version = "Version";
            static constexpr const auto architecture = "Architecture";
        };

        struct OperationalSystem {
            static constexpr const auto question
                = "Choose the operating system ISO";
            static constexpr const auto help
                = "Select the installation DVD ISO to import into the "
                  "provisioner. Boot, minimal, and live images are not "
                  "suitable.";
            static constexpr const auto noneFound
                = "No ISO matching the selected distribution was found in the "
                  "provided directory";
            static constexpr const auto downloadMissing
                = "Download an ISO for this distribution instead? Choose No "
                  "to enter another directory.";
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
