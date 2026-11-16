#include "EditorViewportState.h"

namespace rs::app {

void EditorViewportState::Synchronize(uint64_t sceneGeneration, uint64_t pluginGeneration) {
    if (sceneGeneration_ != sceneGeneration) {
        pendingEdge = rs::world::NilGuid();
        pendingDematerialize.clear();
        hangerModels.clear();
        lastModeSignature_.reset();
        sceneGeneration_ = sceneGeneration;
    }
    if (pluginGeneration_ != pluginGeneration) {
        hangerModels.clear();
        pluginGeneration_ = pluginGeneration;
    }
}

bool EditorViewportState::UpdateMode(const EditorModeSignature& signature) {
    const bool changed = lastModeSignature_.has_value() && *lastModeSignature_ != signature;
    lastModeSignature_ = signature;
    return changed;
}

} // namespace rs::app
