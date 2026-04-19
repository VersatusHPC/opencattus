#include <opencattus/functions.h>
#include <opencattus/services/osservice.h>
#include <opencattus/utils/string.h>
#include <stdexcept>

namespace opencattus::services {
using opencattus::Singleton;
using opencattus::functions::makeUniqueDerived;
using opencattus::services::IOSService;

class ELOSService final : public IOSService {
    OS m_osinfo;

public:
    explicit ELOSService(const OS& osinfo)
        : m_osinfo(osinfo) { };

    [[nodiscard]] std::string getKernelInstalled() const override
    {
        auto runner = Singleton<IRunner>::get();
        // Get the kernel version from the kernel package, order by BUILDTIME
        // since there may be multiple kernels installed (previous kernels)
        return runner->checkOutput(
            "bash -c \"rpm -q kernel --qf '%{VERSION}-%{RELEASE}.%{ARCH} "
            "%{BUILDTIME}\n' | sort -nrk 2 | head -1 | awk '{print $1}'\"")[0];
    }

    [[nodiscard]] std::string getKernelRunning() const override
    {
        return Singleton<IRunner>::get()->checkOutput("uname -r")[0];
    }

    [[nodiscard]] std::string getLocale() const override
    {
        // localectl status outputs a line like this System Locale:
        // LANG=en_US.utf8 The awk gets the en_US.utf8 part
        return Singleton<IRunner>::get()->checkOutput(
            R"(bash -c "localectl status | awk -F'=' '/System Locale: / {print $2}'")")
            [0];
    }

    [[nodiscard]] std::vector<std::string> getAvailableLocales() const override
    {
        auto runner = opencattus::Singleton<IRunner>::get();
        return runner->checkOutput("locale -a");
    }

    [[nodiscard]] bool install(std::string_view package) const override
    {
        return (opencattus::Singleton<IRunner>::get()->executeCommand(
                    fmt::format("dnf -y install {}", package))
            != 0);
    }

    [[nodiscard]] bool reinstall(std::string_view package) const override
    {
        return (opencattus::Singleton<IRunner>::get()->executeCommand(
                    fmt::format("dnf -y reinstall {}", package))
            != 0);
    }

    [[nodiscard]] bool groupInstall(std::string_view package) const override
    {
        return (opencattus::Singleton<IRunner>::get()->executeCommand(
                    fmt::format("dnf -y groupinstall \"{}\"", package))
            != 0);
    }

    [[nodiscard]] bool remove(std::string_view package) const override
    {
        return (opencattus::Singleton<IRunner>::get()->executeCommand(
                    fmt::format("dnf -y remove {}", package))
            != 0);
    }

    [[nodiscard]] bool update(std::string_view package) const override
    {
        return (opencattus::Singleton<IRunner>::get()->executeCommand(
                    fmt::format("dnf -y update {}", package))
            != 0);
    }

    [[nodiscard]] bool update() const override
    {
        return (opencattus::Singleton<IRunner>::get()->executeCommand(
                    "dnf -y update")
            != 0);
    }

    void check() const override
    {
        opencattus::Singleton<IRunner>::get()->executeCommand("dnf check");
    }

    void pinOSVersion() const override
    {
        const auto runner = opencattus::Singleton<IRunner>::get();
        switch (m_osinfo.getDistro()) {
            // Rocky Linux is pinned by using Vault, this is done
            // during the repository generation
            case OS::Distro::Rocky:
            // For Oracle Linux and Alma Linux I assume that we'll
            // need to do the same trick with the repositories
            case OS::Distro::AlmaLinux:
            case OS::Distro::OL:
                break;
            case OS::Distro::RHEL:
                runner->executeCommand(
                    fmt::format("subscription-manager release --set={}",
                        m_osinfo.getVersion()));
                break;
            default:
                std::unreachable();
        }
    }

    void clean() const override
    {
        opencattus::Singleton<IRunner>::get()->executeCommand("dnf clean all");
    }

    [[nodiscard]] std::vector<std::string> repolist() const override
    {
        return Singleton<IRunner>::get()->checkOutput("dnf repolist");
    }

    [[nodiscard]] bool enableService(std::string_view service) const override
    {
        return Singleton<IRunner>::get()->executeCommand(
                   fmt::format("systemctl enable --now {}", service))
            == 0;
    };

    [[nodiscard]] bool disableService(std::string_view service) const override
    {
        return Singleton<IRunner>::get()->executeCommand(
                   fmt::format("systemctl disable --now {}", service))
            == 0;
    };

    [[nodiscard]] bool startService(std::string_view service) const override
    {
        return Singleton<IRunner>::get()->executeCommand(
                   fmt::format("systemctl start {}", service))
            == 0;
    };

    [[nodiscard]] bool stopService(std::string_view service) const override
    {
        return Singleton<IRunner>::get()->executeCommand(
                   fmt::format("systemctl stop {}", service))
            == 0;
    };

    [[nodiscard]] bool restartService(std::string_view service) const override
    {
        return Singleton<IRunner>::get()->executeCommand(
                   fmt::format("systemctl restart {}", service))
            == 0;
    };
};

class UbuntuOSService final : public IOSService {
    OS m_osinfo;

public:
    explicit UbuntuOSService(const OS& osinfo)
        : m_osinfo(osinfo) { };

    [[nodiscard]] std::string getKernelInstalled() const override
    {
        auto output = Singleton<IRunner>::get()->checkOutput(
            "bash -c \"dpkg-query -W -f='${Version} ${Package}\\n' "
            "'linux-image-*' 2>/dev/null | sort -Vr | head -1 | awk "
            "'{print $1}'\"");
        return output.empty() ? getKernelRunning() : output[0];
    }

    [[nodiscard]] std::string getKernelRunning() const override
    {
        return Singleton<IRunner>::get()->checkOutput("uname -r")[0];
    }

    [[nodiscard]] std::string getLocale() const override
    {
        return Singleton<IRunner>::get()->checkOutput(
            R"(bash -c "localectl status | awk -F'=' '/System Locale: / {print $2}'")")
            [0];
    }

    [[nodiscard]] std::vector<std::string> getAvailableLocales() const override
    {
        return Singleton<IRunner>::get()->checkOutput("locale -a");
    }

    [[nodiscard]] bool install(std::string_view package) const override
    {
        return Singleton<IRunner>::get()->executeCommand(fmt::format(
                   "DEBIAN_FRONTEND=noninteractive apt-get install -y {}",
                   package))
            != 0;
    }

    [[nodiscard]] bool reinstall(std::string_view package) const override
    {
        return Singleton<IRunner>::get()->executeCommand(fmt::format(
                   "DEBIAN_FRONTEND=noninteractive apt-get install --reinstall "
                   "-y {}",
                   package))
            != 0;
    }

    [[nodiscard]] bool groupInstall(std::string_view package) const override
    {
        return install(package);
    }

    [[nodiscard]] bool remove(std::string_view package) const override
    {
        return Singleton<IRunner>::get()->executeCommand(fmt::format(
                   "DEBIAN_FRONTEND=noninteractive apt-get remove -y {}",
                   package))
            != 0;
    }

    [[nodiscard]] bool update(std::string_view package) const override
    {
        return Singleton<IRunner>::get()->executeCommand(
                   fmt::format("DEBIAN_FRONTEND=noninteractive apt-get install "
                               "--only-upgrade -y {}",
                       package))
            != 0;
    }

    [[nodiscard]] bool update() const override
    {
        return Singleton<IRunner>::get()->executeCommand(
                   "DEBIAN_FRONTEND=noninteractive apt-get update && "
                   "DEBIAN_FRONTEND=noninteractive apt-get upgrade -y")
            != 0;
    }

    void check() const override
    {
        Singleton<IRunner>::get()->executeCommand("apt-get check");
    }

    void pinOSVersion() const override { static_cast<void>(m_osinfo); }

    void clean() const override
    {
        Singleton<IRunner>::get()->executeCommand("apt-get clean");
    }

    [[nodiscard]] std::vector<std::string> repolist() const override
    {
        return Singleton<IRunner>::get()->checkOutput("apt-cache policy");
    }

    [[nodiscard]] bool enableService(std::string_view service) const override
    {
        return Singleton<IRunner>::get()->executeCommand(
                   fmt::format("systemctl enable --now {}", service))
            == 0;
    };

    [[nodiscard]] bool disableService(std::string_view service) const override
    {
        return Singleton<IRunner>::get()->executeCommand(
                   fmt::format("systemctl disable --now {}", service))
            == 0;
    };

    [[nodiscard]] bool startService(std::string_view service) const override
    {
        return Singleton<IRunner>::get()->executeCommand(
                   fmt::format("systemctl start {}", service))
            == 0;
    };

    [[nodiscard]] bool stopService(std::string_view service) const override
    {
        return Singleton<IRunner>::get()->executeCommand(
                   fmt::format("systemctl stop {}", service))
            == 0;
    };

    [[nodiscard]] bool restartService(std::string_view service) const override
    {
        return Singleton<IRunner>::get()->executeCommand(
                   fmt::format("systemctl restart {}", service))
            == 0;
    };
};

std::unique_ptr<IOSService> IOSService::factory(const OS& osinfo)
{
    switch (osinfo.getDistro()) {
        case OS::Distro::RHEL:
        case OS::Distro::Rocky:
        case OS::Distro::AlmaLinux:
        case OS::Distro::OL:
            return makeUniqueDerived<IOSService, ELOSService>(osinfo);
        case OS::Distro::Ubuntu:
            return makeUniqueDerived<IOSService, UbuntuOSService>(osinfo);
        default:
            throw std::logic_error("Not implemented");
    }
}

std::optional<bool> RockyLinux::m_shouldUseVault = std::nullopt;
bool RockyLinux::shouldUseVault(const OS& osinfo)
{
    if (m_shouldUseVault) {
        return m_shouldUseVault.value();
    }
    const auto lastVersion = osinfo.getVersion();
    LOG_INFO("Checking if Rocky {} should use vault", lastVersion);
    auto output = opencattus::functions::getHttpStatus(
        fmt::format("https://dl.rockylinux.org/vault/rocky/{}/BaseOS/x86_64/os/"
                    "repodata/repomd.xml",
            lastVersion),
        1);
    const auto should = output == "200";
    LOG_INFO("{}", should ? "Yes, use vault" : "No, don't use vault");
    // Cache it
    m_shouldUseVault = std::make_optional(should);
    return should;
}

}; // namespace opencattus::services {
