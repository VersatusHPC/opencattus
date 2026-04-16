/*
 * Copyright 2022 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OPENCATTUS_PRESENTERINSTALL_H_
#define OPENCATTUS_PRESENTERINSTALL_H_

#include <opencattus/presenter/Presenter.h>

#include <boost/lexical_cast.hpp>

namespace opencattus::presenter {
class PresenterInstall : public Presenter {
public:
    PresenterInstall(
        std::unique_ptr<Cluster>& model, std::unique_ptr<View>& view);
};
}

#endif // OPENCATTUS_PRESENTERINSTALL_H_
