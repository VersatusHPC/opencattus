/*
 * Copyright 2021 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <unistd.h>
#include <utility>
#include <vector>

#include <opencattus/const.h>
#include <opencattus/dbus_client.h>
#include <opencattus/functions.h>
#include <opencattus/models/cluster.h>
#include <opencattus/opencattus.h>
#include <opencattus/presenter/PresenterInstall.h>
#include <opencattus/services/ansible/roles.h>
#include <opencattus/services/confluent.h>
#include <opencattus/services/files.h>
#include <opencattus/services/init.h>
#include <opencattus/services/log.h>
#include <opencattus/services/options.h>
#include <opencattus/services/shell.h>
#include <opencattus/services/xcat.h>
#include <opencattus/utils/formatters.h>
#include <opencattus/utils/singleton.h>
#include <opencattus/verification.h>
#include <opencattus/view/newt.h>

#include <internal_use_only/config.hpp>

#ifdef _OPENCATTUS_I18N
#include "include/i18n-cpp.hpp"
#endif

namespace {

using namespace opencattus;
using namespace opencattus::services;

// Run test commands and exit. This is to make easier to test
// code during development and troubleshooting.  Use a combination of
// --force and --skip to control what code to run.
int runTestCommand(const std::string& testCommand,
    const std::vector<std::string>& testCommandArgs)
{
#ifndef NDEBUG
    LOG_INFO("Running test command {} {} ", testCommand,
        fmt::join(testCommandArgs, ","));
    auto cluster = opencattus::Singleton<opencattus::models::Cluster>::get();
    auto runner = opencattus::Singleton<opencattus::functions::IRunner>::get();
    auto repoManager = opencattus::Singleton<repos::RepoManager>::get();
    if (testCommand == "execute-command") {
        runner->checkCommand(testCommandArgs[0]);
    } else if (testCommand == "initialize-repos") {
        repoManager->initializeDefaultRepositories();
        runner->checkCommand(
            R"(bash -c "dnf config-manager --set-enabled '*' && dnf makecache -y" )");
    } else if (testCommand == "create-http-repo") {
        assert(testCommandArgs.size() > 0);
        opencattus::functions::createHTTPRepo(testCommandArgs[0]);
    } else if (testCommand == "parse-key-file") {
        assert(testCommandArgs.size() > 0);
        LOG_INFO("Loading file {}", testCommandArgs[0]);
        auto file = opencattus::services::files::KeyFile(testCommandArgs[0]);
        LOG_INFO("Groups: {}", fmt::join(file.getGroups(), ","));
        LOG_INFO("Contents: {}", file.toData());
    } else if (testCommand == "confluent-install") {
        Confluent cfl;
        cfl.install();
    } else if (testCommand == "install-doca-ofed"
        || testCommand == "install-mellanox-ofed") {
        OFED(OFED::Kind::Doca, "latest").install();
    } else if (testCommand == "image-install-doca-ofed"
        || testCommand == "image-install-mellanox-ofed") {
        auto provisioner = std::make_unique<opencattus::services::XCAT>();
        provisioner->configureInfiniband();
    } else if (testCommand == "dump-headnode-os") {
        LOG_INFO("OS: {}", cluster->getHeadnode().getOS());
    } else if (testCommand == "dump-xcat-osimage") {
        auto provisioner = std::make_unique<opencattus::services::XCAT>();
        LOG_INFO("xCAT osimage: {}", provisioner->getImage());
    } else if (testCommand == "xcat-patch") {
        auto provisioner = std::make_unique<opencattus::services::XCAT>();
        provisioner->patchInstall();
    } else if (testCommand == "ansible-role") {
        assert(testCommandArgs.size() == 1);
        // Execute a single role
        opencattus::services::ansible::roles::run(
            ansible::roles::parseRoleString(testCommandArgs[0]),
            cluster->getHeadnode().getOS());
    } else {
        LOG_ERROR("Invalid test command {}", testCommand);
        return EXIT_FAILURE;
    }
#endif
    return EXIT_SUCCESS;
}

class ScopedFileRemoval {
private:
    std::filesystem::path m_path;

public:
    explicit ScopedFileRemoval(std::filesystem::path path)
        : m_path(std::move(path))
    {
    }

    ScopedFileRemoval(const ScopedFileRemoval&) = delete;
    ScopedFileRemoval& operator=(const ScopedFileRemoval&) = delete;
    ScopedFileRemoval(ScopedFileRemoval&&) = delete;
    ScopedFileRemoval& operator=(ScopedFileRemoval&&) = delete;

    ~ScopedFileRemoval()
    {
        std::error_code error;
        if (!m_path.empty() && !std::filesystem::remove(m_path, error)
            && error) {
            LOG_WARN("Failed to remove temporary TUI answerfile {}: {}",
                m_path.string(), error.message());
        }
    }
};

auto createTemporaryTuiAnswerfilePath() -> std::filesystem::path
{
    auto pathTemplate
        = (std::filesystem::temp_directory_path() / "opencattus-tui-XXXXXX")
              .string();
    std::vector<char> mutableTemplate(pathTemplate.begin(), pathTemplate.end());
    mutableTemplate.push_back('\0');

    const int descriptor = mkstemp(mutableTemplate.data());
    if (descriptor == -1) {
        throw std::runtime_error(
            fmt::format("Unable to create temporary TUI answerfile: {}",
                std::strerror(errno)));
    }

    const auto path = std::filesystem::path(mutableTemplate.data());
    std::error_code permissionError;
    std::filesystem::permissions(path,
        std::filesystem::perms::owner_read
            | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace, permissionError);
    if (permissionError) {
        close(descriptor);
        std::error_code removeError;
        std::filesystem::remove(path, removeError);
        throw std::runtime_error(fmt::format(
            "Unable to restrict temporary TUI answerfile permissions on {}: {}",
            path.string(), permissionError.message()));
    }

    if (close(descriptor) != 0) {
        std::error_code removeError;
        std::filesystem::remove(path, removeError);
        throw std::runtime_error(
            fmt::format("Unable to close temporary TUI answerfile {}: {}",
                path.string(), std::strerror(errno)));
    }

    return path;
}

auto generateAnswerfileFromTuiModel(opencattus::models::Cluster& model)
    -> std::unique_ptr<opencattus::models::AnswerFile>
{
    const auto path = createTemporaryTuiAnswerfilePath();
    const ScopedFileRemoval cleanup(path);

    model.dumpData(path);
    LOG_INFO("Generated temporary TUI answerfile at {}", path.string());
    return std::make_unique<opencattus::models::AnswerFile>(path);
}

}; // anonymous namespace

/**
 * @brief The entrypoint.
 */
int main(int argc, const char** argv)
{
    // Options are not const yet because some parameters are mutated during the
    // initialization in main, maybe this should be moved to options.cpp and
    // factory should return constant options, we also mutate the options during
    // the tests
    auto optsMut = options::factory(argc, argv);
    const bool shouldRunInteractiveQuestionnaire
        = optsMut->answerfile.empty() && optsMut->testCommand.empty();

    if (optsMut->parsingError) {
        fmt::print("Parsing error: {}", optsMut->error);
        return EXIT_FAILURE;
    }

    if (optsMut->showVersion) {
        fmt::print("{}: Version {}\n", productName, productVersion);
        return EXIT_SUCCESS;
    }

    if (optsMut->helpAndExit) {
        fmt::print("Help:\n{}", optsMut->helpText);
        return EXIT_SUCCESS;
    }

    if (optsMut->listRoles) {
        const auto roles = utils::string::lower(fmt::format("{}",
            fmt::join(
                utils::enums::toStrings<services::ansible::roles::Roles>(),
                ",")));

        fmt::print("Roles: {}", roles);
        return EXIT_SUCCESS;
    }
    optsMut->enableTUI = shouldRunInteractiveQuestionnaire;
    Log::init(optsMut->logLevelInput, !optsMut->enableTUI);

#ifndef NDEBUG
    LOG_DEBUG("Log level set to: {}\n", optsMut->logLevelInput)
#endif
    LOG_INFO("{} Started", productName)

    if (optsMut->testCommand.empty() && optsMut->dumpAnswerfile.empty()) {
        // skip during tests, we do not want to run tests as root
        opencattus::checkEffectiveUserId();
    }

    // --test implies --unattended
    if (!optsMut->testCommand.empty()) {
        optsMut->unattended = true;
    }

    if (optsMut->dryRun) {
        LOG_INFO("Dry run enabled.");
    } else {
        while (!optsMut->unattended && optsMut->dumpAnswerfile.empty()) {
            char response = 'N';
            fmt::print("{} will now modify your system, do you want to "
                       "continue? [Y/N]\n",
                opencattus::productName);
            std::cin >> response;

            if (std::toupper(response) == 'Y') {
                LOG_INFO("Running {}.\n", opencattus::productName)
                break;
            } else if (std::toupper(response) == 'N') {
                LOG_INFO("Stopping {}.\n", opencattus::productName)
                return EXIT_SUCCESS;
            }
        }
    }

    //@TODO implement CLI feature
    if (optsMut->enableCLI) {
        LOG_ERROR("CLI feature not implemented.\n");
        return EXIT_FAILURE;
    }

    // Initialize options singleton making it const
    LOG_DEBUG("Initializing command line options");
    initializeSingletonsOptions(std::move(optsMut));
    auto opts = utils::singleton::options();
    // Assert that opts is const from now on
    static_assert(std::is_const_v<std::remove_reference_t<decltype(*opts)>>);

    LOG_INFO("Initializing the model");
    auto model = std::make_unique<opencattus::models::Cluster>();
    LOG_INFO("Model initialized");
    std::unique_ptr<models::AnswerFile> answerfile;
    if (!opts->answerfile.empty()) {
        LOG_INFO("Loading the answerfile: {}", opts->answerfile)
        answerfile = std::make_unique<models::AnswerFile>(opts->answerfile);
        model->fillData(*answerfile);
    }
    LOG_INFO("Answerfile loaded: {}", opts->answerfile)

#ifndef NDEBUG
    // model->fillTestData();
    model->printData();
#endif

    if (opts->enableTUI) {
        // Entrypoint; if the view is constructed it will start the TUI.
        std::unique_ptr<View> view = std::make_unique<Newt>();
        auto presenter
            = std::make_unique<opencattus::presenter::PresenterInstall>(
                model, view);
        answerfile = generateAnswerfileFromTuiModel(*model);

        if (opts->dumpAnswerfile.empty() && !opts->dryRun) {
            Log::init(opts->logLevelInput, true);
        }
    }

    if (!opts->dumpAnswerfile.empty()) {
        model->dumpData(opts->dumpAnswerfile);
        Log::shutdown();
        return EXIT_SUCCESS;
    }

    if (opts->dryRun && opts->enableTUI) {
        LOG_INFO("Dry run questionnaire complete; skipping the installation "
                 "engine");
        Log::shutdown();
        return EXIT_SUCCESS;
    }

    initializeSingletonsModel(std::move(model), std::move(answerfile));

#ifndef NDEBUG
    if (!opts->testCommand.empty()) {
        return runTestCommand(opts->testCommand, opts->testCommandArgs);
    }
#endif
    LOG_TRACE("Starting execution engine");
    auto executionEngine = [&]() -> std::unique_ptr<Execution> {
        if (opts->roles.empty()) {
            return std::make_unique<opencattus::services::Shell>();
        } else {
            return std::make_unique<
                opencattus::services::ansible::roles::Executor>();
        };
    }();

    executionEngine->install();

    LOG_INFO("{} has successfully ended", productName)
    Log::shutdown();

    return EXIT_SUCCESS;
}
