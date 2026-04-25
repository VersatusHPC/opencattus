#include <opencattus/services/ansible/roles/ohpc.h>
#include <opencattus/services/log.h>
#include <opencattus/services/runner.h>
#include <opencattus/utils/singleton.h>

#ifdef BUILD_TESTING
#include <doctest/doctest.h>
#else
#define DOCTEST_CONFIG_DISABLE
#include <doctest/doctest.h>
#endif

#include <fmt/core.h>
#include <optional>
#include <set>
#include <string_view>
#include <utility>

namespace opencattus::services::ansible::roles::ohpc {

namespace {

    constexpr auto bundleSerialLibraries = std::string_view("serial-libs");
    constexpr auto bundleParallelLibraries = std::string_view("parallel-libs");
    constexpr auto bundleIntelOneAPI = std::string_view("intel-oneapi");

    // Keep Enterprise Linux mappings explicit. The current update baselines we
    // pin here are:
    //   EL8 -> OpenHPC 2 update stream with GNU12/OpenMPI4
    //   EL9 -> OpenHPC 3 update stream with GNU15/OpenMPI5
    //   EL10 -> OpenHPC 4 path with GNU15/OpenMPI5 PMIx
    std::set<std::string> defaultPackagesForEl8()
    {
        return { "gnu12-compilers-ohpc", "openmpi4-gnu12-ohpc",
            "mpich-ofi-gnu12-ohpc", "mpich-ucx-gnu12-ohpc",
            "mvapich2-gnu12-ohpc", "lmod-ohpc",
            "lmod-defaults-gnu12-openmpi4-ohpc", "ohpc-autotools", "hwloc-ohpc",
            "spack-ohpc", "valgrind-ohpc" };
    }

    std::set<std::string> defaultPackagesForEl9()
    {
        return { "gnu15-compilers-ohpc", "openmpi5-pmix-gnu15-ohpc",
            "mpich-ofi-gnu15-ohpc", "mpich-ucx-gnu15-ohpc",
            "mvapich2-gnu15-ohpc", "lmod-ohpc",
            "lmod-defaults-gnu15-openmpi5-ohpc", "ohpc-autotools", "hwloc-ohpc",
            "spack-ohpc", "valgrind-ohpc" };
    }

    std::set<std::string> defaultPackagesForEl10()
    {
        return { "gnu15-compilers-ohpc", "openmpi5-pmix-gnu15-ohpc",
            "mpich-ofi-gnu15-ohpc", "mpich-ucx-gnu15-ohpc",
            "mvapich2-gnu15-ohpc", "lmod-ohpc",
            "lmod-defaults-gnu15-openmpi5-ohpc", "ohpc-autotools", "hwloc-ohpc",
            "spack-ohpc", "valgrind-ohpc" };
    }

    std::set<std::string> defaultPackagesForUbuntu24()
    {
        return { "gnu15-compilers-ohpc", "openmpi5-gnu15-ohpc",
            "mpich-ucx-gnu15-ohpc", "mvapich2-gnu15-ohpc", "lmod-ohpc",
            "lmod-defaults-gnu15-openmpi5-ohpc", "ohpc-autotools", "hwloc-ohpc",
            "spack-ohpc", "valgrind-ohpc" };
    }

    std::set<std::string> bundlePackagesForEl8(std::string_view bundleId)
    {
        if (bundleId == bundleSerialLibraries) {
            return { "ohpc-gnu12-serial-libs" };
        }
        if (bundleId == bundleParallelLibraries) {
            return { "ohpc-gnu12-parallel-libs" };
        }
        if (bundleId == bundleIntelOneAPI) {
            return { };
        }

        return { };
    }

    std::set<std::string> bundlePackagesForEl9(std::string_view bundleId)
    {
        if (bundleId == bundleSerialLibraries) {
            return { "ohpc-gnu15-serial-libs" };
        }
        if (bundleId == bundleParallelLibraries) {
            return { "ohpc-gnu15-parallel-libs" };
        }
        if (bundleId == bundleIntelOneAPI) {
            return { "intel-oneapi-toolkit-release-ohpc",
                "intel-compilers-devel-ohpc", "intel-mpi-devel-ohpc",
                "ohpc-intel-serial-libs", "ohpc-intel-impi-parallel-libs" };
        }

        return { };
    }

    std::set<std::string> bundlePackagesForEl10(std::string_view bundleId)
    {
        if (bundleId == bundleSerialLibraries) {
            return { "ohpc-gnu15-serial-libs" };
        }
        if (bundleId == bundleParallelLibraries) {
            return { "ohpc-gnu15-parallel-libs" };
        }
        if (bundleId == bundleIntelOneAPI) {
            return { "intel-oneapi-toolkit-release-ohpc",
                "intel-compilers-devel-ohpc", "intel-mpi-devel-ohpc",
                "ohpc-intel-serial-libs", "ohpc-intel-impi-parallel-libs" };
        }

        return { };
    }

    std::set<std::string> bundlePackagesForUbuntu24(std::string_view bundleId)
    {
        if (bundleId == bundleSerialLibraries) {
            return { "ohpc-gnu15-serial-libs" };
        }
        if (bundleId == bundleParallelLibraries) {
            return { "ohpc-gnu15-parallel-libs" };
        }
        if (bundleId == bundleIntelOneAPI) {
            return { "intel-oneapi-toolkit-release-ohpc",
                "intel-compilers-devel-ohpc", "intel-mpi-devel-ohpc",
                "ohpc-intel-serial-libs", "ohpc-intel-impi-parallel-libs" };
        }

        return { };
    }

    std::set<std::string> defaultPackagesFor(const models::OS& os)
    {
        switch (os.getPlatform()) {
            case models::OS::Platform::el8:
                return defaultPackagesForEl8();
            case models::OS::Platform::el9:
                return defaultPackagesForEl9();
            case models::OS::Platform::el10:
                return defaultPackagesForEl10();
            case models::OS::Platform::ubuntu2404:
                return defaultPackagesForUbuntu24();
            default:
                std::unreachable();
        }
    }

    auto defaultBundlesFor(const models::OS& os) -> std::vector<std::string>
    {
        switch (os.getPlatform()) {
            case models::OS::Platform::el8:
            case models::OS::Platform::el9:
            case models::OS::Platform::el10:
            case models::OS::Platform::ubuntu2404:
                return { std::string(bundleSerialLibraries),
                    std::string(bundleParallelLibraries) };
            default:
                std::unreachable();
        }
    }

    std::set<std::string> bundlePackagesFor(
        const models::OS& os, std::string_view bundleId)
    {
        switch (os.getPlatform()) {
            case models::OS::Platform::el8:
                return bundlePackagesForEl8(bundleId);
            case models::OS::Platform::el9:
                return bundlePackagesForEl9(bundleId);
            case models::OS::Platform::el10:
                return bundlePackagesForEl10(bundleId);
            case models::OS::Platform::ubuntu2404:
                return bundlePackagesForUbuntu24(bundleId);
            default:
                std::unreachable();
        }
    }

    std::set<std::string> resolvePackages(const models::OS& os,
        const std::optional<std::vector<std::string>>& enabledBundles,
        const std::set<std::string>& overridePackages)
    {
        if (!overridePackages.empty()) {
            return overridePackages;
        }

        auto packages = defaultPackagesFor(os);
        const auto bundles = enabledBundles.has_value() ? enabledBundles.value()
                                                        : defaultBundlesFor(os);

        for (const auto& bundleId : bundles) {
            const auto bundlePackages = bundlePackagesFor(os, bundleId);
            packages.insert(bundlePackages.begin(), bundlePackages.end());
        }

        return packages;
    }

    std::string buildPackageInstallCommand(
        const models::OS& os, const std::set<std::string>& packages)
    {
        const auto packageList = fmt::format("{}", fmt::join(packages, " "));
        switch (os.getPackageType()) {
            case models::OS::PackageType::RPM:
                return packageList;
            case models::OS::PackageType::DEB:
                return fmt::format(
                    "DEBIAN_FRONTEND=noninteractive apt install -y {}",
                    packageList);
        }

        std::unreachable();
    }

}

void run(const Role& role)
{
    LOG_INFO("Installing OpenHPC tools, development libraries, compilers and "
             "MPI stacks");

    auto ohpcPackages = resolvePackages(utils::singleton::os(),
        utils::singleton::cluster()->getEnabledOpenHPCBundles(),
        utils::singleton::options()->ohpcPackages);
    const auto installCommand
        = buildPackageInstallCommand(utils::singleton::os(), ohpcPackages);

    if (utils::singleton::os().getPackageType()
        == models::OS::PackageType::DEB) {
        opencattus::services::runner::shell::cmd(installCommand);
    } else {
        utils::singleton::osservice()->install(installCommand);
    }
}

TEST_CASE("resolvePackages keeps the current EL8 defaults explicit")
{
    const auto packages = resolvePackages(
        models::OS(models::OS::Distro::Rocky, models::OS::Platform::el8, 10),
        std::nullopt, { });

    CHECK(packages.contains("gnu12-compilers-ohpc"));
    CHECK(packages.contains("openmpi4-gnu12-ohpc"));
    CHECK(packages.contains("mpich-ofi-gnu12-ohpc"));
    CHECK(packages.contains("mpich-ucx-gnu12-ohpc"));
    CHECK(packages.contains("mvapich2-gnu12-ohpc"));
    CHECK(packages.contains("lmod-ohpc"));
    CHECK(packages.contains("lmod-defaults-gnu12-openmpi4-ohpc"));
    CHECK(packages.contains("ohpc-autotools"));
    CHECK(packages.contains("hwloc-ohpc"));
    CHECK(packages.contains("ohpc-gnu12-serial-libs"));
    CHECK(packages.contains("ohpc-gnu12-parallel-libs"));
    CHECK(packages.contains("spack-ohpc"));
    CHECK(packages.contains("valgrind-ohpc"));
    CHECK_FALSE(packages.contains("gnu9-compilers-ohpc"));
    CHECK_FALSE(packages.contains("lmod-defaults-gnu9-openmpi4-ohpc"));
    CHECK_FALSE(packages.contains("gnu15-compilers-ohpc"));
}

TEST_CASE("resolvePackages keeps the current EL9 defaults explicit")
{
    const auto packages = resolvePackages(
        models::OS(models::OS::Distro::Rocky, models::OS::Platform::el9, 7),
        std::nullopt, { });

    CHECK(packages.contains("gnu15-compilers-ohpc"));
    CHECK(packages.contains("openmpi5-pmix-gnu15-ohpc"));
    CHECK(packages.contains("mpich-ofi-gnu15-ohpc"));
    CHECK(packages.contains("mpich-ucx-gnu15-ohpc"));
    CHECK(packages.contains("mvapich2-gnu15-ohpc"));
    CHECK(packages.contains("lmod-ohpc"));
    CHECK(packages.contains("lmod-defaults-gnu15-openmpi5-ohpc"));
    CHECK(packages.contains("ohpc-autotools"));
    CHECK(packages.contains("hwloc-ohpc"));
    CHECK(packages.contains("ohpc-gnu15-serial-libs"));
    CHECK(packages.contains("ohpc-gnu15-parallel-libs"));
    CHECK(packages.contains("spack-ohpc"));
    CHECK(packages.contains("valgrind-ohpc"));
    CHECK_FALSE(packages.contains("gnu12-compilers-ohpc"));
    CHECK_FALSE(packages.contains("openmpi4-gnu12-ohpc"));
}

TEST_CASE("resolvePackages switches EL10 defaults to OpenHPC 4 toolchains")
{
    const auto packages = resolvePackages(
        models::OS(models::OS::Distro::Rocky, models::OS::Platform::el10, 1),
        std::nullopt, { });

    CHECK(packages.contains("gnu15-compilers-ohpc"));
    CHECK(packages.contains("openmpi5-pmix-gnu15-ohpc"));
    CHECK(packages.contains("mpich-ofi-gnu15-ohpc"));
    CHECK(packages.contains("mpich-ucx-gnu15-ohpc"));
    CHECK(packages.contains("mvapich2-gnu15-ohpc"));
    CHECK(packages.contains("lmod-ohpc"));
    CHECK(packages.contains("lmod-defaults-gnu15-openmpi5-ohpc"));
    CHECK(packages.contains("ohpc-autotools"));
    CHECK(packages.contains("hwloc-ohpc"));
    CHECK(packages.contains("ohpc-gnu15-serial-libs"));
    CHECK(packages.contains("ohpc-gnu15-parallel-libs"));
    CHECK(packages.contains("spack-ohpc"));
    CHECK(packages.contains("valgrind-ohpc"));
    CHECK_FALSE(packages.contains("lmod-defaults-gnu12-openmpi4-ohpc"));
    CHECK_FALSE(packages.contains("openmpi4-gnu12-ohpc"));
    CHECK_FALSE(packages.contains("openmpi5-gnu15-ohpc"));
}

TEST_CASE("resolvePackages keeps Ubuntu 24.04 OpenHPC fork defaults explicit")
{
    const auto packages
        = resolvePackages(models::OS(models::OS::Distro::Ubuntu,
                              models::OS::Platform::ubuntu2404, 0),
            std::nullopt, { });

    CHECK(packages.contains("gnu15-compilers-ohpc"));
    CHECK(packages.contains("openmpi5-gnu15-ohpc"));
    CHECK(packages.contains("mpich-ucx-gnu15-ohpc"));
    CHECK(packages.contains("mvapich2-gnu15-ohpc"));
    CHECK(packages.contains("lmod-ohpc"));
    CHECK(packages.contains("lmod-defaults-gnu15-openmpi5-ohpc"));
    CHECK(packages.contains("ohpc-autotools"));
    CHECK(packages.contains("hwloc-ohpc"));
    CHECK(packages.contains("ohpc-gnu15-serial-libs"));
    CHECK(packages.contains("ohpc-gnu15-parallel-libs"));
    CHECK(packages.contains("spack-ohpc"));
    CHECK(packages.contains("valgrind-ohpc"));
    CHECK_FALSE(packages.contains("openmpi5-pmix-gnu15-ohpc"));
    CHECK_FALSE(packages.contains("mpich-ofi-gnu15-ohpc"));
}

TEST_CASE("resolvePackages preserves explicit package overrides")
{
    const std::set<std::string> explicitPackages { "custom-ohpc-package" };
    const auto packages = resolvePackages(
        models::OS(models::OS::Distro::Rocky, models::OS::Platform::el10, 1),
        std::nullopt, explicitPackages);

    CHECK(packages == explicitPackages);
}

TEST_CASE("buildPackageInstallCommand uses apt on Ubuntu")
{
    const auto command
        = buildPackageInstallCommand(models::OS(models::OS::Distro::Ubuntu,
                                         models::OS::Platform::ubuntu2404, 0),
            { "gnu15-compilers-ohpc", "openmpi5-gnu15-ohpc" });

    CHECK(command
        == "DEBIAN_FRONTEND=noninteractive apt install -y "
           "gnu15-compilers-ohpc openmpi5-gnu15-ohpc");
}

TEST_CASE("resolvePackages adds Intel oneAPI and Intel MPI when requested")
{
    const auto packages = resolvePackages(
        models::OS(models::OS::Distro::Rocky, models::OS::Platform::el10, 1),
        std::vector<std::string> { std::string(bundleSerialLibraries),
            std::string(bundleParallelLibraries),
            std::string(bundleIntelOneAPI) },
        { });

    CHECK(packages.contains("ohpc-gnu15-serial-libs"));
    CHECK(packages.contains("ohpc-gnu15-parallel-libs"));
    CHECK(packages.contains("intel-oneapi-toolkit-release-ohpc"));
    CHECK(packages.contains("intel-compilers-devel-ohpc"));
    CHECK(packages.contains("intel-mpi-devel-ohpc"));
    CHECK(packages.contains("ohpc-intel-serial-libs"));
    CHECK(packages.contains("ohpc-intel-impi-parallel-libs"));
}

}
