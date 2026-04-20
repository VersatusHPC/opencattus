#include <opencattus/services/ansible/roles/aide.h>
#include <opencattus/services/log.h>

#ifdef BUILD_TESTING
#include <doctest/doctest.h>
#else
#define DOCTEST_CONFIG_DISABLE
#include <doctest/doctest.h>
#endif

// @FIXME: Not creating cron script (daily)
//  /opt/versatushpc/scripts/aide-check.sh

#include <fmt/core.h>

namespace opencattus::services::ansible::roles::aide {

ScriptBuilder installScript(
    const Role& role, const opencattus::models::OS& osinfo)
{
    using namespace opencattus;

    ScriptBuilder builder(osinfo);

    LOG_ASSERT(role.roleName() == "aide",
        fmt::format("Expected aide role, found {}", role.roleName()));

    builder.addNewLine()
        .addCommand("# Install AIDE package")
        .addPackage("aide")
        .addNewLine()
        .addCommand("# Skip if aide database exists")
        .addCommand(
            "test -f /var/lib/aide/aide.db.gz -o -f /var/lib/aide/aide.db && "
            "exit 0")
        .addCommand("# Initialize AIDE database")
        .addCommand("aide --init || aideinit")
        .addCommand(R"(if test -f /var/lib/aide/aide.db.new.gz; then
    mv /var/lib/aide/aide.db.new.gz /var/lib/aide/aide.db.gz
elif test -f /var/lib/aide/aide.db.new; then
    mv /var/lib/aide/aide.db.new /var/lib/aide/aide.db
fi)");

    return builder;
}

} // namespace opencattus::services::ansible::roles::aide
