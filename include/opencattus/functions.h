#ifndef OPENCATTUS_FUNCTIONS_H_
#define OPENCATTUS_FUNCTIONS_H_

#include <algorithm>
#include <filesystem>
#include <opencattus/models/cluster.h>
#include <opencattus/patterns/singleton.h>
#include <opencattus/patterns/wrapper.h>
#include <opencattus/services/log.h>
#include <opencattus/services/options.h>
#include <opencattus/utils/enums.h>
#include <opencattus/utils/string.h>
#include <string>

#ifdef BUILD_TESTING
#include <doctest/doctest.h>
#else
#define DOCTEST_CONFIG_DISABLE
#include <doctest/doctest.h>
#endif

#include <boost/asio.hpp>
#include <opencattus/services/runner.h>
#include <type_traits>

// @TODO Split these functions into multiple utils subnamespaces like strings
//   and enums
namespace opencattus::functions {
// Globals, intialized by the command line parser
template <typename B, typename T, typename... Args>
constexpr std::unique_ptr<B> makeUniqueDerived(Args... args)
{
    return static_cast<std::unique_ptr<B>>(std::make_unique<T>(args...));
}

// @FIXME: File utilities functions should live in services::files namespace

using models::OS;
using services::IRunner;

/* shell execution */

/**
 * @brief Retrieves the value of an environment variable.
 *
 * @param variable The name of the environment variable.
 * @return The value of the environment variable.
 */
std::string getEnvironmentVariable(const std::string&);

/* conf manipulation functions */

/**
 * @brief Reads a configuration value from a file.
 *
 * @param filename The name of the file to read from.
 * @return The configuration value as a string.
 */
std::string readConfig(const std::string&);

/**
 * @brief Writes a configuration value to a file.
 *
 * @param filename The name of the file to write to.
 */
void writeConfig(const std::string&);

/* helper function */

/**
 * @brief Creates an empty file, analogous to the `touch` command
 */
void touchFile(const std::filesystem::path& path);

/**
 * @brief Creates a directory at the specified path.
 *
 * @param path The path where the directory should be created.
 */
void createDirectory(const std::filesystem::path& path);

/**
 * @brief Removes a file.
 *
 * @param filename The name of the file to remove.
 */
void removeFile(std::string_view filename);

/**
 * @brief Creates a backup of a file.
 *
 * @param filename The name of the file to backup.
 */
void backupFile(std::string_view filename);

/**
 * @brief Changes a value in a configuration file.
 *
 * @param filename The name of the configuration file.
 * @param key The key of the value to change.
 * @param value The new value to set.
 */
void changeValueInConfigurationFile(
    const std::string&, const std::string&, std::string_view);

/**
 * @brief Adds a string to a file.
 *
 * @param filename The name of the file to add the string to.
 * @param string The string to add to the file.
 */
void addStringToFile(std::string_view filename, std::string_view string);
std::string getCurrentTimestamp();
std::string findAndReplace(const std::string_view& source,
    const std::string_view& find, const std::string_view& replace);

/**
 * @brief Copies a file, skip copying if it exists
 *
 * @param source The source file to copy.
 * @param destination The path where the source file will be copied.
 */
void copyFile(std::filesystem::path source, std::filesystem::path destination);

/**
 * @brief Create a file in path with the contents of data
 *
 * @param path The source file to install.
 * @param data The contents of the file to install
 */
void installFile(const std::filesystem::path& path, std::istream& data);
void installFile(const std::filesystem::path& path, std::string&& data);

bool exists(const std::filesystem::path& path);

struct HTTPRepo {
    std::filesystem::path directory;
    std::string name;
    std::string url;
};

HTTPRepo createHTTPRepo(const std::string_view repoName);

void backupFilesByExtension(const wrappers::DestinationPath& backupPath,
    const wrappers::SourcePath& sourcePath,
    const wrappers::Extension& extension);

/**
 * @brief Returns true if [val] is in [container]
 */
inline bool isIn(const auto& container, const auto& val)
{
    return std::find(container.begin(), container.end(), val)
        != container.end();
}

TEST_SUITE_BEGIN("opencattus::utils");

TEST_CASE("isIn")
{
    const auto container = { 1, 2, 3 };
    CHECK(isIn(container, 3) == true);
    CHECK(isIn(container, 4) == false);
};

/**
 * @brief Run [func] if opencattus::dryRun is false
 */
template <typename T>
    requires std::is_default_constructible_v<T>
inline T dryrun(const std::function<T()>& func, const std::string& msg)
{
    auto opts = opencattus::Singleton<opencattus::services::Options>::get();
    if (opts->dryRun) {
        LOG_INFO("Dry Run: {}", msg);
        return T();
    }

    return func();
}

template <typename Path>
    requires std::is_convertible_v<Path, std::filesystem::path>
std::filesystem::directory_iterator openDir(const Path& path)
{
    return dryrun(
        static_cast<std::function<std::filesystem::directory_iterator()>>(
            [&path]() { return std::filesystem::directory_iterator(path); }),
        fmt::format("Dry Run: Would open directory {}", path.string()));
}

std::vector<std::string> getFilesByExtension(
    const auto& path, const auto& extension)
{
    std::vector<std::string> result;
    namespace fs = std::filesystem;

    for (const auto& entry : fs::directory_iterator(path)) {
        if (entry.is_regular_file() && entry.path().extension() == extension) {
            result.push_back(entry.path().filename().string());
        }
    }

    return result;
}

TEST_CASE("getFilesByExtension")
{
    const auto files = getFilesByExtension("repos/", ".conf");
    CHECK(files.size() > 0);
    CHECK(isIn(files, "repos.conf"));
}

void removeFilesWithExtension(const auto& path, const auto& extension)
{
    namespace fs = std::filesystem;

    std::string extensionLower = utils::string::lower(std::string(extension));

    for (const auto& entry : fs::directory_iterator(path)) {
        if (entry.is_regular_file()) {
            std::string ext
                = utils::string::lower(entry.path().extension().string());

            if (ext == extensionLower) {
                LOG_DEBUG("Removing file {}", entry.path());
                fs::remove(entry.path());
            }
        }
    }
}

TEST_CASE("removeFilesWithExtension")
{
    const std::filesystem::path path
        = "test/output/utils/removeFilesWithExtension";
    createDirectory(path);
    touchFile(path / "test.txt");
    CHECK(getFilesByExtension(path, ".txt").size() == 1);
    removeFilesWithExtension(path, ".txt");
    CHECK(getFilesByExtension(path, ".txt").size() == 0);
}

void copyFilesWithExtension(
    const auto& source, const auto& destination, const auto& extension)
{
    namespace fs = std::filesystem;

    for (const auto& entry : fs::directory_iterator(source)) {
        if (entry.is_regular_file() && entry.path().extension() == extension) {
            copyFile(entry.path(), destination / entry.path().filename());
        }
    }
}

TEST_CASE("copyFileWithExtension")
{
    const std::filesystem::path source
        = "test/output/utils/copyFilesWithExtension/src";
    const std::filesystem::path destination
        = "test/output/utils/copyFilesWithExtension/dst";
    createDirectory(source);
    createDirectory(destination);
    touchFile(source / "test.txt");
    touchFile(source / "test2.txt");
    touchFile(source / "test3.txt");
    CHECK(getFilesByExtension(source, ".txt").size() == 3);
    removeFilesWithExtension(destination, ".txt");
    CHECK(getFilesByExtension(destination, ".txt").size() == 0);
    copyFilesWithExtension(source, destination, ".txt");
    CHECK(getFilesByExtension(destination, ".txt").size() == 3);
}

void moveFilesWithExtension(
    const auto& source, const auto& destination, const auto& extension)
{
    copyFilesWithExtension(source, destination, extension);
    removeFilesWithExtension(source, extension);
}

// @FIXME: Yes, curl works I know, but no.. fix this, use boost for
//   requests and asynchronous I/O so we can run a bunch of these
//   concurrently
std::string getHttpStatus(const auto& url, const std::size_t maxRetries = 3)
{
    auto runner = opencattus::Singleton<IRunner>::get();
    auto opts = opencattus::Singleton<const services::Options>::get();
    if (opts->shouldSkip("http-status")) {
        LOG_WARN("Skipping HTTP status check for {}, assuming 200 (reason: "
                 "--skip=http-status in the command line)",
            url);
        return "200";
    }
    constexpr auto getHttpStatusInner = [](const auto& url,
                                            const auto& runner) {
        auto lines = runner->checkOutput(fmt::format(
            R"(bash -c "curl -sSLI {} | awk '/HTTP/ {{print $2}}' | tail -1" )",
            url));
        if (lines.size() > 0) {
            return lines[0];
        }
        return std::string("CURL ERROR");
    };

    std::string header;
    for (std::size_t i = 0; i < maxRetries; ++i) {
        header = getHttpStatusInner(url, runner);
        LOG_DEBUG("HTTP status of {}: {}", url, header);
        if (header.starts_with("2")) {
            return header;
        }
        if (i + 1 >= maxRetries) {
            break;
        }
        if (header.starts_with("5")) {
            LOG_DEBUG("HTTP internal server error {}, retrying {}/{}", header,
                i + 1, maxRetries);
        } else {
            LOG_DEBUG("HTTP {} error, retrying {}/{}", header, i + 1,
                maxRetries);
        }
    }
    return header;
};

TEST_CASE("getHttpStatus retries until success")
{
    class SequencedRunner final : public IRunner {
    public:
        explicit SequencedRunner(std::vector<std::string> responses)
            : m_responses(std::move(responses))
        {
        }

        int executeCommand(const std::string&) override { return 0; }
        int executeCommand(const std::string&, std::list<std::string>&) override
        {
            return 0;
        }
        services::CommandProxy executeCommandIter(
            const std::string&, services::Stream) override
        {
            return {};
        }
        void checkCommand(const std::string&) override { }
        std::vector<std::string> checkOutput(const std::string& cmd) override
        {
            m_commands.emplace_back(cmd);
            if (m_index >= m_responses.size()) {
                return {};
            }
            return { m_responses[m_index++] };
        }
        int downloadFile(const std::string&, const std::string&) override
        {
            return 0;
        }
        int run(const services::ScriptBuilder&) override { return 0; }

        [[nodiscard]] const std::vector<std::string>& commands() const
        {
            return m_commands;
        }

    private:
        std::size_t m_index = 0;
        std::vector<std::string> m_responses;
        std::vector<std::string> m_commands;
    };

    Singleton<const services::Options>::init(
        std::make_unique<const services::Options>(services::Options {}));

    auto runner
        = std::make_unique<SequencedRunner>(std::vector<std::string> { "404",
            "200" });
    auto* runnerPtr = runner.get();
    Singleton<IRunner>::init(std::move(runner));

    CHECK(getHttpStatus("https://example.com/repo", 3) == "200");
    CHECK(runnerPtr->commands().size() == 2);
}

[[noreturn]]
void abort(const fmt::string_view& fmt, auto&&... args)
{
    throw std::runtime_error(
        fmt::format(fmt::runtime(fmt), std::forward<decltype(args)>(args)...));
}

void abortif(const bool cond, const fmt::string_view& fmt, auto&&... args)
{
    if (cond) {
        throw std::runtime_error(fmt::format(
            fmt::runtime(fmt), std::forward<decltype(args)>(args)...));
    }
}

TEST_SUITE_END();
};

#endif // OPENCATTUS_FUNCTIONS_H_
