/*
 * Copyright 2026 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <algorithm>
#include <opencattus/presenter/PresenterOpenHPC.h>
#include <ranges>

namespace {

using BundleSelection = std::tuple<std::string, std::string, bool>;

auto selectableBundlesFor(const opencattus::models::OS& os)
    -> std::vector<BundleSelection>
{
    auto bundles = std::vector<BundleSelection> {
        { "serial-libs", "Scientific serial libraries", true },
        { "parallel-libs", "Scientific parallel libraries", true },
    };

    if (os.getPlatform() == opencattus::models::OS::Platform::el9
        || os.getPlatform() == opencattus::models::OS::Platform::el10) {
        bundles.emplace_back(
            "intel-oneapi", "Intel oneAPI toolchain and Intel MPI", false);
    }

    return bundles;
}

void applySavedSelection(std::vector<BundleSelection>& bundles,
    const std::optional<std::vector<std::string>>& enabledBundles)
{
    if (!enabledBundles.has_value()) {
        return;
    }

    for (auto& [id, label, enabled] : bundles) {
        static_cast<void>(label);
        enabled = std::ranges::find(enabledBundles.value(), id)
            != enabledBundles->end();
    }
}

} // namespace

namespace opencattus::presenter {

PresenterOpenHPC::PresenterOpenHPC(
    std::unique_ptr<Cluster>& model, std::unique_ptr<View>& view)
    : Presenter(model, view)
{
    auto bundles = selectableBundlesFor(m_model->getComputeNodeOS());
    applySavedSelection(bundles, m_model->getEnabledOpenHPCBundles());

    const auto& [ret, enabledBundles]
        = m_view->checkboxSelectionMenu(Messages::title,
            Messages::General::question, Messages::General::help, bundles);

    if (ret != 1) {
        m_view->abort();
    }

    m_model->setEnabledOpenHPCBundles(enabledBundles);
}

}
