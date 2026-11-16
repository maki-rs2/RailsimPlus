#include "SceneryPlacementAction.h"

#include "../../editor/tool/Editor.h"
#include "../../plugins/load/PluginDefinitionLookup.h"
#include "../../plugins/schema/PluginSchema.h"

namespace rs::app {

bool SceneryPlacementAction::CanAnchor(const std::string& folderUtf8) {
    const rs::legacy_pi::ParsedDefinitionResult parsed = rs::legacy_plugin::LoadPluginDefinition(folderUtf8);
    return parsed.ok && parsed.def.header.type == rs::legacy_pi::PluginType::Station &&
           !parsed.def.platforms.empty();
}

void SceneryPlacementAction::Begin(const std::string& folderUtf8, const SceneryPlacementOptions& options) {
    End();
    editor_.placementAddMode = false;
    editor_.sectionSplitMode = false;
    editor_.wireEditMode = false;
    if (options.stationAnchor && CanAnchor(folderUtf8)) {
        editor_.stationAnchorFolder = folderUtf8;
        editor_.stationAnchorMode = true;
        return;
    }
    editor_.sceneryPlacementFolder = folderUtf8;
    editor_.sceneryPlacementContinuous = options.continuous;
    editor_.sceneryPlacementMode = true;
}

void SceneryPlacementAction::End() {
    editor_.stationAnchorMode = false;
    editor_.stationAnchorFolder.clear();
    editor_.sceneryPlacementMode = false;
    editor_.sceneryPlacementFolder.clear();
    editor_.sceneryPlacementHeightOffset = 0.0f;
}

bool SceneryPlacementAction::IsPlacing(const std::string& folderUtf8) const {
    return (editor_.sceneryPlacementMode && editor_.sceneryPlacementFolder == folderUtf8) ||
           (editor_.stationAnchorMode && editor_.stationAnchorFolder == folderUtf8);
}

bool SceneryPlacementAction::Active() const {
    return editor_.sceneryPlacementMode || editor_.stationAnchorMode;
}

} // namespace rs::app
