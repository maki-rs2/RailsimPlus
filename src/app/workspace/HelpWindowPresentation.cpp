#include "HelpWindowPresentation.h"

#include "EnumIndex.h"

namespace rs::app::workspace {

bool HelpWindowPresentation::SelectContext(const WorkspaceSnapshot& snapshot) {
    const HelpOperationTable& table = snapshot.InWorkMode()
        ? workModes_[EnumIndex(snapshot.workMode)] : screens_[EnumIndex(snapshot.screen)];
    if (current_ == table) return false;
    current_ = table;
    return true;
}

} // namespace rs::app::workspace
