/*
 * Copyright 2022 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OPENCATTUS_PRESENTERINSTALL_H_
#define OPENCATTUS_PRESENTERINSTALL_H_

#include <opencattus/presenter/Presenter.h>

#include <boost/lexical_cast.hpp>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace opencattus::presenter {
class PresenterInstall : public Presenter {
public:
    using StepStateCallback
        = std::function<void(const std::vector<std::string>&)>;

    PresenterInstall(std::unique_ptr<Cluster>& model,
        std::unique_ptr<View>& view, StepStateCallback onStepComplete = {},
        std::set<std::string> completedSteps = {});
};
}

#endif // OPENCATTUS_PRESENTERINSTALL_H_
