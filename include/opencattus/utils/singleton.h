#ifndef OPENCATTUS_UTILS_SINGLETON_H_
#define OPENCATTUS_UTILS_SINGLETON_H_

#include <opencattus/patterns/singleton.h>
// WARNING: The getters depend on types defined in these
//   headers so we cannot use it inside these headers and
//   its transitive dependencies, sorry
#include <opencattus/models/cluster.h>
#include <opencattus/services/osservice.h>
#include <opencattus/services/repos.h>

/**
 * @brief Simple getter for some singletons.
 */
namespace opencattus::utils::singleton {

// Const singletons
constexpr auto os()
{
    return opencattus::Singleton<models::Cluster>::get()->getHeadnode().getOS();
}
constexpr auto osservice()
{
    return opencattus::Singleton<const services::IOSService>::get();
}
constexpr auto options()
{
    return opencattus::Singleton<const services::Options>::get();
}
constexpr auto answerfile()
{
    return opencattus::Singleton<const models::AnswerFile>::get();
}

// Mutable singletons
constexpr auto cluster()
{
    return opencattus::Singleton<models::Cluster>::get();
}
constexpr auto runner()
{
    return opencattus::Singleton<services::IRunner>::get();
}
constexpr auto repos()
{
    return opencattus::Singleton<services::repos::RepoManager>::get();
}

}

#endif
