#pragma once

#include <array>
#include <string>
#include <vector>

#include "WorkspaceShell.h"

namespace rs::app::workspace {

struct HelpOperationRow {
    std::string operation_;
    std::string mouse_;
    std::string key_;
    bool operator==(const HelpOperationRow&) const = default;
};

struct HelpOperationTable {
    std::string modeName_;
    std::vector<HelpOperationRow> rows_;
    bool operator==(const HelpOperationTable&) const = default;
};

struct HelpWindowPresentation {
    std::array<HelpOperationTable, kScreenCount> screens_;
    std::array<HelpOperationTable, kWorkModeCount> workModes_;
    HelpOperationTable current_;

    bool SelectContext(const WorkspaceSnapshot& snapshot);
};

} // namespace rs::app::workspace
