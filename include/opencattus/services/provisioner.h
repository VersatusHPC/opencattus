/*
 * Copyright 2021 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OPENCATTUS_PROVISIONER_H_
#define OPENCATTUS_PROVISIONER_H_

template <typename Derived> class Provisioner {
    constexpr auto derived() -> Derived& { return static_cast<Derived&>(this); }

public:
    void install() { derived().install(); }
};

#endif // OPENCATTUS_PROVISIONER_H_
