/*
 * Copyright 2026 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <opencattus/presenter/PresenterProvisioner.h>

#include <algorithm>
#include <utility>

#ifdef BUILD_TESTING
#include <doctest/doctest.h>
#else
#define DOCTEST_CONFIG_DISABLE
#include <doctest/doctest.h>
#endif

namespace {

using Cluster = opencattus::models::Cluster;
using OS = opencattus::models::OS;

auto supportedProvisionersFor(const OS& os) -> std::vector<Cluster::Provisioner>
{
    switch (os.getPlatform()) {
        case OS::Platform::el10:
            return { Cluster::Provisioner::Confluent };
        case OS::Platform::el8:
        case OS::Platform::el9:
            return {
                Cluster::Provisioner::xCAT,
                Cluster::Provisioner::Confluent,
            };
        default:
            __builtin_unreachable();
    }
}

auto toProvisionerName(Cluster::Provisioner provisioner) -> std::string
{
    switch (provisioner) {
        case Cluster::Provisioner::xCAT:
            return "xcat";
        case Cluster::Provisioner::Confluent:
            return "confluent";
        default:
            __builtin_unreachable();
    }
}

} // namespace

namespace opencattus::presenter {

PresenterProvisioner::PresenterProvisioner(
    std::unique_ptr<Cluster>& model, std::unique_ptr<View>& view)
    : Presenter(model, view)
{
    const auto supported
        = supportedProvisionersFor(m_model->getHeadnode().getOS());
    if (supported.size() == 1) {
        m_model->setProvisioner(supported.front());
        m_view->message(Messages::title, Messages::confluentOnly);
        return;
    }

    std::vector<std::string> choices;
    choices.reserve(supported.size());
    std::ranges::transform(supported, std::back_inserter(choices),
        [](const auto provisioner) { return toProvisionerName(provisioner); });

    const auto selectedName = m_view->listMenu(
        Messages::title, Messages::question, choices, Messages::help);
    const auto selectedIt
        = std::find(choices.begin(), choices.end(), selectedName);
    LOG_ASSERT(selectedIt != choices.end(),
        "selected provisioner is not present on the choices list");
    m_model->setProvisioner(supported[static_cast<std::size_t>(
        std::distance(choices.begin(), selectedIt))]);
}

} // namespace opencattus::presenter

TEST_CASE("supportedProvisionersFor keeps EL10 on confluent")
{
    const auto supported = supportedProvisionersFor(
        OS(OS::Distro::Rocky, OS::Platform::el10, 1));

    CHECK(supported
        == std::vector<Cluster::Provisioner> {
            Cluster::Provisioner::Confluent,
        });
}

TEST_CASE("supportedProvisionersFor keeps EL9 xcat and confluent available")
{
    const auto supported
        = supportedProvisionersFor(OS(OS::Distro::Rocky, OS::Platform::el9, 6));

    CHECK(supported
        == std::vector<Cluster::Provisioner> {
            Cluster::Provisioner::xCAT,
            Cluster::Provisioner::Confluent,
        });
}
