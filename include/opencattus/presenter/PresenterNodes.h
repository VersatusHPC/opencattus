/*
 * Copyright 2022 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OPENCATTUS_PRESENTERNODES_H_
#define OPENCATTUS_PRESENTERNODES_H_

#include <opencattus/presenter/Presenter.h>

namespace opencattus::presenter {

class PresenterNodes : public Presenter {
private:
    struct Messages {
        static constexpr const auto title = "Compute nodes settings";
        static constexpr const auto message
            = "We will now gather information to fill your compute nodes data";

        struct Nodes {
            static constexpr const auto question
                = "Enter the compute nodes information";
            static constexpr const auto help
                = Presenter::Messages::Placeholder::help;

            static constexpr const auto prefix = "Prefix";
            static constexpr const auto padding = "Padding";
            static constexpr const auto startIP = "Compute node first IP";
            static constexpr const auto rootPassword
                = "Compute node root password";
            static constexpr const auto confirmRootPassword
                = "Confirm compute node root password";
        };

        struct Topology {
            static constexpr const auto question
                = "Enter the compute node topology and BMC defaults";
            static constexpr const auto help
                = Presenter::Messages::Placeholder::help;

            static constexpr const auto sockets = "Sockets";
            static constexpr const auto coresPerSocket = "Cores per socket";
            static constexpr const auto threadsPerCore = "Threads per core";
            static constexpr const auto realMemory = "Real memory (MiB)";
            static constexpr const auto bmcUsername = "Generic BMC username";
            static constexpr const auto bmcPassword = "Generic BMC password";
            static constexpr const auto bmcSerialPort = "BMC serial port";
            static constexpr const auto bmcSerialSpeed = "BMC serial speed";
        };

        struct Error {
            static constexpr const auto prefixLetter
                = "Prefix must start with a letter";
            static constexpr const auto paddingMax
                = "We can only support up to 1000 nodes";
            static constexpr const auto rootPasswordMismatch
                = "The compute node root password confirmation does not match";
        };

        struct Quantity {
            static constexpr const auto question
                = "Enter the compute nodes quantity information";
            static constexpr const auto help
                = Presenter::Messages::Placeholder::help;

            static constexpr const auto racks = "Racks";
            static constexpr const auto nodes = "Nodes";
            static constexpr const auto startNumber = "Node start number";
        };

        struct NodeEntry {
            static constexpr const auto question
                = "Enter the management MAC and BMC address for node";
            static constexpr const auto help
                = Presenter::Messages::Placeholder::help;

            static constexpr const auto macAddress = "Management MAC address";
            static constexpr const auto bmcAddress = "BMC address";
        };
    };

public:
    PresenterNodes(
        std::unique_ptr<Cluster>& model, std::unique_ptr<Newt>& view);
};

};

#endif // OPENCATTUS_PRESENTERNODES_H_
