/*
 * Copyright 2021 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <algorithm>
#include <filesystem>
#include <fmt/core.h>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/property_tree/ptree.hpp>
#include <gsl/gsl-lite.hpp>

#include <opencattus/functions.h>
#include <opencattus/models/cluster.h>
#include <opencattus/patterns/wrapper.h>
#include <opencattus/services/files.h>
#include <opencattus/services/init.h>
#include <opencattus/services/log.h>
#include <opencattus/services/options.h>
#include <opencattus/services/osservice.h>
#include <opencattus/services/repos.h>
#include <opencattus/services/runner.h>
#include <opencattus/utils/ranges.h>
#include <opencattus/utils/singleton.h>
#include <opencattus/utils/string.h>

#ifdef BUILD_TESTING
#include <doctest/doctest.h>
#else
#define DOCTEST_CONFIG_DISABLE
#include <doctest/doctest.h>
#endif

using opencattus::concepts::IsParser;
using opencattus::services::files::IsKeyFileReadable;
using opencattus::services::files::KeyFile;
using opencattus::services::repos::IRepository;
using std::filesystem::path;

namespace opencattus::services::repos {

// Represents a debian repository file
class DebianRepository : public IRepository {
private:
    std::string m_type; // "deb" or "deb-src"
    std::optional<std::string> m_options; // Optional, e.g., "[arch=amd64]"
    std::string m_uri; // URI, e.g., "http://deb.debian.org/debian"
    std::string m_distribution; // Distribution, e.g., "bookworm"
    std::vector<std::string>
        m_components; // Components, e.g., "main", "contrib", "non-free"
    std::filesystem::path m_source; // File where this is saved

public:
    explicit DebianRepository(const std::string& line);

    [[nodiscard]] std::string type() const;
    [[nodiscard]] std::optional<std::string> options() const;
    [[nodiscard]] std::optional<std::string> uri() const override;
    [[nodiscard]] std::string distribution() const;
    [[nodiscard]] std::vector<std::string> components() const;
    [[nodiscard]] std::filesystem::path source() const override;

    void type(std::string value);
    void options(std::optional<std::string> value);
    void uri(std::optional<std::string> value) override;
    void distribution(std::string value);
    void components(std::vector<std::string> value);
    void source(const std::filesystem::path value) override;
};

// Represents a RPM Repository inside a repository file (/etc/yum.repo.d/*.repo)
class RPMRepository final : public IRepository {
    std::string m_id;
    bool m_enabled = true;
    std::string m_name;
    std::optional<std::string> m_baseurl;
    std::optional<std::string> m_metalink;
    std::optional<std::string> m_gpgkey;
    bool m_gpgcheck = true;
    std::filesystem::path m_source;
    std::string m_group;

public:
    RPMRepository() = default;
    ~RPMRepository() override = default;
    RPMRepository(const RPMRepository&) = default;
    RPMRepository(RPMRepository&&) = default;
    RPMRepository& operator=(const RPMRepository&) = default;
    RPMRepository& operator=(RPMRepository&&) = default;

    [[nodiscard]] std::string id() const override { return m_id; };
    [[nodiscard]] bool enabled() const override { return m_enabled; };
    [[nodiscard]] std::string name() const override { return m_name; };
    [[nodiscard]] std::optional<std::string> uri() const override
    {
        return baseurl();
    };
    [[nodiscard]] std::filesystem::path source() const override
    {
        return m_source;
    };
    [[nodiscard]] std::string group() const { return m_group; };
    [[nodiscard]] std::optional<std::string> baseurl() const
    {
        return m_baseurl;
    };
    [[nodiscard]] std::optional<std::string> metalink() const
    {
        return m_metalink;
    };
    [[nodiscard]] bool gpgcheck() const { return m_gpgcheck; };
    [[nodiscard]] std::optional<std::string> gpgkey() const
    {
        return m_gpgkey;
    };

    void id(std::string value) override { m_id = value; };
    void enabled(bool enabled) override { m_enabled = enabled; };
    void name(std::string name) override { m_name = name; };
    void uri(std::optional<std::string> value) override { baseurl(value); };
    void source(std::filesystem::path source) override { m_source = source; };
    void group(std::string group) { m_group = std::move(group); };
    void baseurl(std::optional<std::string> baseurl)
    {
        m_baseurl = std::move(baseurl);
    };
    void metalink(std::optional<std::string> metalink)
    {
        m_metalink = std::move(metalink);
    };
    void gpgcheck(bool gpgcheck) { m_gpgcheck = gpgcheck; };
    void gpgkey(std::optional<std::string> gpgkey)
    {
        m_gpgkey = std::move(gpgkey);
    };

    void valid() const;

    bool operator==(const auto other) const { return other.id() == id(); }
};

namespace {

    // Easy conversions for string
    std::ostream& operator<<(std::ostream& ostr, const RPMRepository& repo)
    {
        ostr << "RPMRepository(";
        ostr << repo.id() << " ";
        ostr << repo.name() << " ";
        ostr << repo.baseurl().value_or("") << " ";
        ostr << "enabled=" << repo.enabled() << " ";
        ostr << ")";

        return ostr;
    };

    auto parseRepoBoolean(const KeyFile& file, const std::string& group,
        const std::string& key, const bool defaultValue) -> bool
    {
        const auto value = file.getStringOpt(group, key);
        if (!value.has_value()) {
            return defaultValue;
        }

        auto normalized = value.value();
        boost::algorithm::trim(normalized);
        boost::algorithm::to_lower(normalized);

        if (normalized == "1" || normalized == "true" || normalized == "yes") {
            return true;
        }

        if (normalized == "0" || normalized == "false" || normalized == "no") {
            return false;
        }

        throw std::runtime_error(
            fmt::format("Keyfile Error, invalid boolean [{}].{}={}", group, key,
                normalized));
    }

    template <typename T> std::string toString(const T& input)
    {
        std::ostringstream strm;
        strm << input;
        return strm.str();
    };

};

// Parses RPM .repo files
class RPMRepositoryParser final {
public:
    static void parse(const std::filesystem::path& path,
        std::map<std::string, std::shared_ptr<RPMRepository>>& output)
    {
        auto file = KeyFile(path);
        auto reponames = file.getGroups();
        for (const auto& repogroup : reponames) {
            auto name = file.getString(repogroup, "name");

            if (name.empty()) {
                opencattus::functions::abort(
                    "Could not load repo name from repo '{}'", repogroup);
            }

            auto metalink = file.getStringOpt(repogroup, "metalink");
            auto baseurl = file.getStringOpt(repogroup, "baseurl");
            auto gpgkey = file.getStringOpt(repogroup, "gpgkey");
            auto enabled = parseRepoBoolean(file, repogroup, "enabled", true);
            auto gpgcheck = parseRepoBoolean(
                file, repogroup, "gpgcheck", gpgkey.has_value());

            RPMRepository repo;
            repo.group(repogroup);
            repo.name(name);
            repo.metalink(metalink);
            repo.baseurl(baseurl);
            repo.enabled(enabled);
            repo.gpgcheck(gpgcheck);
            repo.gpgkey(gpgkey);
            repo.source(path.string());
            repo.id(repogroup);
            repo.valid();

            output.emplace(repo.id(), std::make_shared<RPMRepository>(repo));
        }
    }

    static void unparse(
        const std::map<std::string, std::shared_ptr<RPMRepository>>& repos,
        const std::filesystem::path& path)
    {
        auto file = opencattus::services::files::KeyFile(path);
        for (const auto& [repoId, repo] : repos) {
            file.setString(repo->group(), "name", repo->name());
            file.setBoolean(repo->group(), "enabled", repo->enabled());
            file.setBoolean(repo->group(), "gpgcheck", repo->gpgcheck());
            file.setString(repo->group(), "gpgkey", repo->gpgkey());
            file.setString(repo->group(), "metalink", repo->metalink());
            file.setString(repo->group(), "baseurl", repo->baseurl());
        }

        file.save();
    }
};
static_assert(IsParser<RPMRepositoryParser, std::filesystem::path,
    std::map<std::string, std::shared_ptr<RPMRepository>>>);

// Represents a file inside /etc/yum.repos.d
class RPMRepositoryFile final {
    std::filesystem::path m_path;

    // @FIXME: Double check if this is required to be shared_ptr
    std::map<std::string, std::shared_ptr<RPMRepository>> m_repos;

public:
    explicit RPMRepositoryFile(auto path)
        : m_path(std::move(path))
    {
        RPMRepositoryParser::parse(m_path, m_repos);
    }

    RPMRepositoryFile(std::filesystem::path path,
        std::map<std::string, std::shared_ptr<RPMRepository>> repos)
        : m_path(std::move(path))
        , m_repos(std::move(repos))
    {
    }

    const auto& path() { return m_path; }

    auto& repos() { return m_repos; }

    auto repo(const std::string& name) { return m_repos.at(name); }

    void save() const
    {
        LOG_DEBUG("Saving {}", m_path.string());
        RPMRepositoryParser::unparse(m_repos, m_path);
    }
};

TEST_SUITE_BEGIN("opencattus::services::repos");

/**
 * @brief Decouples filesystem and network I/O from the MirrorRepoConfig
 *   and UpstreamRepoConfig
 */
struct DefaultMirrorExistenceChecker final {
    [[nodiscard]] static bool pathExists(const std::filesystem::path& path)
    {
        return opencattus::functions::exists(path);
    }

    [[nodiscard]] static bool urlExists(const std::string& url)
    {
        return opencattus::functions::getHttpStatus(url) == "200";
    }
};

// For testing
struct FalseMirrorExistenceChecker final {
    [[nodiscard]] static bool pathExists(const std::filesystem::path& path)
    {
        static_cast<void>(path);
        return false;
    }

    [[nodiscard]] static bool urlExists(const std::string& url)
    {
        static_cast<void>(url);
        return false;
    }
};

// For testing
struct TrueMirrorExistenceChecker final {
    [[nodiscard]] static bool pathExists(const std::filesystem::path& path)
    {
        static_cast<void>(path);
        return true;
    }

    [[nodiscard]] static bool urlExists(const std::string& url)
    {
        static_cast<void>(url);
        return true;
    }
};

// Represents repository path/url and gpg path/url
struct RepoPaths final {
    std::string repo;
    std::optional<std::string> gpgkey = std::nullopt;
};

// Uniquely identify a remote repository
struct RepoId final {
    std::string id;
    std::string name;
    std::string filename;
};

// Upstream and Mirror configuration for a single repository
struct RepoConfig final {
    RepoId repoId;
    RepoPaths mirror;
    RepoPaths upstream;
};

// Represent variables values present in repos.conf to be interpolated during
// the parsing
struct RepoConfigVars final {
    std::string arch; // ex: x86_64
    std::string beegfsVersion; // latest-stable or beegfs_<version>
    std::string ohpcVersion; // major, ex: 3
    std::string osversion; // major.minor, ex: 9.5
    std::string releasever; // major, ex: 9
    std::string xcatVersion; // major.minor, ex: 2.17 or latest
    std::string zabbixVersion; // major.minor LTS, ex: 7.0
    std::string ofedVersion; // major.minor, ex: 6.4
    std::string ofedRepoTarget; // repo OS target, ex: 9.6 or 10
    std::string cudaGPGKey; // NVIDIA CUDA repository key filename
    std::string rhelBaseMirrorGPGKey; // optional local mirror GPG key
    std::string rhelCodeReadyMirrorRepo; // local mirror override for RHEL CRB
    std::string rhelCodeReadyMirrorGPGKey; // optional local mirror GPG key
};

// Represents a Mirror Repository
template <typename MirrorExistenceChecker = DefaultMirrorExistenceChecker>
struct MirrorRepo final {
    RepoPaths paths;

    [[nodiscard]] std::string baseurl() const
    {
        const auto opts = opencattus::utils::singleton::options();

        return opencattus::utils::string::rstrip(
            fmt::format("{mirrorUrl}/{path}",
                fmt::arg("mirrorUrl", opts->mirrorBaseUrl),
                fmt::arg("path", paths.repo)),
            "/");
    };

    [[nodiscard]] std::optional<std::string> gpgkey() const
    {
        if (!paths.gpgkey) {
            return std::nullopt;
        }

        const auto opts = opencattus::utils::singleton::options();
        return fmt::format("{mirrorUrl}/{path}",
            fmt::arg("mirrorUrl", opts->mirrorBaseUrl),
            fmt::arg("path", paths.gpgkey.value()));
    }

    [[nodiscard]] static bool isLocalUrl(const std::string_view url)
    {
        return url.starts_with("file://");
    }

    [[nodiscard]] static std::string localPath(const std::string_view url)
    {
        assert(isLocalUrl(url));
        return std::string(url.substr(std::string_view("file://").length()));
    }

    [[nodiscard]] bool exists() const
    {
        if (paths.repo.empty()) {
            return false;
        }
        const auto opts = opencattus::utils::singleton::options();
        if (isLocalUrl(opts->mirrorBaseUrl)) {
            return MirrorExistenceChecker::pathExists(localPath(baseurl()));
        } else {
            return MirrorExistenceChecker::urlExists(
                baseurl() + "/repodata/repomd.xml");
        }
    }
};

TEST_CASE("MirrorRepo")
{
    // NOLINTNEXTLINE
    auto opts = Options { .mirrorBaseUrl = "https://mirror.example.com" };
    opencattus::Singleton<const Options>::init(
        std::make_unique<const Options>(opts));
    // Log::init(5);

    auto mirrorConfigOnline = MirrorRepo<TrueMirrorExistenceChecker> { .paths
        = { .repo = "myrepo/repo", .gpgkey = "myrepo/key.gpg" } };
    auto mirrorConfigOffline = MirrorRepo<FalseMirrorExistenceChecker> { .paths
        = { .repo = "myrepo/repo", .gpgkey = "myrepo/key.gpg" } };
    CHECK(mirrorConfigOnline.baseurl()
        == "https://mirror.example.com/myrepo/repo");
    CHECK(mirrorConfigOnline.gpgkey().value()
        == "https://mirror.example.com/myrepo/key.gpg");

    // Test local paths
    opencattus::Singleton<const Options>::init(std::make_unique<const Options>(
        Options { .mirrorBaseUrl = "file:///var/run/repos" }));
    CHECK(mirrorConfigOnline.baseurl() == "file:///var/run/repos/myrepo/repo");
    CHECK(mirrorConfigOnline.gpgkey().value()
        == "file:///var/run/repos/myrepo/key.gpg");

    // Test existence
    CHECK(mirrorConfigOnline.exists());
    CHECK(!mirrorConfigOffline.exists());
}

// Represents an upstream repository
template <typename MirrorExistenceChecker = DefaultMirrorExistenceChecker>
struct UpstreamRepo final {
    RepoPaths paths;

    [[nodiscard]] std::string baseurl() const
    {
        return opencattus::utils::string::rstrip(paths.repo, "/");
    };

    [[nodiscard]] std::optional<std::string> gpgurl() const
    {
        if (!paths.gpgkey) {
            return std::nullopt;
        }

        return paths.gpgkey;
    };

    [[nodiscard]] constexpr bool exists() const
    {
        return MirrorExistenceChecker::urlExists(
            baseurl() + "/repodata/repomd.xml");
    };

    [[nodiscard]] constexpr std::optional<std::string> gpgkey() const
    {
        if (!paths.gpgkey) {
            return std::nullopt;
        }

        const auto url = gpgurl();
        if (!url) {
            return std::nullopt;
        }

        if (MirrorExistenceChecker::urlExists(url.value())) {
            return gpgurl();
        } else {
            LOG_WARN(
                "GPG not found, assuming disabled GPG check {}", url.value());
            return std::nullopt;
        }
    }
};

// Chose between upstream or mirror repository
struct RepoChooser final {
    enum class Choice : bool { UPSTREAM, MIRROR };

    template <typename MirrorChecker, typename UpstreamChecker>
    static constexpr Choice choose(const MirrorRepo<MirrorChecker>& mirror,
        const UpstreamRepo<UpstreamChecker>& upstream,
        const bool forceUpstream = false)
    {
        if (forceUpstream) {
            return Choice::UPSTREAM;
        }

        const auto opts = opencattus::utils::singleton::options();
        if (!opts->enableMirrors) {
            return Choice::UPSTREAM;
        }

        if (!mirror.exists()) {
            LOG_WARN("Mirror does not exists falling back to upstream {}",
                upstream.baseurl());

            if (!upstream.exists()) {
                LOG_WARN("Upstream URL error, is the URL correct? {}",
                    upstream.baseurl());
            }
            return Choice::UPSTREAM;
        }

        return Choice::MIRROR;
    }
};

TEST_CASE("RepoChooser")
{
    // NOLINTNEXTLINE
    auto opts = Options { .mirrorBaseUrl = "https://mirror.example.com" };
    opencattus::Singleton<const Options>::init(
        std::make_unique<const Options>(opts));
    // Log::init(5);

    auto mirrorConfigOnline = MirrorRepo<TrueMirrorExistenceChecker> { .paths
        = { .repo = "myrepo/repo", .gpgkey = "myrepo/key.gpg" } };
    auto mirrorConfigOffline = MirrorRepo<FalseMirrorExistenceChecker> { .paths
        = { .repo = "myrepo/repo", .gpgkey = "myrepo/key.gpg" } };
    auto upstreamConfig = UpstreamRepo<TrueMirrorExistenceChecker> { .paths
        = { .repo = "https://upstream.example.com/upstream/repo",
            .gpgkey = "https://upstream.example.com/upstream/key.gpg" } };

    opencattus::Singleton<const Options>::init(
        std::make_unique<const Options>(Options {
            .enableMirrors = true,
            .mirrorBaseUrl = opts.mirrorBaseUrl,
        }));
    auto choice1 = RepoChooser::choose(mirrorConfigOnline, upstreamConfig);
    CHECK(choice1 == RepoChooser::Choice::MIRROR);
    auto choice2 = RepoChooser::choose(mirrorConfigOffline, upstreamConfig);
    CHECK(choice2 == RepoChooser::Choice::UPSTREAM);
}

// Converts RepoConfig to RPMRepository do HTTP requests to decide
// between upstream or mirror
struct RepoAssembler final {
    template <typename MChecker, typename UChecker>
    static constexpr RPMRepository assemble(const RepoId& repoid,
        const MirrorRepo<MChecker>& mirror,
        const UpstreamRepo<UChecker>& upstream, const bool enabled = false,
        const bool forceUpstream = false)
    {
        auto repo = RPMRepository { };
        repo.group(static_cast<std::string>(repoid.id));
        repo.id(static_cast<std::string>(repoid.id));
        repo.name(static_cast<std::string>(repoid.name));
        repo.source(repoid.filename);
        repo.enabled(enabled);

        const auto choice
            = RepoChooser::choose(mirror, upstream, forceUpstream);
        switch (choice) {
            case RepoChooser::Choice::UPSTREAM:
                repo.baseurl(upstream.baseurl());
                repo.gpgkey(upstream.gpgkey());
                repo.gpgcheck(upstream.gpgkey().has_value());
                break;
            case RepoChooser::Choice::MIRROR:
                repo.baseurl(mirror.baseurl());
                repo.gpgkey(mirror.gpgkey());
                repo.gpgcheck(mirror.gpgkey().has_value());
                break;
        }

        return repo;
    }
};

TEST_CASE("RepoAssembler")
{
    // NOLINTNEXTLINE
    auto opts = Options { .mirrorBaseUrl = "https://mirror.example.com" };
    opencattus::Singleton<const Options>::init(
        std::make_unique<const Options>(opts));
    // Log::init(5);

    auto mirrorConfigOffline = MirrorRepo<FalseMirrorExistenceChecker> { .paths
        = { .repo = "myrepo/repo", .gpgkey = "myrepo/key.gpg" } };
    auto mirrorConfigOnline = MirrorRepo<TrueMirrorExistenceChecker> { .paths
        = { .repo = "myrepo/repo", .gpgkey = "myrepo/key.gpg" } };
    auto upstreamConfig = UpstreamRepo<TrueMirrorExistenceChecker> { .paths
        = { .repo = "https://upstream.example.com/upstream/repo",
            .gpgkey = "https://upstream.example.com/upstream/key.gpg" } };

    // If mirror is offline it should fallback to upstream
    CHECK(RepoChooser::choose(mirrorConfigOffline, upstreamConfig)
        == RepoChooser::Choice::UPSTREAM);
    // If mirror is online it should chose the default
    constexpr auto defaultRepoChoice = RepoChooser::Choice::UPSTREAM;
    CHECK(RepoChooser::choose(mirrorConfigOnline, upstreamConfig)
        == defaultRepoChoice);

    // Disable mirrors
    opts.enableMirrors = false;
    opencattus::Singleton<const Options>::init(
        std::make_unique<const Options>(opts));

    // If mirrors are disabled it should choose the upstream even if the
    // mirror is online
    CHECK(RepoChooser::choose(mirrorConfigOnline, upstreamConfig)
        == RepoChooser::Choice::UPSTREAM);

    auto repoId = RepoId {
        .id = "myrepo",
        .name = "My very cool repository full of packages",
        .filename = "myrepo.repo",
    };

    auto repoUpstream
        = RepoAssembler::assemble(repoId, mirrorConfigOffline, upstreamConfig);
    CHECK(repoUpstream.baseurl().value() == upstreamConfig.baseurl());

    // Enable mirrors again
    opts.enableMirrors = true;
    opencattus::Singleton<const Options>::init(
        std::make_unique<const Options>(opts));
    auto repoMirror
        = RepoAssembler::assemble(repoId, mirrorConfigOnline, upstreamConfig);
    // CHECK(repoMirror.baseurl().value() == mirrorConfigOnline.baseurl());
}

// In-memory representation of a repos.conf
class RepoConfFile final {
    // RepoConfigs grouped by file name
    std::map<std::string, std::vector<RepoConfig>> m_files;

public:
    void insert(const std::string& filename, RepoConfig& value)
    {
        if (!m_files.contains(filename)) {
            m_files.emplace(filename, std::vector<RepoConfig> { });
        }
        m_files.at(filename).emplace_back(value);
    }

    [[nodiscard]] const auto& at(const std::string& key) const
    {
        return m_files.at(key);
    }

    // RepoConfigs grouped by file name
    [[nodiscard]] const auto& files() const { return m_files; }
    //
    // RepoConfigs grouped by file name
    [[nodiscard]] std::vector<std::string> filesnames() const
    {
        return m_files
            | std::views::transform([](const auto& pair) { return pair.first; })
            | opencattus::utils::ranges::to<std::vector>();
    }

    // Find a RepoConfig by repository id, if it exists
    [[nodiscard]] std::optional<RepoConfig> find(const auto& repoid) const
    {
        for (const auto& [_, configs] : m_files) {
            for (const auto& config : configs) {
                if (config.repoId.id == repoid) {
                    return config;
                }
            }
        };

        return std::nullopt;
    }
};

struct RepoConfFiles {
    RepoConfFile distroRepos;
    RepoConfFile nonDistroRepos;
};

//
// Parser for repos.conf
//
class RepoConfigParser final {
    static std::string interpolateVars(const auto& fmt, const auto& vars)
    {
        return fmt::format(fmt::runtime(fmt),
            fmt::arg("releasever", vars.releasever),
            fmt::arg("osversion", vars.osversion), fmt::arg("arch", vars.arch),
            fmt::arg("beegfsVersion", vars.beegfsVersion),
            fmt::arg("zabbixVersion", vars.zabbixVersion),
            fmt::arg("xcatVersion", vars.xcatVersion),
            fmt::arg("ohpcVersion", vars.ohpcVersion),
            fmt::arg("ofedVersion", vars.ofedVersion),
            fmt::arg("ofedRepoTarget", vars.ofedRepoTarget),
            fmt::arg("cudaGPGKey", vars.cudaGPGKey),
            fmt::arg("rhelBaseMirrorGPGKey", vars.rhelBaseMirrorGPGKey),
            fmt::arg("rhelCodeReadyMirrorRepo", vars.rhelCodeReadyMirrorRepo),
            fmt::arg(
                "rhelCodeReadyMirrorGPGKey", vars.rhelCodeReadyMirrorGPGKey));
    };

public:
    // Base path used during production, tests use another path
    static constexpr std::string_view defaultPath
        = "/opt/opencattus/conf/repos/";
    static void parse(const std::filesystem::path& path, RepoConfFile& output,
        const RepoConfigVars& vars)
    {
        LOG_DEBUG("Loading repo config: {}", path);
        if (!opencattus::functions::exists(path)) {
            throw std::runtime_error(fmt::format(
                "Repository configuration file is missing: {}", path));
        }
        auto file = KeyFile(path);
        auto repoNames = file.getGroups();

        for (const auto& repoGroup : repoNames) {
            RepoConfig repo;

            // repoId.id
            try {
                repo.repoId.id = interpolateVars(repoGroup, vars);
            } catch (const fmt::format_error& e) {
                opencattus::functions::abort(
                    "Failed to format repository id for repo '{}': {}",
                    repoGroup, e.what());
            }

            // name
            auto name = file.getString(repoGroup, "name");
            if (name.empty()) {
                opencattus::functions::abort(
                    "Could not load name from repo '{}'", repoGroup);
            }
            try {
                repo.repoId.name = interpolateVars(name, vars);
            } catch (const fmt::format_error& e) {
                opencattus::functions::abort(
                    "Failed to format name for repo '{}': {}", repoGroup,
                    e.what());
            }

            // filename (no placeholders)
            repo.repoId.filename = file.getString(repoGroup, "filename");
            if (repo.repoId.filename.empty()) {
                opencattus::functions::abort(
                    "Could not load filename from repo '{}'", repoGroup);
            }

            // mirror.repo
            const auto mirrorRepoOpt
                = file.getStringOpt(repoGroup, "mirror.repo");
            if (mirrorRepoOpt) {
                const auto mirrorRepo = mirrorRepoOpt.value();
                try {
                    repo.mirror.repo = interpolateVars(mirrorRepo, vars);
                } catch (const fmt::format_error& e) {
                    opencattus::functions::abort(
                        "Could not interpolate mirror.repo from repo '{}'",
                        repoGroup);
                }
            } else {
                repo.mirror.repo = "";
            }

            // mirror.gpgkey (optional)
            auto mirrorGpgkey = file.getStringOpt(repoGroup, "mirror.gpgkey");
            if (mirrorGpgkey) {
                try {
                    const auto value
                        = interpolateVars(mirrorGpgkey.value(), vars);
                    repo.mirror.gpgkey = value.empty()
                        ? std::nullopt
                        : std::make_optional(value);
                } catch (const fmt::format_error& e) {
                    opencattus::functions::abort(
                        "Failed to format mirror.gpgkey for repo '{}': {}",
                        repoGroup, e.what());
                }
            } else {
                repo.mirror.gpgkey = std::nullopt;
            }

            // upstream.repo
            auto upstreamRepo = file.getString(repoGroup, "upstream.repo");
            if (upstreamRepo.empty()) {
                opencattus::functions::abort(
                    "Could not load upstream.repo from repo '{}'", repoGroup);
            }
            try {
                repo.upstream.repo = interpolateVars(upstreamRepo, vars);
            } catch (const fmt::format_error& e) {
                opencattus::functions::abort(
                    "Failed to format upstream.repo for repo '{}': {}",
                    repoGroup, e.what());
            }

            // upstream.gpgkey (optional)
            auto upstreamGpgkey
                = file.getStringOpt(repoGroup, "upstream.gpgkey");
            if (upstreamGpgkey) {
                try {
                    const auto value
                        = interpolateVars(upstreamGpgkey.value(), vars);
                    repo.upstream.gpgkey = value.empty()
                        ? std::nullopt
                        : std::make_optional(value);
                } catch (const fmt::format_error& e) {
                    opencattus::functions::abort(
                        "Failed to format upstream.gpgkey for repo '{}': {}",
                        repoGroup, e.what());
                }
            } else {
                repo.upstream.gpgkey = std::nullopt;
            }

            LOG_TRACE("Loaded repository configuration for {} from {}",
                interpolateVars(repo.repoId.id, vars), path);
            output.insert(repo.repoId.filename, repo);
        }
    };

#ifdef BUILD_TESTING
    // Parse a repo.conf file and return a RepoConfFile with default vars,
    // for testing only
    static RepoConfFile parseTest(const std::filesystem::path& path,
        const RepoConfigVars& vars = RepoConfigVars {
            .arch = "x86_64",
            .beegfsVersion = "latest-stable",
            .ohpcVersion = "3",
            .osversion = "9.5",
            .releasever = "9",
            .xcatVersion = "latest",
            .zabbixVersion = "7.0",
            .ofedVersion = "latest-3.2-LTS",
            .ofedRepoTarget = "9",
            .cudaGPGKey = "D42D0685.pub",
        })
    {
        RepoConfFile conffile;
        parse(path, conffile, vars);
        return conffile;
    };
#endif

    // Parse a repo.conf file and return a RepoConfFile using default path
    static RepoConfFile parse(const RepoConfigVars& vars)
    {
        RepoConfFile conffile;
        parse(defaultPath, conffile, vars);
        return conffile;
    };

    template <typename UseVaultService = RockyLinux>
    static constexpr RepoConfFiles load(const std::filesystem::path& basePath,
        const OS& osinfo, const RepoConfigVars& vars)
    {
        RepoConfFiles conffile;
        const auto commonReposPath = basePath / "repos.conf";
        const auto distroReposPath
            = [&osinfo, &basePath]() -> std::filesystem::path {
            switch (osinfo.getDistro()) {
                case OS::Distro::RHEL:
                    return basePath / "rhel.conf";
                case OS::Distro::OL:
                    return basePath / "oracle.conf";
                case OS::Distro::AlmaLinux:
                    return basePath / "alma.conf";
                case OS::Distro::Rocky:
                    if (UseVaultService::shouldUseVault(osinfo)) {
                        return basePath / "rocky-vault.conf";
                    } else {
                        return basePath / "rocky-upstream.conf";
                    }
                default:
                    std::unreachable();
            }
        }();
        parse(commonReposPath, conffile.nonDistroRepos, vars);
        parse(distroReposPath, conffile.distroRepos, vars);
        return conffile;
    }
};

auto currentExecutablePath() -> std::optional<std::filesystem::path>
{
#ifdef __linux__
    try {
        auto executable = std::filesystem::read_symlink("/proc/self/exe");
        if (!executable.empty()) {
            return executable;
        }
    } catch (const std::filesystem::filesystem_error& e) {
        LOG_DEBUG("Could not resolve /proc/self/exe: {}", e.what());
    }
#endif

    return std::nullopt;
}

auto repoConfigCandidatePaths(const std::filesystem::path& currentPath,
    const std::optional<std::filesystem::path>& executablePath)
    -> std::vector<std::filesystem::path>
{
    std::vector<std::filesystem::path> candidates;
    const auto appendCandidate = [&candidates](std::filesystem::path path) {
        path = path.lexically_normal();
        if (std::ranges::find(candidates, path) == candidates.end()) {
            candidates.emplace_back(std::move(path));
        }
    };

    if (executablePath.has_value()) {
        const auto executableDirectory = executablePath->parent_path();
        appendCandidate(executableDirectory / "repos");
        appendCandidate(executableDirectory / "../repos");
        appendCandidate(executableDirectory / "../../repos");
    }

    appendCandidate(RepoConfigParser::defaultPath);

    appendCandidate(currentPath / "repos");
    appendCandidate(currentPath / "../repos");
    appendCandidate(currentPath / "../../repos");

    return candidates;
}

auto hasRepoConfig(const std::filesystem::path& path) -> bool
{
    return opencattus::functions::exists(path / "repos.conf");
}

auto findRepoConfigBasePath(
    const std::vector<std::filesystem::path>& candidates)
    -> std::optional<std::filesystem::path>
{
    for (const auto& candidate : candidates) {
        if (hasRepoConfig(candidate)) {
            return candidate;
        }
    }

    return std::nullopt;
}

auto formatRepoConfigCandidates(
    const std::vector<std::filesystem::path>& candidates) -> std::string
{
    std::ostringstream output;
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (index > 0) {
            output << ", ";
        }
        output << candidates[index].string();
    }
    return output.str();
}

auto repoConfigBasePath() -> std::filesystem::path
{
    const auto candidates = repoConfigCandidatePaths(
        std::filesystem::current_path(), currentExecutablePath());

    if (const auto basePath = findRepoConfigBasePath(candidates)) {
        LOG_DEBUG("Using repository config path {}", *basePath);
        return *basePath;
    }

    throw std::runtime_error(
        fmt::format("Repository configuration was not found. Checked: {}",
            formatRepoConfigCandidates(candidates)));
}

TEST_CASE("repo config discovery finds source repos from build-tree binary")
{
    const auto basePath
        = std::filesystem::path("test/output/repo-config-discovery");
    const auto sourceRoot = basePath / "checkout";
    const auto repoPath = sourceRoot / "repos";
    const auto executablePath
        = sourceRoot / "build-host" / "src" / "opencattus";

    std::filesystem::remove_all(basePath);
    std::filesystem::create_directories(repoPath);
    std::filesystem::create_directories(executablePath.parent_path());
    std::ofstream(repoPath / "repos.conf") << '\n';

    const auto candidates
        = repoConfigCandidatePaths(basePath / "home", executablePath);
    const auto selected = findRepoConfigBasePath(candidates);

    REQUIRE(selected.has_value());
    CHECK(selected->lexically_normal() == repoPath.lexically_normal());

    std::filesystem::remove_all(basePath);
}

TEST_CASE("repo config discovery ignores directories without repos.conf")
{
    const auto basePath
        = std::filesystem::path("test/output/repo-config-missing-common");
    const auto repoPath = basePath / "repos";

    std::filesystem::remove_all(basePath);
    std::filesystem::create_directories(repoPath);

    const auto selected = findRepoConfigBasePath({ repoPath });

    CHECK_FALSE(selected.has_value());

    std::filesystem::remove_all(basePath);
}

TEST_CASE("repo config parser reports missing config files")
{
    const auto missing = std::filesystem::path(
        "test/output/repo-config-parser-missing/repos.conf");

    std::filesystem::remove(missing);

    CHECK_THROWS_WITH_AS(RepoConfigParser::parseTest(missing),
        doctest::Contains("Repository configuration file is missing"),
        std::runtime_error);
}

TEST_CASE("repo config discovery resolves the runtime repository config path")
{
    const auto selected = repoConfigBasePath();

    CHECK(hasRepoConfig(selected));
}

std::string defaultOpenHPCVersionFor(const OS& osinfo)
{
    switch (osinfo.getPlatform()) {
        case OS::Platform::el8:
            return "2";
        case OS::Platform::el9:
            return "3";
        case OS::Platform::el10:
            return "4";
        case OS::Platform::ubuntu24:
            return "versatushpc-4";
        default:
            throw std::runtime_error(
                fmt::format("Unsupported OpenHPC repository baseline for {}",
                    osinfo.getMajorVersion()));
    }
}

std::string defaultZabbixVersionFor(std::string_view requestedVersion)
{
    if (requestedVersion.empty() || requestedVersion == "latest-lts"
        || requestedVersion == "lts") {
        return "7.0";
    }

    return std::string(requestedVersion);
}

std::optional<std::pair<int, int>> parseDocaLtsVersion(
    std::string_view ofedVersion)
{
    const auto normalized
        = opencattus::utils::string::lower(std::string(ofedVersion));
    if (!normalized.contains("lts")) {
        return std::nullopt;
    }

    constexpr std::string_view prefix = "latest-";
    constexpr std::string_view suffix = "-lts";
    const auto prefixPos = normalized.find(prefix);
    const auto suffixPos = normalized.rfind(suffix);
    if (prefixPos == std::string::npos || suffixPos == std::string::npos
        || suffixPos <= prefixPos + prefix.size()) {
        return std::nullopt;
    }

    const auto versionToken = normalized.substr(
        prefixPos + prefix.size(), suffixPos - (prefixPos + prefix.size()));
    const auto dotPos = versionToken.find('.');
    if (dotPos == std::string::npos) {
        return std::nullopt;
    }

    try {
        return std::pair { std::stoi(versionToken.substr(0, dotPos)),
            std::stoi(versionToken.substr(dotPos + 1)) };
    } catch (...) {
        return std::nullopt;
    }
}

bool usesDocaMajorVersionRepo(std::string_view ofedVersion)
{
    const auto normalized
        = opencattus::utils::string::lower(std::string(ofedVersion));
    if (!normalized.contains("lts")) {
        return true;
    }

    if (const auto parsed = parseDocaLtsVersion(ofedVersion);
        parsed.has_value()) {
        return parsed.value() >= std::pair { 3, 2 };
    }

    return false;
}

std::string defaultDOCARepoTargetFor(
    const OS& osinfo, std::string_view ofedVersion)
{
    if (osinfo.getDistro() == OS::Distro::Ubuntu) {
        return "";
    }

    if (usesDocaMajorVersionRepo(ofedVersion)) {
        switch (osinfo.getPlatform()) {
            case OS::Platform::el8:
                return "8";
            case OS::Platform::el9:
                return "9";
            case OS::Platform::el10:
                return "10";
            default:
                throw std::runtime_error(
                    fmt::format("Unsupported DOCA repository baseline for EL{}",
                        osinfo.getMajorVersion()));
        }
    }

    switch (osinfo.getPlatform()) {
        case OS::Platform::el8:
            return "8.10";
        case OS::Platform::el9:
            return "9.6";
        case OS::Platform::el10:
            throw std::runtime_error(
                "Legacy DOCA LTS repo target is not defined for EL10; choose a "
                "major-version DOCA stream such as latest or latest-3.2-LTS");
        default:
            throw std::runtime_error(
                fmt::format("Unsupported DOCA repository baseline for EL{}",
                    osinfo.getMajorVersion()));
    }
}

std::string defaultCUDAGPGKeyFor(const OS& osinfo)
{
    switch (osinfo.getPlatform()) {
        case OS::Platform::el8:
        case OS::Platform::el9:
            return "D42D0685.pub";
        case OS::Platform::el10:
            return "CDF6BA43.pub";
        case OS::Platform::ubuntu24:
            return "";
        default:
            throw std::runtime_error(
                fmt::format("Unsupported CUDA repository baseline for {}",
                    osinfo.getMajorVersion()));
    }
}

std::string defaultRHELCodeReadyMirrorRepoFor(const OS& osinfo)
{
    if (osinfo.getDistro() == OS::Distro::Ubuntu) {
        return "";
    }

    const auto arch = opencattus::utils::enums::toString(osinfo.getArch());
    switch (osinfo.getPlatform()) {
        case OS::Platform::el8:
        case OS::Platform::el9:
            return fmt::format("rhel/codeready-builder-for-rhel-{}-{}-rpms/",
                osinfo.getMajorVersion(), arch);
        case OS::Platform::el10:
            return fmt::format(
                "rocky/linux/{}/CRB/{}/os/", osinfo.getMajorVersion(), arch);
        default:
            throw std::runtime_error(fmt::format(
                "Unsupported RHEL CodeReady mirror baseline for EL{}",
                osinfo.getMajorVersion()));
    }
}

std::string defaultRHELBaseMirrorGPGKeyFor(const OS& osinfo)
{
    if (osinfo.getDistro() == OS::Distro::Ubuntu) {
        return "";
    }

    switch (osinfo.getPlatform()) {
        case OS::Platform::el8:
        case OS::Platform::el9:
            return "rhel/RPM-GPG-KEY-redhat-release";
        case OS::Platform::el10:
            return "";
        default:
            throw std::runtime_error(fmt::format(
                "Unsupported RHEL BaseOS mirror signing baseline for EL{}",
                osinfo.getMajorVersion()));
    }
}

std::string defaultRHELCodeReadyMirrorGPGKeyFor(const OS& osinfo)
{
    if (osinfo.getDistro() == OS::Distro::Ubuntu) {
        return "";
    }

    switch (osinfo.getPlatform()) {
        case OS::Platform::el8:
        case OS::Platform::el9:
            return "rhel/RPM-GPG-KEY-redhat-release";
        case OS::Platform::el10:
            return "";
        default:
            throw std::runtime_error(fmt::format(
                "Unsupported RHEL CodeReady mirror signing baseline for EL{}",
                osinfo.getMajorVersion()));
    }
}

TEST_CASE("RepoConfigParser")
{
#ifdef BUILD_TESTING
    REQUIRE(opencattus::functions::exists("repos/repos.conf"));
    auto conffile = RepoConfigParser::parseTest("repos/repos.conf");
    CHECK(conffile.files().size() > 0);
    CHECK(conffile.files().contains("epel.repo"));

    const auto epelOpt = conffile.find("epel");
    CHECK(epelOpt.has_value() == true);
    const auto& epel = epelOpt.value();
    CHECK(epel.mirror.repo == "epel/9/Everything/x86_64/");
    CHECK(epel.upstream.repo
        == "https://download.fedoraproject.org/pub/epel/9/Everything/"
           "x86_64/");
    const auto zabbixOpt = conffile.find("zabbix");
    REQUIRE(zabbixOpt.has_value() == true);
    CHECK(zabbixOpt->mirror.repo == "zabbix/zabbix/7.0/rhel/9/x86_64/");
    CHECK(zabbixOpt->upstream.repo
        == "https://repo.zabbix.com/zabbix/7.0/rhel/9/x86_64/");

    const auto el10Conf = RepoConfigParser::parseTest("repos/repos.conf",
        RepoConfigVars {
            .arch = "x86_64",
            .beegfsVersion = "beegfs_7.4.0",
            .ohpcVersion = "4",
            .osversion = "10.1",
            .releasever = "10",
            .xcatVersion = "latest",
            .zabbixVersion = "7.0",
            .ofedVersion = "latest",
            .ofedRepoTarget = "10",
            .cudaGPGKey = "CDF6BA43.pub",
        });
    const auto ohpcOpt = el10Conf.find("OpenHPC");
    REQUIRE(ohpcOpt.has_value() == true);
    CHECK(ohpcOpt->upstream.repo
        == "https://repos.openhpc.community/OpenHPC/4/EL_10/");
#endif
}

TEST_CASE("defaultDOCARepoTargetFor uses explicit EL baselines")
{
#ifdef BUILD_TESTING
    CHECK(defaultDOCARepoTargetFor(
              OS(models::OS::Distro::Rocky, OS::Platform::el8, 10),
              "latest-2.9-LTS")
        == "8.10");
    CHECK(defaultDOCARepoTargetFor(
              OS(models::OS::Distro::Rocky, OS::Platform::el9, 7),
              "latest-2.9-LTS")
        == "9.6");
    CHECK(defaultDOCARepoTargetFor(
              OS(models::OS::Distro::Rocky, OS::Platform::el9, 7),
              "latest-3.2-LTS")
        == "9");
    CHECK(defaultDOCARepoTargetFor(
              OS(models::OS::Distro::Rocky, OS::Platform::el9, 7), "latest")
        == "9");
    CHECK(defaultDOCARepoTargetFor(
              OS(models::OS::Distro::Rocky, OS::Platform::el9, 7),
              "latest-3.2-LTS")
        == "9");
    CHECK(defaultDOCARepoTargetFor(
              OS(models::OS::Distro::Rocky, OS::Platform::el10, 1), "latest")
        == "10");
    CHECK(defaultDOCARepoTargetFor(
              OS(models::OS::Distro::Rocky, OS::Platform::el10, 1),
              "latest-3.2-LTS")
        == "10");
    CHECK_THROWS_AS(defaultDOCARepoTargetFor(
                        OS(models::OS::Distro::Rocky, OS::Platform::el10, 1),
                        "latest-2.9-LTS"),
        std::runtime_error);
#endif
}

TEST_CASE("defaultZabbixVersionFor resolves latest LTS alias")
{
#ifdef BUILD_TESTING
    CHECK(defaultZabbixVersionFor("") == "7.0");
    CHECK(defaultZabbixVersionFor("latest-lts") == "7.0");
    CHECK(defaultZabbixVersionFor("lts") == "7.0");
    CHECK(defaultZabbixVersionFor("7.2") == "7.2");
#endif
}

TEST_CASE("defaultCUDAGPGKeyFor tracks NVIDIA CUDA repo keys by EL release")
{
#ifdef BUILD_TESTING
    CHECK(defaultCUDAGPGKeyFor(
              OS(models::OS::Distro::Rocky, OS::Platform::el8, 10))
        == "D42D0685.pub");
    CHECK(defaultCUDAGPGKeyFor(
              OS(models::OS::Distro::Rocky, OS::Platform::el9, 7))
        == "D42D0685.pub");
    CHECK(defaultCUDAGPGKeyFor(
              OS(models::OS::Distro::Rocky, OS::Platform::el10, 1))
        == "CDF6BA43.pub");
#endif
}

TEST_CASE("defaultRHELCodeReadyMirrorRepoFor follows the local mirror layout")
{
#ifdef BUILD_TESTING
    CHECK(defaultRHELBaseMirrorGPGKeyFor(
              OS(models::OS::Distro::RHEL, OS::Platform::el9, 7))
        == "rhel/RPM-GPG-KEY-redhat-release");
    CHECK(defaultRHELBaseMirrorGPGKeyFor(
        OS(models::OS::Distro::RHEL, OS::Platform::el10, 1))
            .empty());
    CHECK(defaultRHELCodeReadyMirrorRepoFor(
              OS(models::OS::Distro::RHEL, OS::Platform::el9, 7))
        == "rhel/codeready-builder-for-rhel-9-x86_64-rpms/");
    CHECK(defaultRHELCodeReadyMirrorGPGKeyFor(
              OS(models::OS::Distro::RHEL, OS::Platform::el9, 7))
        == "rhel/RPM-GPG-KEY-redhat-release");
    CHECK(defaultRHELCodeReadyMirrorRepoFor(
              OS(models::OS::Distro::RHEL, OS::Platform::el10, 1))
        == "rocky/linux/10/CRB/x86_64/os/");
    CHECK(defaultRHELCodeReadyMirrorGPGKeyFor(
        OS(models::OS::Distro::RHEL, OS::Platform::el10, 1))
            .empty());
#endif
}

TEST_CASE("RepoConfigParser emits explicit DOCA repo targets")
{
#ifdef BUILD_TESTING
    const auto el9Conf29 = RepoConfigParser::parseTest("repos/repos.conf",
        RepoConfigVars {
            .arch = "x86_64",
            .beegfsVersion = "beegfs_7.3.3",
            .ohpcVersion = "3",
            .osversion = "9.7",
            .releasever = "9",
            .xcatVersion = "latest",
            .zabbixVersion = "7.0",
            .ofedVersion = "latest-2.9-LTS",
            .ofedRepoTarget = "9.6",
            .cudaGPGKey = "D42D0685.pub",
        });
    const auto el9Doca29 = el9Conf29.find("doca");
    REQUIRE(el9Doca29.has_value() == true);
    CHECK(el9Doca29->upstream.repo
        == "https://linux.mellanox.com/public/repo/doca/latest-2.9-LTS/"
           "rhel9.6/x86_64/");
    REQUIRE(el9Doca29->upstream.gpgkey.has_value() == true);
    CHECK(el9Doca29->upstream.gpgkey.value()
        == "https://linux.mellanox.com/public/repo/doca/latest-2.9-LTS/"
           "rhel9.6/x86_64/GPG-KEY-Mellanox.pub");

    const auto el9Conf32 = RepoConfigParser::parseTest("repos/repos.conf",
        RepoConfigVars {
            .arch = "x86_64",
            .beegfsVersion = "beegfs_7.3.3",
            .ohpcVersion = "3",
            .osversion = "9.7",
            .releasever = "9",
            .xcatVersion = "latest",
            .zabbixVersion = "7.0",
            .ofedVersion = "latest-3.2-LTS",
            .ofedRepoTarget = "9",
            .cudaGPGKey = "D42D0685.pub",
        });
    const auto el9Doca32 = el9Conf32.find("doca");
    REQUIRE(el9Doca32.has_value() == true);
    CHECK(el9Doca32->upstream.repo
        == "https://linux.mellanox.com/public/repo/doca/latest-3.2-LTS/"
           "rhel9/x86_64/");
    REQUIRE(el9Doca32->upstream.gpgkey.has_value() == true);
    CHECK(el9Doca32->upstream.gpgkey.value()
        == "https://linux.mellanox.com/public/repo/doca/latest-3.2-LTS/"
           "rhel9/x86_64/GPG-KEY-Mellanox.pub");

    const auto el10Conf = RepoConfigParser::parseTest("repos/repos.conf",
        RepoConfigVars {
            .arch = "x86_64",
            .beegfsVersion = "beegfs_7.4.0",
            .ohpcVersion = "4",
            .osversion = "10.1",
            .releasever = "10",
            .xcatVersion = "latest",
            .zabbixVersion = "7.0",
            .ofedVersion = "latest",
            .ofedRepoTarget = "10",
            .cudaGPGKey = "CDF6BA43.pub",
        });
    const auto el10Doca = el10Conf.find("doca");
    REQUIRE(el10Doca.has_value() == true);
    CHECK(el10Doca->upstream.repo
        == "https://linux.mellanox.com/public/repo/doca/latest/rhel10/"
           "x86_64/");
#endif
}

TEST_CASE("RepoConfigParser emits CUDA repository URLs")
{
#ifdef BUILD_TESTING
    const auto el9Conf = RepoConfigParser::parseTest("repos/repos.conf",
        RepoConfigVars {
            .arch = "x86_64",
            .beegfsVersion = "beegfs_7.3.3",
            .ohpcVersion = "3",
            .osversion = "9.7",
            .releasever = "9",
            .xcatVersion = "latest",
            .zabbixVersion = "7.0",
            .ofedVersion = "latest-2.9-LTS",
            .ofedRepoTarget = "9.6",
            .cudaGPGKey = "D42D0685.pub",
        });
    const auto el9Cuda = el9Conf.find("cuda");
    REQUIRE(el9Cuda.has_value() == true);
    CHECK(el9Cuda->upstream.repo
        == "https://developer.download.nvidia.com/compute/cuda/repos/rhel9/"
           "x86_64/");
    REQUIRE(el9Cuda->upstream.gpgkey.has_value() == true);
    CHECK(el9Cuda->upstream.gpgkey.value()
        == "https://developer.download.nvidia.com/compute/cuda/repos/rhel9/"
           "x86_64/D42D0685.pub");

    const auto el9ModernLtsConf
        = RepoConfigParser::parseTest("repos/repos.conf",
            RepoConfigVars {
                .arch = "x86_64",
                .beegfsVersion = "beegfs_7.3.3",
                .ohpcVersion = "3",
                .osversion = "9.7",
                .releasever = "9",
                .xcatVersion = "latest",
                .zabbixVersion = "7.0",
                .ofedVersion = "latest-3.2-LTS",
                .ofedRepoTarget = "9",
                .cudaGPGKey = "D42D0685.pub",
            });
    const auto el9ModernLtsDoca = el9ModernLtsConf.find("doca");
    REQUIRE(el9ModernLtsDoca.has_value() == true);
    CHECK(el9ModernLtsDoca->upstream.repo
        == "https://linux.mellanox.com/public/repo/doca/latest-3.2-LTS/"
           "rhel9/x86_64/");
    REQUIRE(el9ModernLtsDoca->upstream.gpgkey.has_value() == true);
    CHECK(el9ModernLtsDoca->upstream.gpgkey.value()
        == "https://linux.mellanox.com/public/repo/doca/latest-3.2-LTS/"
           "rhel9/x86_64/GPG-KEY-Mellanox.pub");

    const auto el10Conf = RepoConfigParser::parseTest("repos/repos.conf",
        RepoConfigVars {
            .arch = "x86_64",
            .beegfsVersion = "beegfs_7.4.0",
            .ohpcVersion = "4",
            .osversion = "10.1",
            .releasever = "10",
            .xcatVersion = "latest",
            .zabbixVersion = "7.0",
            .ofedVersion = "latest",
            .ofedRepoTarget = "10",
            .cudaGPGKey = "CDF6BA43.pub",
        });
    const auto el10Cuda = el10Conf.find("cuda");
    REQUIRE(el10Cuda.has_value() == true);
    CHECK(el10Cuda->upstream.repo
        == "https://developer.download.nvidia.com/compute/cuda/repos/rhel10/"
           "x86_64/");
    REQUIRE(el10Cuda->upstream.gpgkey.has_value() == true);
    CHECK(el10Cuda->upstream.gpgkey.value()
        == "https://developer.download.nvidia.com/compute/cuda/repos/rhel10/"
           "x86_64/CDF6BA43.pub");
    const auto el10ModernLtsConf
        = RepoConfigParser::parseTest("repos/repos.conf",
            RepoConfigVars {
                .arch = "x86_64",
                .beegfsVersion = "beegfs_7.4.0",
                .ohpcVersion = "4",
                .osversion = "10.1",
                .releasever = "10",
                .xcatVersion = "latest",
                .zabbixVersion = "7.0",
                .ofedVersion = "latest-3.2-LTS",
                .ofedRepoTarget = "10",
                .cudaGPGKey = "CDF6BA43.pub",
            });
    const auto el10ModernLtsDoca = el10ModernLtsConf.find("doca");
    REQUIRE(el10ModernLtsDoca.has_value() == true);
    CHECK(el10ModernLtsDoca->upstream.repo
        == "https://linux.mellanox.com/public/repo/doca/latest-3.2-LTS/"
           "rhel10/x86_64/");
#endif
}

TEST_CASE("RepoConfigParser emits oneAPI and ZFS repository URLs")
{
#ifdef BUILD_TESTING
    const auto el9Conf = RepoConfigParser::parseTest("repos/repos.conf",
        RepoConfigVars {
            .arch = "x86_64",
            .beegfsVersion = "beegfs_7.3.3",
            .ohpcVersion = "3",
            .osversion = "9.7",
            .releasever = "9",
            .xcatVersion = "latest",
            .zabbixVersion = "7.0",
            .ofedVersion = "latest-2.9-LTS",
            .ofedRepoTarget = "9.6",
            .cudaGPGKey = "D42D0685.pub",
        });

    const auto oneApi = el9Conf.find("oneAPI");
    REQUIRE(oneApi.has_value() == true);
    CHECK(oneApi->upstream.repo == "https://yum.repos.intel.com/oneapi/");
    REQUIRE(oneApi->upstream.gpgkey.has_value() == true);
    CHECK(oneApi->upstream.gpgkey.value()
        == "https://yum.repos.intel.com/intel-gpg-keys/"
           "GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB");

    const auto zfs = el9Conf.find("zfs");
    REQUIRE(zfs.has_value() == true);
    CHECK(
        zfs->upstream.repo == "http://download.zfsonlinux.org/epel/9/x86_64/");
    CHECK(zfs->upstream.gpgkey.has_value() == false);
#endif
}

TEST_CASE("RepoConfigParser interpolates distro repository ids")
{
#ifdef BUILD_TESTING
    const auto el9OracleConf = RepoConfigParser::parseTest("repos/oracle.conf",
        RepoConfigVars {
            .arch = "x86_64",
            .beegfsVersion = "beegfs_7.3.3",
            .ohpcVersion = "3",
            .osversion = "9.7",
            .releasever = "9",
            .xcatVersion = "latest",
            .zabbixVersion = "7.0",
            .ofedVersion = "latest-2.9-LTS",
            .ofedRepoTarget = "9.6",
            .cudaGPGKey = "D42D0685.pub",
        });
    CHECK(el9OracleConf.find("OLAppStream").has_value() == true);
    CHECK(el9OracleConf.find("ol9_codeready_builder").has_value() == true);

    const auto el10RHELConf = RepoConfigParser::parseTest("repos/rhel.conf",
        RepoConfigVars {
            .arch = "x86_64",
            .beegfsVersion = "beegfs_7.4.0",
            .ohpcVersion = "4",
            .osversion = "10.1",
            .releasever = "10",
            .xcatVersion = "latest",
            .zabbixVersion = "7.0",
            .ofedVersion = "latest",
            .ofedRepoTarget = "10",
            .cudaGPGKey = "CDF6BA43.pub",
            .rhelBaseMirrorGPGKey = "",
            .rhelCodeReadyMirrorRepo = "rocky/linux/10/CRB/x86_64/os/",
            .rhelCodeReadyMirrorGPGKey = "",
        });
    const auto el10RHELAppStream = el10RHELConf.find("rhel-10-appstream");
    REQUIRE(el10RHELAppStream.has_value() == true);
    CHECK(el10RHELAppStream->mirror.repo
        == "rhel/rhel-10-for-x86_64-appstream-rpms/");
    CHECK(el10RHELAppStream->mirror.gpgkey.has_value() == false);
    const auto el10RHELBase = el10RHELConf.find("rhel-10-baseos");
    REQUIRE(el10RHELBase.has_value() == true);
    CHECK(el10RHELBase->mirror.repo == "rhel/rhel-10-for-x86_64-baseos-rpms/");
    CHECK(el10RHELBase->mirror.gpgkey.has_value() == false);
    const auto el10RHELCRB = el10RHELConf.find("rhel-10-codeready-builder");
    REQUIRE(el10RHELCRB.has_value() == true);
    CHECK(el10RHELCRB->mirror.repo == "rocky/linux/10/CRB/x86_64/os/");
    CHECK(el10RHELCRB->mirror.gpgkey.has_value() == false);

    const auto el10AlmaConf = RepoConfigParser::parseTest("repos/alma.conf",
        RepoConfigVars {
            .arch = "x86_64",
            .beegfsVersion = "beegfs_7.4.0",
            .ohpcVersion = "4",
            .osversion = "10.1",
            .releasever = "10",
            .xcatVersion = "latest",
            .zabbixVersion = "7.0",
            .ofedVersion = "latest",
            .ofedRepoTarget = "10",
            .cudaGPGKey = "CDF6BA43.pub",
        });
    CHECK(el10AlmaConf.find("AlmaLinuxAppStream").has_value() == true);
    CHECK(el10AlmaConf.find("AlmaLinuxCRB").has_value() == true);
#endif
}

TEST_CASE("RPMRepositoryParser tolerates repo files that omit gpgcheck")
{
#ifdef BUILD_TESTING
    const auto repoPath = std::filesystem::temp_directory_path()
        / "opencattus-lenovo-hpc-test.repo";
    opencattus::services::files::write(repoPath,
        "[lenovo-hpc]\n"
        "name=Lenovo packages for HPC\n"
        "baseurl=https://hpc.lenovo.com/yum/latest/el10/x86_64/\n"
        "enabled=1\n"
        "gpgkey=file:///etc/pki/rpm-gpg/RPM-GPG-KEY-LENOVO\n");

    std::map<std::string, std::shared_ptr<RPMRepository>> repos;
    REQUIRE_NOTHROW(RPMRepositoryParser::parse(repoPath, repos));
    REQUIRE(repos.contains("lenovo-hpc"));
    CHECK(repos.at("lenovo-hpc")->enabled() == true);
    CHECK(repos.at("lenovo-hpc")->gpgcheck() == true);
    CHECK(repos.at("lenovo-hpc")->gpgkey().value()
        == "file:///etc/pki/rpm-gpg/RPM-GPG-KEY-LENOVO");

    std::filesystem::remove(repoPath);
#endif
}

TEST_CASE("defaultOpenHPCVersionFor maps the supported EL releases")
{
    CHECK(defaultOpenHPCVersionFor(
              OS(models::OS::Distro::Rocky, OS::Platform::el8, 10))
        == "2");
    CHECK(defaultOpenHPCVersionFor(
              OS(models::OS::Distro::Rocky, OS::Platform::el9, 7))
        == "3");
    CHECK(defaultOpenHPCVersionFor(
              OS(models::OS::Distro::Rocky, OS::Platform::el10, 1))
        == "4");
    CHECK(defaultOpenHPCVersionFor(
              OS(models::OS::Distro::Ubuntu, OS::Platform::ubuntu24, 4))
        == "versatushpc-4");
}

// Installs and enable/disable RPM repositories
class RPMRepoManager final {
    static constexpr auto m_parser = RPMRepositoryParser();
    // Maps repo id to files
    std::map<std::string, std::shared_ptr<RPMRepositoryFile>> m_filesIdx;

public:
    static constexpr std::string_view basedir = "/etc/yum.repos.d/";

    // Installs a single .repo file
    void install(const std::filesystem::path& source)
    {
        const auto& dest = basedir / source.filename();
        const auto opts = opencattus::utils::singleton::options();

        // Do not copy the file to the basedir if it
        // is already there
        if (source != dest) {
            opencattus::functions::copyFile(source, dest);
        }

        if (opts->dryRun) {
            LOG_INFO("Dry Run: Would open {}", dest.string());
            return;
        }

        const auto& repofile
            = std::make_shared<RPMRepositoryFile>(RPMRepositoryFile(dest));
        LOG_ASSERT(repofile->repos().size() > 0, "BUG Loading file");
        for (auto& [repo, _] : repofile->repos()) {
            LOG_TRACE("{} loaded", repo);
            m_filesIdx.emplace(repo, repofile);
        }
    }

    // Install all .repo files inside a folder
    void install(std::filesystem::directory_iterator&& dirIter)
    {
        for (const auto& fil : std::move(dirIter)) {
            std::string fname = fil.path().filename().string();
            if (fname.ends_with(".repo")) {
                LOG_TRACE("Loading {}", fname);
                install(fil);
            }
        }
    }

    // Install all .repos files inside a folder
    void loadDir(const std::filesystem::path& path)
    {
        const auto opts = opencattus::utils::singleton::options();
        if (opts->dryRun) {
            LOG_INFO("Dry Run: Would open the directory {}", path.string());
            return;
        }

        install(std::filesystem::directory_iterator(path));
    }

    void loadBaseDir() { loadDir(basedir); }

    auto repo(const std::string& repoName)
    {
        try {
            auto repoFile = m_filesIdx.at(repoName);
            auto repoObj = repoFile->repo(repoName);
            return std::make_unique<const RPMRepository>(
                *repoObj); // copy to unique ptr
        } catch (const std::out_of_range& e) {
            auto repos = m_filesIdx
                | std::views::transform(
                    [](const auto& pair) { return pair.first; });
            auto msg
                = fmt::format("Cannot find repository {}, no such repository "
                              "loaded, repositories: available: {}",
                    repoName, fmt::join(repos, ","));
            throw std::runtime_error(msg);
        }
    }

    static std::vector<std::unique_ptr<const IRepository>> repoFile(
        const std::string& repoFileName)
    {
        try {
            auto path = fmt::format("{}/{}", basedir, repoFileName);
            auto repos = RPMRepositoryFile(path).repos()
                // We copy to const unique to express that these values cannot
                // be changed through this API.
                | std::views::transform([](auto&& pair) {
                      return std::make_unique<const RPMRepository>(
                          *pair.second);
                  })
                | opencattus::utils::ranges::to<
                    std::vector<std::unique_ptr<const IRepository>>>();
            return repos;
        } catch (const std::out_of_range& e) {
            throw std::runtime_error(
                fmt::format("No such repository file {}", repoFileName));
        }
    }

    // Enable/disable a repository by name
    void enable(const auto& repo, bool value)
    {
        if (value) {
            LOG_DEBUG("Enabling RPM repo {}", repo);
        } else {
            LOG_DEBUG("Disabling RPM repo {}", repo);
        }
        auto& repofile = m_filesIdx.at(repo);
        repofile->repo(repo)->enabled(value);
        repofile->save();
    }

    // Enable a repo but dot not save the repofile, (used internally)
    void enable(const auto& repo, auto& repofile, bool value)
    {
        LOG_DEBUG("{} RPM repo {}", value ? "Enabling" : "Disabling", repo);
        repofile->repo(repo)->enabled(value);
    }

    // Enable/disable multiple repositories by name
    void enable(const std::vector<std::string>& repos, bool value)
    {
        auto byIdPtr = [](const std::shared_ptr<RPMRepositoryFile>& rptr) {
            return std::hash<std::string> { }(rptr->path());
        };
        std::unordered_set<std::shared_ptr<RPMRepositoryFile>,
            decltype(byIdPtr)>
            toSave;
        for (const auto& repo : repos) {
            try {
                auto& rfile = m_filesIdx.at(repo);
                toSave.emplace(rfile);
                enable(repo, rfile, value);
            } catch (const std::out_of_range&) {
                opencattus::functions::abort(
                    "Trying to enable unknown repository {}, "
                    "failed because the repository was not found.",
                    repo);
            }
        }
        for (const auto& repoFil : toSave) {
            repoFil->save();
        }
    }

    // List repositories through a const unique pointer vector
    //
    // Rationale: IRepository type is to keep client code generic
    std::vector<std::unique_ptr<const IRepository>> repos()
    {
        // Function to iterate over map by id
        constexpr auto byId
            = [](auto& repo) { return std::hash<std::string> { }(repo.id()); };

        std::unordered_set<RPMRepository, decltype(byId)> output;
        for (auto& [_id1, repoFile] : m_filesIdx) {
            for (const auto& [_id2, repo] : repoFile->repos()) {
                output.emplace(*repo); // copy
            }
        }

        return output | std::views::transform([](auto&& repo) {
            return std::make_unique<const RPMRepository>(repo);
        })
            | opencattus::utils::ranges::to<
                std::vector<std::unique_ptr<const IRepository>>>();
    }
};

// Adpater for simplifying the conversion from RepoConfig
// to RPMRepository and RPMRepositoryFiles, may do HTTP requests
template <typename MChecker = DefaultMirrorExistenceChecker,
    typename UChecker = DefaultMirrorExistenceChecker>
struct RepoConfAdapter final {
    static RPMRepository fromConfig(const RepoConfig& config)
    {
        const RepoId& repoid = config.repoId;
        const MirrorRepo<MChecker> mirror {
            .paths = config.mirror,
        };
        const UpstreamRepo<UChecker> upstream {
            .paths = config.upstream,
        };
        return RepoAssembler::assemble<MChecker, UChecker>(
            repoid, mirror, upstream);
    };

    static RPMRepositoryFile fromConfigs(const std::string& filename,
        const std::vector<RepoConfig>& configs,
        const std::filesystem::path& basedir = RPMRepoManager::basedir)
    {
        const auto path = basedir / filename;
        std::map<std::string, std::shared_ptr<RPMRepository>> repos;
        for (const auto& config : configs) {
            repos.emplace(config.repoId.id,
                std::make_shared<RPMRepository>(fromConfig(config)));
        };
        return { path, repos };
    }

    // Convert RepoConfFile to a list of RPMRepository files using
    // the repoList
    static std::vector<RPMRepositoryFile> fromConfFile(
        const RepoConfFile& conffile, const std::vector<std::string>& repoList,
        const std::filesystem::path& basedir = RPMRepoManager::basedir)
    {
        std::vector<RPMRepositoryFile> output;
        for (const auto& [filename, configs] : conffile.files()) {
            if (!opencattus::functions::isIn(repoList, filename)) {
                continue;
            }

            LOG_INFO("Generating {}", filename);

            auto&& repofile = fromConfigs(filename, configs, basedir);
            output.emplace_back(repofile);
        }
        return output;
    }
};

TEST_CASE("RepoAdapter")
{
#ifdef BUILD_TESTING
    Options opts {
        .enableMirrors = true,
        .mirrorBaseUrl = "https://mirror.example.com",
    };
    opencattus::services::initializeSingletonsOptions(
        std::make_unique<const Options>(opts));
    // Log::init(5);

    const auto conffile = RepoConfigParser::parseTest("repos/repos.conf");
    const auto repofiles = RepoConfAdapter<TrueMirrorExistenceChecker,
        TrueMirrorExistenceChecker>::fromConfFile(conffile,
        conffile.filesnames());
    CHECK(repofiles.size() == conffile.files().size());

    const auto epelOpt = conffile.find("epel");
    REQUIRE(epelOpt.has_value() == true);
    const auto epelRepo = RepoConfAdapter<TrueMirrorExistenceChecker,
        TrueMirrorExistenceChecker>::fromConfig(epelOpt.value());
    REQUIRE(epelRepo.baseurl().has_value() == true);
    CHECK(epelRepo.baseurl().value().starts_with(opts.mirrorBaseUrl));
    REQUIRE(epelRepo.gpgkey().has_value() == true);
    CHECK(epelRepo.gpgkey().value().starts_with(opts.mirrorBaseUrl));

    const auto ohpcOpt = conffile.find("OpenHPC");
    REQUIRE(ohpcOpt.has_value() == true);
    const auto ohpcRepo = RepoConfAdapter<TrueMirrorExistenceChecker,
        TrueMirrorExistenceChecker>::fromConfig(ohpcOpt.value());
    REQUIRE(ohpcRepo.baseurl().has_value() == true);
    CHECK(ohpcRepo.baseurl().value().starts_with(opts.mirrorBaseUrl));

    const auto el10RHELConf = RepoConfigParser::parseTest("repos/rhel.conf",
        RepoConfigVars {
            .arch = "x86_64",
            .beegfsVersion = "beegfs_7.4.0",
            .ohpcVersion = "4",
            .osversion = "10.1",
            .releasever = "10",
            .xcatVersion = "latest",
            .zabbixVersion = "7.0",
            .ofedVersion = "latest",
            .ofedRepoTarget = "10",
            .cudaGPGKey = "CDF6BA43.pub",
            .rhelBaseMirrorGPGKey = "",
            .rhelCodeReadyMirrorRepo = "rocky/linux/10/CRB/x86_64/os/",
            .rhelCodeReadyMirrorGPGKey = "",
        });
    const auto rhelBaseOpt = el10RHELConf.find("rhel-10-baseos");
    REQUIRE(rhelBaseOpt.has_value() == true);
    const auto rhelBaseRepo = RepoConfAdapter<TrueMirrorExistenceChecker,
        TrueMirrorExistenceChecker>::fromConfig(rhelBaseOpt.value());
    REQUIRE(rhelBaseRepo.baseurl().has_value() == true);
    CHECK(rhelBaseRepo.baseurl().value()
        == "https://mirror.example.com/rhel/rhel-10-for-x86_64-baseos-rpms");
    CHECK(rhelBaseRepo.gpgcheck() == false);
    CHECK(rhelBaseRepo.gpgkey().has_value() == false);

    const auto rhelAppStreamOpt = el10RHELConf.find("rhel-10-appstream");
    REQUIRE(rhelAppStreamOpt.has_value() == true);
    const auto rhelAppStreamRepo = RepoConfAdapter<TrueMirrorExistenceChecker,
        TrueMirrorExistenceChecker>::fromConfig(rhelAppStreamOpt.value());
    REQUIRE(rhelAppStreamRepo.baseurl().has_value() == true);
    CHECK(rhelAppStreamRepo.baseurl().value()
        == "https://mirror.example.com/rhel/rhel-10-for-x86_64-appstream-rpms");
    CHECK(rhelAppStreamRepo.gpgcheck() == false);
    CHECK(rhelAppStreamRepo.gpgkey().has_value() == false);

    const auto rhelCRBOpt = el10RHELConf.find("rhel-10-codeready-builder");
    REQUIRE(rhelCRBOpt.has_value() == true);
    const auto rhelCRBRepo = RepoConfAdapter<TrueMirrorExistenceChecker,
        TrueMirrorExistenceChecker>::fromConfig(rhelCRBOpt.value());
    REQUIRE(rhelCRBRepo.baseurl().has_value() == true);
    CHECK(rhelCRBRepo.baseurl().value()
        == "https://mirror.example.com/rocky/linux/10/CRB/x86_64/os");
    CHECK(rhelCRBRepo.gpgcheck() == false);
    CHECK(rhelCRBRepo.gpgkey().has_value() == false);

    initializeSingletonsOptions(std::make_unique<const Options>(Options {
        .enableMirrors = false,
        .mirrorBaseUrl = opts.mirrorBaseUrl,
    }));
    const auto epelRepoUpstream = RepoConfAdapter<TrueMirrorExistenceChecker,
        TrueMirrorExistenceChecker>::fromConfig(epelOpt.value());
    REQUIRE(epelRepoUpstream.baseurl().has_value() == true);
    CHECK(
        epelOpt->upstream.repo.starts_with(epelRepoUpstream.baseurl().value()));
    REQUIRE(epelRepoUpstream.gpgkey().has_value() == true);
    CHECK(
        epelRepoUpstream.gpgkey().value() == epelOpt->upstream.gpgkey.value());
#endif
};

// Return the repository names to enable based on the osinfo
template <typename UseVaultService = RockyLinux> struct RepoNames {
    static std::string resolveCodeReadyBuilderName(const OS& osinfo)
    {
        auto distro = osinfo.getDistro();
        auto platform = osinfo.getPlatform();
        auto majorVersion = osinfo.getMajorVersion();
        std::string arch = opencattus::utils::enums::toString(osinfo.getArch());

        switch (distro) {
            case OS::Distro::AlmaLinux:
                switch (platform) {
                    case OS::Platform::el8:
                        return "AlmaLinuxPowerTools";
                    case OS::Platform::el9:
                    case OS::Platform::el10:
                        return "AlmaLinuxCRB";
                    default:
                        throw std::runtime_error("Unsupported platform");
                }
            case OS::Distro::Rocky:
                switch (platform) {
                    case OS::Platform::el8:
                        return "powertools";
                    case OS::Platform::el9:
                    case OS::Platform::el10:
                        return "crb";
                    default:
                        throw std::runtime_error("Unsupported platform");
                }
            case OS::Distro::RHEL:
                return fmt::format("codeready-builder-for-rhel-{}-{}-rpms",
                    majorVersion, arch);
            case OS::Distro::OL:
                return fmt::format("ol{}_codeready_builder", majorVersion);
            default:
                throw std::runtime_error("Unsupported distro");
        }
    }

    static std::vector<std::string> resolveReposNames(
        const OS& osinfo, const RepoConfFiles& conffiles)
    {
        auto distro = osinfo.getDistro();
        auto majorVersion = osinfo.getMajorVersion();
        auto output = std::vector<std::string>();
        const auto& distroRepos = conffiles.distroRepos;
        const auto& nonDistroRepos = conffiles.nonDistroRepos;
        const auto& addToOutput = [&output]<typename... T>(
                                      fmt::format_string<T...> fmt, T... args) {
            output.emplace_back(fmt::format(fmt::runtime(fmt), args...));
        };

        switch (distro) {
            case OS::Distro::AlmaLinux:
                addToOutput("AlmaLinuxAppStream");
                addToOutput("AlmaLinuxBaseOS");
                addToOutput("{}", resolveCodeReadyBuilderName(osinfo));
                break;
            case OS::Distro::Rocky: {
                addToOutput("appstream");
                addToOutput("baseos");
                addToOutput("{}", resolveCodeReadyBuilderName(osinfo));
                break;
            }
            case OS::Distro::RHEL:
                addToOutput("rhel-{releasever}-appstream",
                    fmt::arg("releasever", majorVersion));
                addToOutput("rhel-{releasever}-baseos",
                    fmt::arg("releasever", majorVersion));
                addToOutput("rhel-{releasever}-codeready-builder",
                    fmt::arg("releasever", majorVersion));
                break;
            case OS::Distro::OL:
                addToOutput("OLAppStream");
                addToOutput("OLBaseOS");
                addToOutput("ol{releasever}_codeready_builder",
                    fmt::arg("releasever", majorVersion));
                break;
            default:
                throw std::runtime_error("Unsupported distro");
        }
        addToOutput("epel");
        addToOutput("OpenHPC");
        addToOutput("OpenHPC-Updates");
        return output;
    }

    static std::vector<std::string> resolveReposNames(
        const OS& osinfo, const RepoConfigVars& vars)
    {
        const auto& conffiles = RepoConfigParser::load<UseVaultService>(
            repoConfigBasePath(), osinfo, vars);
        return resolveReposNames(osinfo, conffiles);
    }
};

TEST_CASE("RepoNames")
{
    struct ShouldUseVaultService final {
        static bool shouldUseVault(const OS& osinfo) { return false; }
    };
    const auto enabler = RepoNames<ShouldUseVaultService> { };
    const RepoConfigVars& vars = RepoConfigVars {
        .arch = "x86_64",
        .beegfsVersion = "beegfs_7.3.3",
        .ohpcVersion = "3",
        .osversion = "9.4",
        .releasever = "9",
        .xcatVersion = "latest",
        .zabbixVersion = "7.0",
        .ofedVersion = "latest-3.2-LTS",
        .ofedRepoTarget = "9",
        .cudaGPGKey = "D42D0685.pub",
    };
    const RepoConfigVars& varsEl8 = RepoConfigVars {
        .arch = "x86_64",
        .beegfsVersion = "beegfs_7.3.3",
        .ohpcVersion = "2",
        .osversion = "8.10",
        .releasever = "8",
        .xcatVersion = "latest",
        .zabbixVersion = "7.0",
        .ofedVersion = "latest-3.2-LTS",
        .ofedRepoTarget = "8",
        .cudaGPGKey = "D42D0685.pub",
    };

    // RHEL
    {
        const auto osinfo = OS(models::OS::Distro::RHEL, OS::Platform::el9, 5);
        const auto conffiles = RepoConfigParser::load<ShouldUseVaultService>(
            "repos/", osinfo, vars);
        const auto enabledRepos = enabler.resolveReposNames(osinfo, conffiles);
        // fmt::print("Repos: {}\n", fmt::join(enabledRepos, ","));
        CHECK(enabledRepos
            == std::vector<std::string> {
                "rhel-9-appstream",
                "rhel-9-baseos",
                "rhel-9-codeready-builder",
                "epel",
                "OpenHPC",
                "OpenHPC-Updates",
            });
    }

    // AlmaLinux
    {
        const auto osinfo
            = OS(models::OS::Distro::AlmaLinux, OS::Platform::el9, 5);
        const auto conffiles = RepoConfigParser::load<ShouldUseVaultService>(
            "repos/", osinfo, vars);
        const auto enabledRepos = enabler.resolveReposNames(osinfo, conffiles);
        CHECK(enabledRepos
            == std::vector<std::string> {
                "AlmaLinuxAppStream",
                "AlmaLinuxBaseOS",
                "AlmaLinuxCRB",
                "epel",
                "OpenHPC",
                "OpenHPC-Updates",
            });
    }

    // AlmaLinux EL8
    {
        const auto osinfo
            = OS(models::OS::Distro::AlmaLinux, OS::Platform::el8, 10);
        const auto conffiles = RepoConfigParser::load<ShouldUseVaultService>(
            "repos/", osinfo, varsEl8);
        const auto enabledRepos = enabler.resolveReposNames(osinfo, conffiles);
        CHECK(enabledRepos
            == std::vector<std::string> {
                "AlmaLinuxAppStream",
                "AlmaLinuxBaseOS",
                "AlmaLinuxPowerTools",
                "epel",
                "OpenHPC",
                "OpenHPC-Updates",
            });
    }
    // Rocky EL10
    {
        const auto osinfo
            = OS(models::OS::Distro::Rocky, OS::Platform::el10, 1);
        const auto el10Vars = RepoConfigVars {
            .arch = "x86_64",
            .beegfsVersion = "beegfs_7.4.0",
            .ohpcVersion = "4",
            .osversion = "10.1",
            .releasever = "10",
            .xcatVersion = "latest",
            .zabbixVersion = "7.0",
            .ofedVersion = "latest",
            .ofedRepoTarget = "10",
            .cudaGPGKey = "CDF6BA43.pub",
        };
        const auto conffiles = RepoConfigParser::load<ShouldUseVaultService>(
            "repos/", osinfo, el10Vars);
        const auto enabledRepos = enabler.resolveReposNames(osinfo, conffiles);
        CHECK(enabledRepos
            == std::vector<std::string> {
                "appstream",
                "baseos",
                "crb",
                "epel",
                "OpenHPC",
                "OpenHPC-Updates",
            });
    }

    // Rocky EL8
    {
        const auto osinfo
            = OS(models::OS::Distro::Rocky, OS::Platform::el8, 10);
        const auto conffiles = RepoConfigParser::load<ShouldUseVaultService>(
            "repos/", osinfo, varsEl8);
        const auto enabledRepos = enabler.resolveReposNames(osinfo, conffiles);
        CHECK(enabledRepos
            == std::vector<std::string> {
                "appstream",
                "baseos",
                "powertools",
                "epel",
                "OpenHPC",
                "OpenHPC-Updates",
            });
    }

    // Rocky
    {
        const auto osinfo = OS(models::OS::Distro::Rocky, OS::Platform::el9, 5);
        const auto conffiles = RepoConfigParser::load<ShouldUseVaultService>(
            "repos/", osinfo, vars);
        const auto enabledRepos = enabler.resolveReposNames(osinfo, conffiles);
        // fmt::print("Repos: {}", fmt::join(enabledRepos, ","));
        CHECK(enabledRepos
            == std::vector<std::string> {
                "appstream",
                "baseos",
                "crb",
                "epel",
                "OpenHPC",
                "OpenHPC-Updates",
            });
    }

    // OL
    {
        const auto osinfo = OS(models::OS::Distro::OL, OS::Platform::el9, 5);
        const auto conffiles = RepoConfigParser::load<ShouldUseVaultService>(
            "repos/", osinfo, vars);
        const auto enabledRepos = enabler.resolveReposNames(osinfo, conffiles);
        // fmt::print("Repos: {}", fmt::join(enabledRepos, ","));
        CHECK(enabledRepos
            == std::vector<std::string> {
                "OLAppStream",
                "OLBaseOS",
                "ol9_codeready_builder",
                "epel",
                "OpenHPC",
                "OpenHPC-Updates",
            });
    }
}

// Generate the repositories in the disk
template <typename MChecker = DefaultMirrorExistenceChecker,
    typename UChecker = DefaultMirrorExistenceChecker,
    typename ShouldUseVaultService = RockyLinux>
struct RepoGenerator final {
    static std::size_t generate(const RepoConfFiles& conffiles,
        const OS& osinfo, const std::filesystem::path& path)
    {
        const auto& distroRepos = conffiles.distroRepos.filesnames();
        const auto& nonDistroRepos = conffiles.nonDistroRepos.filesnames();
        std::vector<std::string> reposToGenerate;
        reposToGenerate.reserve(distroRepos.size() + nonDistroRepos.size());
        reposToGenerate.insert(
            reposToGenerate.end(), distroRepos.begin(), distroRepos.end());
        reposToGenerate.insert(reposToGenerate.end(), nonDistroRepos.begin(),
            nonDistroRepos.end());

        std::vector<RPMRepositoryFile> repofiles
            = RepoConfAdapter<MChecker, UChecker>::fromConfFile(
                conffiles.distroRepos, reposToGenerate, path);
        auto&& nonDistroRepositories
            = RepoConfAdapter<MChecker, UChecker>::fromConfFile(
                conffiles.nonDistroRepos, reposToGenerate, path);
        repofiles.reserve(
            repofiles.size() + conffiles.nonDistroRepos.filesnames().size());
        repofiles.insert(repofiles.end(),
            std::make_move_iterator(nonDistroRepositories.begin()),
            std::make_move_iterator(nonDistroRepositories.end()));

        for (const auto& repofile : repofiles) {
            repofile.save();
        }

        return repofiles.size();
    }

    static std::size_t generate(const OS& osinfo, const RepoConfigVars& vars)
    {
        const auto conffiles = RepoConfigParser::load<ShouldUseVaultService>(
            repoConfigBasePath(), osinfo, vars);
        return generate(conffiles, osinfo, RPMRepoManager::basedir);
    }
};

TEST_CASE("RepoGenerator")
{
    auto opts = Options {
        .enableMirrors = true,
        .mirrorBaseUrl = "https://mirror.example.com",
    };
    const auto osinfo = OS(models::OS::Distro::Rocky, OS::Platform::el9, 5);
    struct ShouldUseVaultService final {
        static bool shouldUseVault(const OS& osinfo) { return false; }
    };
    const RepoConfigVars& vars = RepoConfigVars {
        .arch = "x86_64",
        .beegfsVersion = "beegfs_7.3.3",
        .ohpcVersion = "3",
        .osversion = "9.4",
        .releasever = "9",
        .xcatVersion = "latest",
        .zabbixVersion = "7.0",
        .ofedVersion = "latest-3.2-LTS",
        .ofedRepoTarget = "9",
        .cudaGPGKey = "D42D0685.pub",
    };
    opencattus::Singleton<const Options>::init(
        std::make_unique<const Options>(opts));
    const std::string_view upstreamPath = "test/output/repos/upstream";
    const std::string_view mirrorPath = "test/output/repos/mirror";
    const std::string_view airgapPath = "test/output/repos/airgap";
    const auto conffiles
        = RepoConfigParser::load<ShouldUseVaultService>("repos/", osinfo, vars);

    // Clean up before start
    for (const auto& path : { upstreamPath, mirrorPath, airgapPath }) {
        opencattus::functions::removeFilesWithExtension(path, ".repo");
    }

    const auto generator = RepoGenerator<FalseMirrorExistenceChecker, // mirror
        TrueMirrorExistenceChecker // upstream
        >();
    const auto generatedCount1
        = generator.generate(conffiles, osinfo, upstreamPath);
    CHECK(generatedCount1 == 19);

    const auto generatedCount2
        = generator.generate(conffiles, osinfo, upstreamPath);
    CHECK(generatedCount2 == 19);

    // Generate the other files so we can look at them
    const auto generatorMirror
        = RepoGenerator<TrueMirrorExistenceChecker, // mirror
            TrueMirrorExistenceChecker // upstream
            >();
    generatorMirror.generate(conffiles, osinfo, upstreamPath);
    CHECK(opencattus::services::files::read(
        std::filesystem::path(upstreamPath) / "epel.repo")
            .contains("https://mirror.example.com/epel/9/Everything/x86_64"));
    generatorMirror.generate(conffiles, osinfo, mirrorPath);
    opts.mirrorBaseUrl = "file:///var/run/repos";
    opencattus::Singleton<const Options>::init(
        std::make_unique<const Options>(opts));
    generatorMirror.generate(conffiles, osinfo, airgapPath);
};

TEST_SUITE("opencattus::services::repos [slow]")
{
    // RH CDN requires a certificate that only exists in RHEL machines
    // because of this the repostiories gives 403 and SSL errors. I'm skipping
    // them for now.
    constexpr auto blacklistedFiles = {
        "rhel.repo",
    };

    // Repositories from these files are not checked against Versatus Mirrors
    constexpr auto blacklistedMirrorFiles = {
        "almalinux.repo",
        "cuda.repo",
        "nvidia.repo",
        "influxdata.repo",
        "mlnx-doca.repo",
        "rocky.repo",
        "rocky-addons.repo",
        "rocky-extras.repo",
        "rocky-devel.repo",
    };

    TEST_CASE("[slow] repo.conf urls")
    {
        // This test case issues HTTP requests for each repository
        // URL in repos.conf (and siblings) and fail if the
        // HTTP Status in the response is not 200. So this test
        // will start fail if any repostiory URL changes
        // (which would break the instalation). It is very slow to
        // run so it is not inteded to run frequently, but otherwise
        // as a semi-automated way to validate the repositories URLs
        // in the repos.conf. use -tce="*slow*" to skip it.
#ifdef BUILD_TESTING
        using namespace opencattus::services;
        opencattus::services::initializeSingletonsOptions(
            std::make_unique<const Options>(Options { }));
        const auto repos = std::filesystem::path("./repos");
        REQUIRE(opencattus::functions::exists(repos / "repos.conf"));
        const auto confs
            = opencattus::functions::getFilesByExtension(repos, ".conf");
        REQUIRE(confs.size() > 0);
        using fmt::print;
        for (const auto& configStr : confs) {
            RepoConfFile output
                = RepoConfigParser::parseTest(repos / configStr);
            for (const auto& [repofile, configs] : output.files()) {
                for (const auto& config : configs) {
                    if (opencattus::functions::isIn(
                            blacklistedFiles, config.repoId.filename)) {

                        continue;
                    }

                    print("Checking {} {} {}\n", config.repoId.filename,
                        config.repoId.id, config.upstream.repo);

                    REQUIRE(opencattus::functions::getHttpStatus(
                                config.upstream.repo + "repodata/repomd.xml")
                        == "200");

                    if (config.upstream.gpgkey) {
                        REQUIRE(opencattus::functions::getHttpStatus(
                                    config.upstream.gpgkey.value())
                            == "200");
                    }

                    if (opencattus::functions::isIn(
                            blacklistedMirrorFiles, config.repoId.filename)
                        || config.mirror.repo.empty()) {
                        continue;
                    }

                    print("Checking {} {} {}\n", config.repoId.filename,
                        config.repoId.id,
                        "https://mirror.versatushpc.com.br/"
                            + config.mirror.repo + "repodata/repomd.xml");

                    REQUIRE(opencattus::functions::getHttpStatus(
                                "https://mirror.versatushpc.com.br/"
                                + config.mirror.repo + "repodata/repomd.xml")
                        == "200");

                    if (config.mirror.gpgkey) {
                        REQUIRE(opencattus::functions::getHttpStatus(
                                    "https://mirror.versatushpc.com.br/"
                                    + config.mirror.gpgkey.value())
                            == "200");
                    }
                }
            }
        }
#endif
    }
}
}; // namespace opencattus::services::repos {

namespace opencattus::services::repos {

// Hidden implementation
struct RepoManager::Impl {
    RPMRepoManager rpm;
    // Add debian repo manager here when the day arrives
};

RepoManager::~RepoManager() = default;

RepoManager::RepoManager()
    : m_impl(std::make_unique<RepoManager::Impl>())
{
}

auto buildRepoConfigVars(const OS& osinfo, std::string_view ofedVersion)
    -> RepoConfigVars
{
    const auto opts = opencattus::utils::singleton::options();
    const auto resolvedOfedVersion = ofedVersion.empty()
        ? std::string("latest")
        : std::string(ofedVersion);

    return RepoConfigVars {
        .arch = opencattus::utils::enums::toString(osinfo.getArch()),
        .beegfsVersion = opts->beegfsVersion,
        .ohpcVersion = defaultOpenHPCVersionFor(osinfo),
        .osversion = osinfo.getVersion(),
        .releasever = fmt::format("{}", osinfo.getMajorVersion()),
        .xcatVersion = opts->xcatVersion,
        .zabbixVersion = defaultZabbixVersionFor(opts->zabbixVersion),
        .ofedVersion = resolvedOfedVersion,
        .ofedRepoTarget = defaultDOCARepoTargetFor(osinfo, resolvedOfedVersion),
        .cudaGPGKey = defaultCUDAGPGKeyFor(osinfo),
        .rhelBaseMirrorGPGKey = defaultRHELBaseMirrorGPGKeyFor(osinfo),
        .rhelCodeReadyMirrorRepo = defaultRHELCodeReadyMirrorRepoFor(osinfo),
        .rhelCodeReadyMirrorGPGKey
        = defaultRHELCodeReadyMirrorGPGKeyFor(osinfo),
    };
}

std::vector<std::string> expandSelectedRepositoryIds(
    const std::vector<std::string>& repositoryIds)
{
    static const auto dependencies
        = std::unordered_map<std::string, std::vector<std::string>> {
              { "beegfs", { "grafana", "influxdata" } },
          };

    std::vector<std::string> expanded;
    expanded.reserve(repositoryIds.size() + 2);
    auto seen = std::unordered_set<std::string> { };

    const auto append
        = [&](const std::string& repoId, auto&& appendSelf) -> void {
        if (!seen.insert(repoId).second) {
            return;
        }

        expanded.emplace_back(repoId);
        if (const auto it = dependencies.find(repoId);
            it != dependencies.end()) {
            for (const auto& dependency : it->second) {
                appendSelf(dependency, appendSelf);
            }
        }
    };

    for (const auto& repositoryId : repositoryIds) {
        append(repositoryId, append);
    }

    return expanded;
}

std::vector<RepoManager::RepositorySelection>
RepoManager::defaultRepositoriesFor(const OS& osinfo,
    std::string_view ofedVersion,
    const std::optional<std::vector<std::string>>& enabledRepositories,
    const std::optional<std::vector<std::string>>& enabledOpenHPCBundles)
{
    if (osinfo.getDistro() == OS::Distro::Ubuntu) {
        static_cast<void>(ofedVersion);
        static_cast<void>(enabledRepositories);
        static_cast<void>(enabledOpenHPCBundles);
        return {
            { .id = "ubuntu-main",
                .name = "Ubuntu 24.04 main, restricted, universe, multiverse",
                .enabled = true },
            { .id = "ubuntu-updates",
                .name = "Ubuntu 24.04 updates",
                .enabled = true },
            { .id = "ubuntu-security",
                .name = "Ubuntu 24.04 security",
                .enabled = true },
            { .id = "OpenHPC",
                .name = "VersatusHPC OpenHPC 4.x for Ubuntu 24.04",
                .enabled = true },
        };
    }

    struct NoVaultLookup final {
        static bool shouldUseVault(const OS& osinfo)
        {
            static_cast<void>(osinfo);
            return false;
        }
    };

    const auto vars = buildRepoConfigVars(osinfo, ofedVersion);
    const auto conffiles = RepoConfigParser::load<NoVaultLookup>(
        repoConfigBasePath(), osinfo, vars);
    auto enabledRepoIds = RepoNames<>::resolveReposNames(osinfo, conffiles);
    if (enabledRepositories.has_value()) {
        const auto expandedSelectedRepositories
            = expandSelectedRepositoryIds(enabledRepositories.value());
        enabledRepoIds.insert(enabledRepoIds.end(),
            expandedSelectedRepositories.begin(),
            expandedSelectedRepositories.end());
    }
    if (enabledOpenHPCBundles.has_value()
        && std::ranges::find(enabledOpenHPCBundles.value(), "intel-oneapi")
            != enabledOpenHPCBundles->end()) {
        enabledRepoIds.emplace_back("oneAPI");
    }
    const auto enabledSet = std::unordered_set<std::string>(
        enabledRepoIds.begin(), enabledRepoIds.end());
    std::map<std::string, RepositorySelection> selections;

    const auto appendSelections = [&](const RepoConfFile& confFile) {
        for (const auto& [filename, configs] : confFile.files()) {
            static_cast<void>(filename);
            for (const auto& config : configs) {
                selections.insert_or_assign(config.repoId.id,
                    RepositorySelection {
                        .id = config.repoId.id,
                        .name = config.repoId.name,
                        .enabled = enabledSet.contains(config.repoId.id),
                    });
            }
        }
    };

    appendSelections(conffiles.distroRepos);
    appendSelections(conffiles.nonDistroRepos);

    std::vector<RepositorySelection> output;
    output.reserve(selections.size());
    for (const auto& [id, selection] : selections) {
        static_cast<void>(id);
        output.emplace_back(selection);
    }

    return output;
}

TEST_CASE("expandSelectedRepositoryIds enables BeeGFS monitoring dependencies")
{
    CHECK(expandSelectedRepositoryIds({ "beegfs" })
        == std::vector<std::string> { "beegfs", "grafana", "influxdata" });
}

TEST_CASE("defaultRepositoriesFor keeps mandatory repositories enabled when "
          "optional repositories are selected")
{
    opencattus::services::initializeSingletonsOptions(
        std::make_unique<const Options>(Options { }));
    const auto osinfo
        = OS(models::OS::Distro::Rocky, OS::Platform::el9, 6, OS::Arch::x86_64);
    const auto selections = RepoManager::defaultRepositoriesFor(
        osinfo, "latest", std::vector<std::string> { "cuda", "beegfs" });

    const auto enabled = [&selections](std::string_view repoId) {
        const auto it = std::ranges::find_if(selections,
            [repoId](const auto& selection) { return selection.id == repoId; });
        REQUIRE(it != selections.end());
        return it->enabled;
    };

    CHECK(enabled("appstream"));
    CHECK(enabled("baseos"));
    CHECK(enabled("crb"));
    CHECK(enabled("epel"));
    CHECK(enabled("OpenHPC"));
    CHECK(enabled("OpenHPC-Updates"));
    CHECK(enabled("cuda"));
    CHECK(enabled("beegfs"));
    CHECK(enabled("grafana"));
    CHECK(enabled("influxdata"));
}

TEST_CASE("defaultRepositoriesFor enables oneAPI when the Intel OpenHPC "
          "bundle is selected")
{
    opencattus::services::initializeSingletonsOptions(
        std::make_unique<const Options>(Options { }));
    const auto osinfo = OS(
        models::OS::Distro::Rocky, OS::Platform::el10, 1, OS::Arch::x86_64);
    const auto selections = RepoManager::defaultRepositoriesFor(osinfo,
        "latest", std::nullopt, std::vector<std::string> { "intel-oneapi" });

    const auto it = std::ranges::find_if(selections,
        [](const auto& selection) { return selection.id == "oneAPI"; });
    REQUIRE(it != selections.end());
    CHECK(it->enabled);
}

TEST_CASE("defaultRepositoriesFor exposes Ubuntu 24.04 mandatory repositories")
{
    opencattus::services::initializeSingletonsOptions(
        std::make_unique<const Options>(Options { }));
    const auto osinfo = OS(models::OS::Distro::Ubuntu, OS::Platform::ubuntu24,
        4, OS::Arch::x86_64);
    const auto selections = RepoManager::defaultRepositoriesFor(
        osinfo, "latest", std::nullopt, std::nullopt);

    CHECK(selections.size() == 4);
    CHECK(std::ranges::all_of(
        selections, [](const auto& selection) { return selection.enabled; }));
    CHECK(std::ranges::any_of(selections,
        [](const auto& selection) { return selection.id == "OpenHPC"; }));
}

inline void RPMRepository::valid() const
{
    auto isValid = (!id().empty() && !name().empty()
        && (!uri().has_value() || !uri().value().empty())
        && (!source().empty()));
    LOG_ASSERT(isValid, "Invalid RPM Repository");
}

struct RPMRepositoryGenerator {
    static void generate(const RepoConfigVars& vars,
        const std::filesystem::path& backupPath
        = "/opt/opencattus/backup/etc/yum.repos.d/",
        const std::filesystem::path& sourcePath = "/etc/yum.repos.d")
    {
        opencattus::functions::backupFilesByExtension(
            wrappers::DestinationPath(backupPath),
            wrappers::SourcePath(sourcePath), wrappers::Extension(".repo"));
        LOG_DEBUG("Generating the repository files");
        const auto cluster = opencattus::Singleton<models::Cluster>::get();
        const auto& osinfo = cluster->getComputeNodeOS();
        RepoGenerator<>::generate(osinfo, vars);
    }
};

std::string ubuntuOpenHpcRepositoryUrl(const OS& osinfo)
{
    switch (osinfo.getPlatform()) {
        case OS::Platform::ubuntu24:
            return "https://repos.versatushpc.com.br/openhpc/"
                   "versatushpc-4/Ubuntu_24.04/";
        default:
            throw std::runtime_error(fmt::format(
                "Unsupported Ubuntu OpenHPC repository baseline for {}",
                osinfo.getVersion()));
    }
}

TEST_CASE("ubuntuOpenHpcRepositoryUrl uses the VersatusHPC Noble fork")
{
    const auto osinfo = OS(models::OS::Distro::Ubuntu, OS::Platform::ubuntu24,
        4, OS::Arch::x86_64);

    CHECK(ubuntuOpenHpcRepositoryUrl(osinfo)
        == "https://repos.versatushpc.com.br/openhpc/versatushpc-4/"
           "Ubuntu_24.04/");
}

void initializeDebianHeadnodeRepositories(const OS& osinfo)
{
    if (osinfo.getDistro() != OS::Distro::Ubuntu) {
        throw std::logic_error(
            "Debian repository initialization is only implemented for Ubuntu");
    }

    // The VersatusHPC Ubuntu OpenHPC fork publishes a signed Release file, but
    // the public key is not published next to the repository yet. Keep this
    // explicit so apt can consume the repo while the repository signing path is
    // finished.
    runner::shell::fmt(R"(
DEBIAN_FRONTEND=noninteractive apt update
DEBIAN_FRONTEND=noninteractive apt install -y ca-certificates
install -d /etc/apt/sources.list.d
cat > /etc/apt/sources.list.d/opencattus-openhpc.list <<'EOF'
deb [trusted=yes] {openhpcUrl} ./
EOF
DEBIAN_FRONTEND=noninteractive apt update
)",
        fmt::arg("openhpcUrl", ubuntuOpenHpcRepositoryUrl(osinfo)));
}

void RepoManager::initializeDefaultRepositories()
{
    auto opts = opencattus::utils::singleton::options();
    if (opts->dryRun) {
        LOG_WARN("Dry Run: Skipping RepoManager initialization");
        return;
    }
    LOG_INFO("RepoManager initialization");
    auto cluster = opencattus::Singleton<models::Cluster>::get();
    const auto& computeOS = cluster->getComputeNodeOS();
    const auto& headnodeOS = cluster->getHeadnode().getOS();
    const auto& osinfo = computeOS.getPackageType() == OS::PackageType::DEB
            && headnodeOS.getPackageType() == OS::PackageType::RPM
        ? headnodeOS
        : computeOS;

    if (computeOS.getPackageType() == OS::PackageType::DEB
        && headnodeOS.getPackageType() == OS::PackageType::RPM) {
        LOG_WARN("Compute nodes use DEB repositories, but the head node uses "
                 "RPM. Initializing RPM repositories for head-node packages; "
                 "xCAT will attach Ubuntu APT repositories directly to the "
                 "compute image.");
    }

    const auto ofedVersion = cluster->getOFED().has_value()
        ? cluster->getOFED()->getVersion()
        : std::string("latest");
    const auto vars = buildRepoConfigVars(osinfo, ofedVersion);
    const auto repositories = defaultRepositoriesFor(osinfo, ofedVersion,
        cluster->getEnabledRepositories(), cluster->getEnabledOpenHPCBundles());

    std::vector<std::string> managedRepoIds;
    managedRepoIds.reserve(repositories.size());
    std::vector<std::string> enabledRepoIds;
    for (const auto& repository : repositories) {
        managedRepoIds.emplace_back(repository.id);
        if (repository.enabled) {
            enabledRepoIds.emplace_back(repository.id);
        }
    }

    switch (osinfo.getPackageType()) {
        case OS::PackageType::RPM: {
            // Generate the repository files
            RPMRepositoryGenerator::generate(vars);
            // Load the base directory, /etc/yum.repos.d/*.repo files
            m_impl->rpm.loadBaseDir();
            if (!managedRepoIds.empty()) {
                m_impl->rpm.enable(managedRepoIds, false);
            }
            if (!enabledRepoIds.empty()) {
                m_impl->rpm.enable(enabledRepoIds, true);
            }

            LOG_INFO("Enabling dnf keepcache option, use `dnf config-manager "
                     "--save --setopt=keepcache=False` to disable it")
            runner::shell::cmd("grep -q '^keepcache=' /etc/dnf/dnf.conf || dnf "
                               "config-manager --save --setopt=keepcache=True");
        } break;
        case OS::PackageType::DEB:
            initializeDebianHeadnodeRepositories(osinfo);
            break;
    }
}

void RepoManager::enable(const std::string& repoid)
{
    const auto opts = opencattus::utils::singleton::options();
    if (opts->dryRun) {
        LOG_INFO("Dry Run: Would enable repository {}", repoid);
        return;
    }
    auto osinfo
        = opencattus::Singleton<models::Cluster>::get()->getHeadnode().getOS();

    switch (osinfo.getPackageType()) {
        case OS::PackageType::RPM:
            m_impl->rpm.enable(repoid, true);
            break;
        default:
            throw std::logic_error("Not implemented");
    }
}

void RepoManager::enable(const std::vector<std::string>& repos)
{
    const auto opts = opencattus::utils::singleton::options();
    if (opts->dryRun) {
        LOG_WARN(
            "Dry Run: Would enable these repos: {}", fmt::join(repos, ","));
        return;
    }
    auto osinfo
        = opencattus::Singleton<models::Cluster>::get()->getHeadnode().getOS();
    switch (osinfo.getPackageType()) {
        case OS::PackageType::RPM:
            m_impl->rpm.enable(repos, true);
            break;
        default:

            throw std::logic_error("Not implemented");
    }
}

void RepoManager::disable(const std::string& repoid)
{
    const auto opts = opencattus::utils::singleton::options();
    if (opts->dryRun) {
        LOG_INFO("Dry Run: Would enable repository {}", repoid);
        return;
    }

    auto osinfo
        = opencattus::Singleton<models::Cluster>::get()->getHeadnode().getOS();
    try {
        switch (osinfo.getPackageType()) {
            case OS::PackageType::RPM:
                m_impl->rpm.enable(repoid, false);
                break;
            default:
                throw std::logic_error("Not implemented");
        }
    } catch (const std::out_of_range&) {
        LOG_ERROR("Trying to disable unknown repository {}, "
                  "failed because the repository was not found.",
            repoid);
    }
}

void RepoManager::disable(const std::vector<std::string>& repos)
{
    const auto opts = opencattus::utils::singleton::options();
    if (opts->dryRun) {
        LOG_INFO("Dry Run: Would enable repository {}", fmt::join(repos, ","));
        return;
    }

    auto osinfo
        = opencattus::Singleton<models::Cluster>::get()->getHeadnode().getOS();
    try {
        switch (osinfo.getPackageType()) {
            case OS::PackageType::RPM:
                m_impl->rpm.enable(repos, false);
                break;
            default:
                throw std::logic_error("Not implemented");
        }
    } catch (const std::out_of_range&) {
        LOG_ERROR("Trying to disable unknown repository {}, "
                  "failed because the repository was not found.",
            fmt::join(repos, ","));
    }
}

void RepoManager::install(const std::filesystem::path& path)
{
    LOG_ASSERT(
        path.is_absolute(), "RepoManager::install called with relative path");
    LOG_ASSERT(
        path.has_filename(), "RepoManager::install called with a directory?");
    LOG_INFO("Installing repository {}", path.string());

    auto osinfo
        = opencattus::Singleton<models::Cluster>::get()->getHeadnode().getOS();
    switch (osinfo.getPackageType()) {
        case OS::PackageType::RPM:
            m_impl->rpm.install(path);
            break;
        default:
            throw std::logic_error("Not implemented");
    }
}

void RepoManager::install(const std::vector<std::filesystem::path>& paths)
{
    for (const auto& repo : paths) {
        install(repo);
    }
}

// Return a vector of repositories.
//
// Pay attention that the returned value is an abstract type IRepository. This
// is to keep client code agostic to the repository implementations. They
// must dynamic dispatch over the shared pointer in order to treat the
// repository abstractly.
std::vector<std::unique_ptr<const IRepository>> RepoManager::listRepos() const
{
    auto osinfo
        = opencattus::Singleton<models::Cluster>::get()->getHeadnode().getOS();
    switch (osinfo.getPackageType()) {
        case OS::PackageType::RPM:
            return m_impl->rpm.repos();
            break;
        default:
            throw std::logic_error("Not implemented");
    }
}

std::unique_ptr<const IRepository> RepoManager::repo(
    const std::string& repo) const
{
    auto osinfo
        = opencattus::Singleton<models::Cluster>::get()->getHeadnode().getOS();
    switch (osinfo.getPackageType()) {
        case OS::PackageType::RPM:
            return static_cast<std::unique_ptr<const IRepository>>(
                m_impl->rpm.repo(repo));
            break;
        default:
            throw std::logic_error("Not implemented");
    }
}

std::vector<std::unique_ptr<const IRepository>> RepoManager::repoFile(
    const std::string& repo) const
{
    auto osinfo
        = opencattus::Singleton<models::Cluster>::get()->getHeadnode().getOS();
    switch (osinfo.getPackageType()) {
        case OS::PackageType::RPM:
            return m_impl->rpm.repoFile(repo);
            break;
        default:
            throw std::logic_error("Not implemented");
    }
}

TEST_SUITE_END();

}; // namespace opencattus::services::repos
