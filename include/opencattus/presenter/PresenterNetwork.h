/*
 * Copyright 2022 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OPENCATTUS_PRESENTERNETWORK_H_
#define OPENCATTUS_PRESENTERNETWORK_H_

#include <opencattus/presenter/Presenter.h>

#include <boost/asio.hpp>
#include <opencattus/models/cluster.h>
#include <opencattus/network.h>
#include <opencattus/services/log.h>
#include <opencattus/view/view.h>

#include <memory>
#include <utility>

namespace opencattus::presenter {

struct NetworkCreatorData {
    Network::Profile profile;
    Network::Type type;
    std::string interface;
    std::string address;
    std::string subnetMask;
    std::string gateway;
    std::string name;
    std::vector<boost::asio::ip::address> domains;
};

class NetworkCreator {
private:
    std::vector<NetworkCreatorData> m_networks;

    bool checkIfProfileExists(Network::Profile profile);

public:
    bool addNetworkInformation(NetworkCreatorData&& data);

    [[nodiscard]] bool canUseInterface(
        std::string_view interface, Network::Profile requestedProfile) const;
    [[nodiscard]] const NetworkCreatorData* findByInterface(
        std::string_view interface) const;
    [[nodiscard]] const NetworkCreatorData* findByProfile(
        Network::Profile profile) const;

    void saveNetworksToModel(Cluster& model);

    std::size_t getSelectedInterfaces();
};

class PresenterNetwork : public Presenter {
private:
    std::unique_ptr<Network> m_network;

    struct Messages {
        static constexpr const auto title = "Network Settings";
        static constexpr const auto errorInsufficient
            = "Not enough interfaces!\nYou need at least two separate cards: "
              "one internal and one external";

        struct Interface {
            static std::string formatQuestion(
                Network::Type type, Network::Profile profile)
            {
                return fmt::format("Select your {} ({}) network interface",
                    opencattus::utils::enums::toString(profile),
                    opencattus::utils::enums::toString(type));
            }

            static constexpr const auto help
                = Presenter::Messages::Placeholder::help;
        };

        struct Details {
            static constexpr const auto question
                = "Fill the required network details";
            static constexpr const auto help
                = Presenter::Messages::Placeholder::help;
        };

        struct Error {
            static constexpr const auto sharedIP
                = "The service network cannot reuse the management IP address";
            static constexpr const auto sharedSubnet
                = "The service network must use a separate subnet from the "
                  "management network";
            static constexpr const auto sharedDetailsInvalid
                = "The shared service network details are invalid";
            static constexpr const auto gatewayDetailsInvalid
                = "IP address, subnet mask, and gateway must be valid IPv4 "
                  "addresses";
            static constexpr const auto gatewayOutsideSubnet
                = "Gateway must be inside the selected subnet";
        };

        struct IP {
            static constexpr const auto address = "IP Address";
            static constexpr const auto subnetMask = "Subnet Mask";
            static constexpr const auto gateway = "Gateway";
        };

        struct Domain {
            static constexpr const auto name = "Domain name";
            static constexpr const auto servers = "Nameservers";
        };

#ifndef NDEBUG
        struct Debug {
            static constexpr const auto attributes
                = "The following network attributes were detected";
        };
#endif
    };

    template <typename T>
    std::string networkInterfaceSelection(const T& interfaces)
    {
        return m_view->listMenu(Messages::title,
            Messages::Interface::formatQuestion(
                m_network->getType(), m_network->getProfile())
                .c_str(),
            interfaces, Messages::Interface::help);
    }

    // Tested with T = std::array<std::pair<std::string, std::string>, N>
    template <typename T> T networkAddress(const T& fields)
    {
        return m_view->fieldMenu(Messages::title, Messages::Details::question,
            fields, Messages::Details::help);
    }

#ifndef NDEBUG
    // TODO: Better implementation
    // Tested with T = std::array<std::pair<std::string, std::string>, N>
    template <typename T> void networkConfirmation(const T& pairs)
    {
        m_view->okCancelMessage(Messages::Debug::attributes, pairs);
    }
#endif

    std::vector<std::string> retrievePossibleInterfaces(NetworkCreator& nc);

    void createNetwork(const std::vector<std::string>& interfaceList,
        NetworkCreator& nc, NetworkCreatorData& ncd);

public:
    PresenterNetwork(std::unique_ptr<Cluster>& model,
        std::unique_ptr<View>& view, NetworkCreator& nc,
        Network::Profile profile = Network::Profile::External,
        Network::Type type = Network::Type::Ethernet);
};

};

#endif // OPENCATTUS_PRESENTERNETWORK_H_
