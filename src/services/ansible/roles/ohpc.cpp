#include <opencattus/services/ansible/roles/ohpc.h>
#include <opencattus/services/log.h>
#include <opencattus/utils/singleton.h>

#ifdef BUILD_TESTING
#include <doctest/doctest.h>
#else
#define DOCTEST_CONFIG_DISABLE
#include <doctest/doctest.h>
#endif

#include <fmt/core.h>
#include <set>

namespace opencattus::services::ansible::roles::ohpc {

namespace {

std::set<std::string> defaultPackagesFor(const models::OS& os)
{
    if (os.getMajorVersion() >= 10) {
        return { "gnu15-compilers-ohpc", "openmpi5-pmix-gnu15-ohpc",
            "mpich-ofi-gnu15-ohpc", "mpich-ucx-gnu15-ohpc",
            "mvapich2-gnu15-ohpc", "lmod-ohpc",
            "lmod-defaults-gnu15-openmpi5-ohpc", "ohpc-autotools",
            "hwloc-ohpc", "spack-ohpc", "valgrind-ohpc" };
    }

    return { "openmpi4-gnu12-ohpc", "mpich-ofi-gnu12-ohpc",
        "mpich-ucx-gnu12-ohpc", "mvapich2-gnu12-ohpc",
        "lmod-defaults-gnu12-openmpi4-ohpc", "ohpc-autotools",
        "hwloc-ohpc", "spack-ohpc", "valgrind-ohpc" };
}

std::set<std::string> resolvePackages(
    const models::OS& os, const std::set<std::string>& overridePackages)
{
    if (!overridePackages.empty()) {
        return overridePackages;
    }

    return defaultPackagesFor(os);
}

}

void run(const Role& role)
{
    LOG_INFO("Installing OpenHPC tools, development libraries, compilers and "
             "MPI stacks");

    auto ohpcPackages
        = resolvePackages(utils::singleton::os(),
            utils::singleton::options()->ohpcPackages);
    utils::singleton::osservice()->install(
        fmt::format("{}", fmt::join(ohpcPackages, " ")));
}

TEST_CASE("resolvePackages keeps EL9 defaults unchanged")
{
    const auto packages = resolvePackages(
        models::OS(models::OS::Distro::Rocky, models::OS::Platform::el9, 7),
        {});

    CHECK(packages.contains("openmpi4-gnu12-ohpc"));
    CHECK(packages.contains("mpich-ofi-gnu12-ohpc"));
    CHECK(packages.contains("mpich-ucx-gnu12-ohpc"));
    CHECK(packages.contains("mvapich2-gnu12-ohpc"));
    CHECK(packages.contains("lmod-defaults-gnu12-openmpi4-ohpc"));
    CHECK(packages.contains("ohpc-autotools"));
    CHECK(packages.contains("hwloc-ohpc"));
    CHECK_FALSE(packages.contains("gnu15-compilers-ohpc"));
    CHECK_FALSE(packages.contains("openmpi5-gnu15-ohpc"));
}

TEST_CASE("resolvePackages switches EL10 defaults to OpenHPC 4 toolchains")
{
    const auto packages = resolvePackages(
        models::OS(models::OS::Distro::Rocky, models::OS::Platform::el10, 1),
        {});

    CHECK(packages.contains("gnu15-compilers-ohpc"));
    CHECK(packages.contains("openmpi5-pmix-gnu15-ohpc"));
    CHECK(packages.contains("mpich-ofi-gnu15-ohpc"));
    CHECK(packages.contains("mpich-ucx-gnu15-ohpc"));
    CHECK(packages.contains("mvapich2-gnu15-ohpc"));
    CHECK(packages.contains("lmod-ohpc"));
    CHECK(packages.contains("lmod-defaults-gnu15-openmpi5-ohpc"));
    CHECK(packages.contains("ohpc-autotools"));
    CHECK(packages.contains("hwloc-ohpc"));
    CHECK(packages.contains("spack-ohpc"));
    CHECK(packages.contains("valgrind-ohpc"));
    CHECK_FALSE(packages.contains("lmod-defaults-gnu12-openmpi4-ohpc"));
    CHECK_FALSE(packages.contains("openmpi4-gnu12-ohpc"));
    CHECK_FALSE(packages.contains("openmpi5-gnu15-ohpc"));
}

TEST_CASE("resolvePackages preserves explicit package overrides")
{
    const std::set<std::string> explicitPackages { "custom-ohpc-package" };
    const auto packages = resolvePackages(
        models::OS(models::OS::Distro::Rocky, models::OS::Platform::el10, 1),
        explicitPackages);

    CHECK(packages == explicitPackages);
}

}
