/*
 * Copyright 2026 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OPENCATTUS_TESTING_TEST_MESSAGE_BUS_H_
#define OPENCATTUS_TESTING_TEST_MESSAGE_BUS_H_

#include <any>
#include <memory>
#include <stdexcept>
#include <string>

#include <opencattus/functions.h>
#include <opencattus/messagebus.h>
#include <opencattus/patterns/singleton.h>
#include <opencattus/services/options.h>
#include <opencattus/services/runner.h>

namespace opencattus::testing {

// No-op MessageBusMethod that backs TestMessageBus. The production code path
// for tests that touch initializeSingletonsOptions never actually issues a
// call(), so a stub that throws on use is enough to surface accidental real
// invocations while letting the singleton bootstrap succeed.
class TestMessageBusMethod final : public services::MessageBusMethod {
protected:
    void pushSingleParam(services::MethodParamVariant /*param*/) override { }

    services::MessageReply callMethod() override
    {
        throw std::runtime_error(
            "TestMessageBus::callMethod invoked; tests must not exercise the "
            "real bus");
    }
};

// In-process MessageBus stand-in. Lets tests run the same
// initializeSingletonsOptions wiring as production without requiring a live
// system bus (containers that do not start dbus-daemon would otherwise throw
// [org.freedesktop.DBus.Error.FileNotFound] Failed to open bus).
class TestMessageBus final : public services::MessageBus {
public:
    [[nodiscard]] std::unique_ptr<services::MessageBusMethod> method(
        std::string /*interface*/, std::string /*method*/) override
    {
        return std::make_unique<TestMessageBusMethod>();
    }
};

// Test substitute for opencattus::services::initializeSingletonsOptions.
// Wires up the same three singletons that production code expects (Options,
// MessageBus, IRunner) but uses TestMessageBus instead of DBusClient so the
// tests do not require a running system bus. Use this from any TEST_CASE that
// would otherwise call initializeSingletonsOptions.
inline void initializeSingletonsForTest(
    std::unique_ptr<const services::Options>&& opts
    = std::make_unique<const services::Options>())
{
    Singleton<const services::Options>::init(std::move(opts));
    Singleton<services::MessageBus>::init([]() {
        return functions::makeUniqueDerived<services::MessageBus,
            TestMessageBus>();
    });
    Singleton<services::IRunner>::init([]() {
        return functions::makeUniqueDerived<services::IRunner,
            services::DryRunner>();
    });
}

} // namespace opencattus::testing

#endif
