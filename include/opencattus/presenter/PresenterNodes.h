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
            = "Enter compute node naming, network, and hardware defaults";

        struct Nodes {
            static constexpr const auto question
                = "Enter the compute nodes information";
            static constexpr const auto help
                = "The prefix and padding generate node names such as n01. "
                  "The first compute IP is incremented for each node. Leave "
                  "BMC first IP empty when nodes have no BMC.";

            static constexpr const auto prefix = "Prefix";
            static constexpr const auto padding = "Padding";
            static constexpr const auto startIP = "Compute node first IP";
            static constexpr const auto bmcStartIP = "BMC first IP (optional)";
            static constexpr const auto rootPassword
                = "Compute node root password";
            static constexpr const auto confirmRootPassword
                = "Confirm compute node root password";
        };

        struct Topology {
            static constexpr const auto question
                = "Enter the compute node topology and BMC defaults";
            static constexpr const auto help
                = "Topology values are used to generate scheduler node "
                  "definitions. BMC defaults are copied to each node that has "
                  "a BMC IP address.";

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
            static constexpr const auto paddingInvalid
                = "Padding must be a number between 0 and 3";
            static constexpr const auto startIPInvalid
                = "Compute node first IP must be a valid IP address";
            static constexpr const auto bmcStartIPInvalid
                = "BMC first IP must be a valid IP address";
            static constexpr const auto bmcStartIPMatchesNode
                = "BMC first IP cannot match the compute node first IP";
            static constexpr const auto bmcAddressMatchesNode
                = "BMC IP address cannot match the compute node IP address";
            static constexpr const auto rootPasswordMismatch
                = "The compute node root password confirmation does not match";
            static constexpr const auto topologyInvalid
                = "Topology values must be valid numbers";
            static constexpr const auto quantityInvalid
                = "Node quantity must be a number greater than zero";
        };

        struct Quantity {
            static constexpr const auto question
                = "Enter the compute nodes quantity information";
            static constexpr const auto help
                = "Enter how many racks and nodes per rack should be "
                  "generated. The start number controls the first generated "
                  "hostname.";

            static constexpr const auto racks = "Racks";
            static constexpr const auto nodes = "Nodes";
            static constexpr const auto startNumber = "Node start number";
        };

        struct NodeEntry {
            static constexpr const auto question
                = "Enter the management MAC and BMC IP address for node";
            static constexpr const auto help
                = "The management MAC address is required for provisioning and "
                  "network boot. The BMC IP address is optional and should use "
                  "the service network when one exists.";

            static constexpr const auto macAddress = "Management MAC address";
            static constexpr const auto bmcAddress
                = "BMC IP address (optional)";
        };
    };

public:
    PresenterNodes(
        std::unique_ptr<Cluster>& model, std::unique_ptr<View>& view);
};

};

#endif // OPENCATTUS_PRESENTERNODES_H_
