/*
 * Copyright 2022 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <opencattus/presenter/PresenterInfiniband.h>

#include <algorithm>

#ifdef BUILD_TESTING
#include <doctest/doctest.h>
#else
#define DOCTEST_CONFIG_DISABLE
#include <doctest/doctest.h>
#endif

namespace {

auto usableInfinibandInterfaces(const std::vector<std::string>& interfaces)
    -> std::vector<std::string>
{
    std::vector<std::string> usable;

    for (const auto& interface : interfaces) {
        if (!interface.starts_with("ib")) {
            continue;
        }

        try {
            static_cast<void>(Connection::fetchAddress(interface));
            static_cast<void>(Network::fetchSubnetMask(interface));
            usable.push_back(interface);
        } catch (const std::exception&) {
        }
    }

    return usable;
}

auto displayKindName(OFED::Kind kind) -> std::string
{
    switch (kind) {
        case OFED::Kind::Doca:
            return opencattus::utils::enums::toStringUpper(kind);
        default:
            return opencattus::utils::enums::toString(kind);
    }
}

auto ofedKindChoices() -> std::vector<std::string>
{
    std::vector<std::string> choices;
    choices.reserve(opencattus::utils::enums::count<OFED::Kind>());
    for (const auto kind :
        { OFED::Kind::Inbox, OFED::Kind::Doca, OFED::Kind::Oracle }) {
        choices.emplace_back(displayKindName(kind));
    }

    return choices;
}

auto parseKindName(std::string_view rawKind) -> OFED::Kind
{
    return opencattus::utils::enums::ofStringExc<OFED::Kind>(
        rawKind, opencattus::utils::enums::Case::Insensitive);
}

} // namespace

namespace opencattus::presenter {

PresenterInfiniband::PresenterInfiniband(std::unique_ptr<Cluster>& model,
    std::unique_ptr<View>& view, NetworkCreator& nc)
    : Presenter(model, view)
{

    const auto interfaces
        = usableInfinibandInterfaces(Connection::fetchInterfaces());
    if (interfaces.empty()) {
        LOG_WARN("No usable Infiniband interfaces found.")
        return;
    }

    // TODO: Infiniband class? Detect if IB is available (fetch ib0)
    if (m_view->yesNoQuestion(
            Messages::title, Messages::question, Messages::help)) {

        const auto selectedKind = parseKindName(
            m_view->listMenu(Messages::title, Messages::OFED::question,
                ofedKindChoices(), Messages::OFED::help));

        std::string selectedVersion = "latest";
        if (selectedKind != OFED::Kind::Inbox) {
            auto version = std::to_array<std::pair<std::string, std::string>>(
                { { Messages::OFED::Version::field, selectedVersion } });
            version = m_view->fieldMenu(Messages::title,
                Messages::OFED::Version::question, version,
                Messages::OFED::Version::help);
            selectedVersion = version[0].second;
        }

        m_model->setOFED(selectedKind, selectedVersion);
        LOG_DEBUG("Set OFED stack as: {}",
            opencattus::utils::enums::toString<OFED::Kind>(
                m_model->getOFED()->getKind()));

        try {
            Call<PresenterNetwork>(
                nc, Network::Profile::Application, Network::Type::Infiniband);
        } catch (const ViewBackRequested&) {
            throw;
        } catch (const ViewAbortRequested&) {
            throw;
        } catch (const std::exception& ex) {
            LOG_ERROR("Failed to add {} network: {}",
                opencattus::utils::enums::toString(
                    Network::Profile::Application),
                ex.what());
            m_view->message(Messages::title, ex.what());
        }
    }
}

}

TEST_CASE("ofedKindChoices keeps DOCA uppercase for the TUI")
{
    CHECK(ofedKindChoices()
        == std::vector<std::string> { "Inbox", "DOCA", "Oracle" });
    CHECK(parseKindName("DOCA") == OFED::Kind::Doca);
    CHECK(parseKindName("Doca") == OFED::Kind::Doca);
}
