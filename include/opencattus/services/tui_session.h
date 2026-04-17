/*
 * Copyright 2026 Vinícius Ferrão <vinicius@ferrao.net.br>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OPENCATTUS_SERVICES_TUI_SESSION_H_
#define OPENCATTUS_SERVICES_TUI_SESSION_H_

#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <opencattus/models/cluster.h>
#include <opencattus/services/options.h>

namespace opencattus::services::tui {

struct DraftState {
    bool draft = false;
    std::vector<std::string> completedSteps;
    std::optional<std::string> lastCompletedStep;
};

[[nodiscard]] auto defaultDraftPath(const Options& options)
    -> std::filesystem::path;
[[nodiscard]] auto defaultAnswerfilePath(const Options& options)
    -> std::filesystem::path;
[[nodiscard]] auto loadDraftState(const std::filesystem::path& path)
    -> DraftState;
[[nodiscard]] auto isDraftAnswerfile(const std::filesystem::path& path) -> bool;
[[nodiscard]] auto completedStepSet(const DraftState& state)
    -> std::set<std::string>;

void applyDraftToModel(
    models::Cluster& model, const std::filesystem::path& path);
void writeDraft(models::Cluster& model, const std::filesystem::path& path,
    const std::vector<std::string>& completedSteps, bool complete = false);

} // namespace opencattus::services::tui

#endif // OPENCATTUS_SERVICES_TUI_SESSION_H_
