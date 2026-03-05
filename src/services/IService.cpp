#include <opencattus/opencattus.h>
#include <opencattus/functions.h>
#include <opencattus/services/IService.h>
#include <opencattus/services/log.h>
#include <opencattus/services/options.h>
#include <opencattus/utils/singleton.h>

/* BUG: Refactor:
 * Legacy casting.
 * Dry run is a band-aid solution.
 * Variables could be const.
 * Variables name are not the best ones.
 * Check grammar.
 * Warnings during compilation.
 */

namespace opencattus::services {

using EnableRType
    = std::vector<sdbus::Struct<std::string, std::string, std::string>>;

bool IService::handleException(const sdbus::Error& e, const std::string_view fn)
{
    if (e.getName() == "org.freedesktop.systemd1.NoSuchUnit") {
        throw daemon_exception(fmt::format(
            "Daemon {} not found (NoSuchUnit while calling {})", m_name, fn));
    }

    return false;
}

void IService::enable()
{
    const auto opts = opencattus::utils::singleton::options();
    if (opts->dryRun) {
        LOG_INFO("Dry Run: Would have enabled the service {}", m_name)
        return;
    }

    LOG_TRACE("service: enabling {}", m_name);

    auto ret = callObjectFunctionArray("EnableUnitFiles", false, true);
    if (!ret.has_value()) {
        LOG_ERROR(
            "callObjectFunctionArray returned none for service {}", m_name);
        return;
    }
    const auto& [_install, retvec] = (*ret).getPair<bool, EnableRType>();

    if (retvec.empty()) {
        LOG_WARN("service {} already enabled", m_name);
    }
}

void IService::disable()
{
    const auto opts = opencattus::utils::singleton::options();
    if (opts->dryRun) {
        LOG_INFO("Dry Run: Would have disabled the service {}", m_name)
        return;
    }

    LOG_TRACE("service: disabling {}", m_name);

    auto ret = callObjectFunctionArray("DisableUnitFiles", false, true);
    if (!ret.has_value()) {
        LOG_ERROR("callObjectFunctionArray returned none, service {}", m_name);
        return;
    };

    if ((*ret).get<EnableRType>().empty()) {
        LOG_WARN("service {} already disabled", m_name);
    }
}

void IService::start()
{
    const auto opts = opencattus::utils::singleton::options();
    if (opts->dryRun) {
        LOG_INFO("Dry Run: Would have started the service {}", m_name)
        return;
    }

    LOG_TRACE("service: starting {}", m_name);
    callObjectFunction("StartUnit", "replace");
}

void IService::restart()
{
    const auto opts = opencattus::utils::singleton::options();
    if (opts->dryRun) {
        LOG_INFO("Dry Run: Would have restarted the service {}", m_name)
        return;
    }

    LOG_TRACE("service: restarting {}", m_name);
    callObjectFunction("RestartUnit", "replace");
}

void IService::stop()
{
    const auto opts = opencattus::utils::singleton::options();
    if (opts->dryRun) {
        LOG_INFO("Dry Run: Would have stopped the service {}", m_name)
        return;
    }

    LOG_TRACE("service: stopping {}", m_name);
    callObjectFunction("StopUnit", "replace");
}

}; // namespace opencattus::services
