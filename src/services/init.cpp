#include <opencattus/functions.h>
#include <opencattus/models/cluster.h>
#include <opencattus/patterns/singleton.h>
#include <opencattus/services/init.h>
#include <opencattus/services/osservice.h>
#include <opencattus/utils/singleton.h>

#include <opencattus/dbus_client.h>
#include <opencattus/messagebus.h>

namespace opencattus::services {

using namespace opencattus;
using namespace opencattus::services;

// WARNING: If you change the type T in Singleton<T>::init(...) (to const T for
// instance) all the Singleton<T>::get need to be changed, otherwise you get
// "Singleton read before initialization error" at runtime. While there are
// getters to handle this in opencattus/utils/singletons in a uniform way and for
// most cases, these getters depends on headers (that introduce the type
// T in question), so files including the same header cannot use these getters
// (or we have recursive header inclusion error).

// Singletons that depends only in the options, the cluster model
// depends on these
void initializeSingletonsOptions(std::unique_ptr<const Options>&& opts)
{
    Singleton<const Options>::init(std::move(opts));
    opencattus::Singleton<MessageBus>::init([]() {
        return opencattus::functions::makeUniqueDerived<MessageBus, DBusClient>(
            "org.freedesktop.systemd1", "/org/freedesktop/systemd1");
    });
    opencattus::Singleton<opencattus::services::IRunner>::init([&]() {
        using opencattus::services::IRunner;
        using opencattus::services::DryRunner;
        using opencattus::services::Runner;
        auto opts = Singleton<const Options>::get();

        if (opts->dryRun) {
            return opencattus::functions::makeUniqueDerived<IRunner, DryRunner>();
        }

        return opencattus::functions::makeUniqueDerived<IRunner, Runner>();
    });
}

// Singletons that depends on the cluster model
void initializeSingletonsModel(
    std::unique_ptr<opencattus::models::Cluster>&& cluster,
    std::unique_ptr<const opencattus::models::AnswerFile>&& answerfile)
{
    using opencattus::models::Cluster;
    opencattus::Singleton<const models::AnswerFile>::init(std::move(answerfile));
    opencattus::Singleton<Cluster>::init(std::move(cluster));

    using opencattus::services::repos::RepoManager;
    opencattus::Singleton<RepoManager>::init([]() {
        auto repoManager = std::make_unique<RepoManager>();
        return repoManager;
    });

    opencattus::Singleton<const opencattus::services::IOSService>::init([]() {
        const auto& osinfo
            = opencattus::Singleton<Cluster>::get()->getHeadnode().getOS();
        return opencattus::services::IOSService::factory(osinfo);
    });
}

}; // namespace opencattus::services
