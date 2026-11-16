#include "PluginReferenceWalk.h"

#include "../../editor/tool/Editor.h"
#include "LayoutSession.h"
#include "Rs2SceneApplier.h"

// 訪問(VisitPluginReferences)と同じ「アクティブSceneはライブ状態、ほかはキャッシュ」の見方で、
// Sceneの添字から実体を引く。

namespace rs::app {

std::size_t SceneCountOf(const PluginReferenceContext& context) {
    return context.session.scenes.empty() ? 1 : context.session.scenes.size();
}

rs::world::TrackGraph* SceneGraphFor(PluginReferenceContext& context, std::size_t sceneIndex) {
    LayoutSession& session = context.session;
    if (session.scenes.empty() || sceneIndex == session.activeIndex) return &context.editor.graph;
    if (sceneIndex >= session.scenes.size()) return nullptr;
    return &session.scenes[sceneIndex].graph;
}

rs::world::ManualPlacementOverlay* SceneOverlayFor(PluginReferenceContext& context, std::size_t sceneIndex) {
    LayoutSession& session = context.session;
    if (session.scenes.empty() || sceneIndex == session.activeIndex) return &context.editor.manualOverlay;
    if (sceneIndex >= session.scenes.size()) return nullptr;
    return &session.scenes[sceneIndex].overlay;
}

std::vector<ImportedWireSpan>* SceneWireSpansFor(PluginReferenceContext& context, std::size_t sceneIndex) {
    LayoutSession& session = context.session;
    if (sceneIndex == session.activeIndex && context.importedWireSpans != nullptr) {
        return context.importedWireSpans;
    }
    if (sceneIndex >= session.scenes.size()) return nullptr;
    return &session.scenes[sceneIndex].wireSpans;
}

}  // namespace rs::app
