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
        case OS::Platform::ubuntu2404:
            return {
                Cluster::Provisioner::xCAT,
                Cluster::Provisioner::Confluent,
            };
        case OS::Platform::el8:
        case OS::Platform::el9:
        case OS::Platform::el10:
            return {
                Cluster::Provisioner::xCAT,
                Cluster::Provisioner::Confluent,
            };
        default:
            __builtin_unreachable();
    }
}

auto supportedProvisionersFor(const OS& headnodeOS, const OS& computeNodeOS)
    -> std::vector<Cluster::Provisioner>
{
    const auto headnodeSupported = supportedProvisionersFor(headnodeOS);
    auto supported = supportedProvisionersFor(computeNodeOS);
    std::erase_if(supported, [&](const auto provisioner) {
        return std::ranges::find(headnodeSupported, provisioner)
            == headnodeSupported.end();
    });

    // xCAT on Ubuntu 24.04 headnodes is only implemented for Ubuntu 24.04
    // compute images; mirror validateProvisionerSupport() so the menu never
    // offers a combination the model rejects.
    if (headnodeOS.getPlatform() == OS::Platform::ubuntu2404
        && computeNodeOS.getPlatform() != OS::Platform::ubuntu2404) {
        std::erase(supported, Cluster::Provisioner::xCAT);
    }

    return supported;
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
    const auto supported = supportedProvisionersFor(
        m_model->getHeadnode().getOS(), m_model->getComputeNodeOS());
    if (supported.size() == 1) {
        m_model->setProvisioner(supported.front());
        m_view->message(Messages::title, Messages::singleProvisioner);
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

TEST_CASE("supportedProvisionersFor keeps EL10 xcat and confluent available")
{
    const auto supported = supportedProvisionersFor(
        OS(OS::Distro::Rocky, OS::Platform::el10, 1));

    CHECK(supported
        == std::vector<Cluster::Provisioner> {
            Cluster::Provisioner::xCAT,
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

TEST_CASE("supportedProvisionersFor keeps Ubuntu 24.04 xcat and confluent "
          "available")
{
    const auto supported = supportedProvisionersFor(
        OS(OS::Distro::Ubuntu, OS::Platform::ubuntu2404, 0));

    CHECK(supported
        == std::vector<Cluster::Provisioner> {
            Cluster::Provisioner::xCAT,
            Cluster::Provisioner::Confluent,
        });
}

TEST_CASE("supportedProvisionersFor checks headnode and compute node releases")
{
    const auto supported
        = supportedProvisionersFor(OS(OS::Distro::RHEL, OS::Platform::el10, 1),
            OS(OS::Distro::Rocky, OS::Platform::el9, 6));

    CHECK(supported
        == std::vector<Cluster::Provisioner> {
            Cluster::Provisioner::xCAT,
            Cluster::Provisioner::Confluent,
        });
}

TEST_CASE("supportedProvisionersFor keeps Ubuntu 24.04 headnodes with "
          "Enterprise Linux compute images on confluent")
{
    const auto supported = supportedProvisionersFor(
        OS(OS::Distro::Ubuntu, OS::Platform::ubuntu2404, 0),
        OS(OS::Distro::Rocky, OS::Platform::el10, 1));

    CHECK(supported
        == std::vector<Cluster::Provisioner> {
            Cluster::Provisioner::Confluent,
        });
}

TEST_CASE("supportedProvisionersFor keeps Ubuntu 24.04 headnode and compute "
          "pairs on xcat and confluent")
{
    const auto supported = supportedProvisionersFor(
        OS(OS::Distro::Ubuntu, OS::Platform::ubuntu2404, 0),
        OS(OS::Distro::Ubuntu, OS::Platform::ubuntu2404, 0));

    CHECK(supported
        == std::vector<Cluster::Provisioner> {
            Cluster::Provisioner::xCAT,
            Cluster::Provisioner::Confluent,
        });
}
