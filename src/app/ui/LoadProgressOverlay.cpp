//
// LoadProgressOverlay.cpp
//
#include "LoadProgressOverlay.h"

#include "imgui.h"

#include "LoadProgressText.h"
#include "../action/LayoutPersistenceAction.h"
#include "../scene/SceneRenderer.h"

namespace rs::app {

void DrawLoadProgressOverlay(const LoadProgressOverlayContext& ctx) {
    if (!ctx.sceneRenderer.SyncJobActive()) return;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowBgAlpha(0.55f);
    const ImGuiWindowFlags overlayFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    ImGui::Begin("##LoadProgressOverlay", nullptr, overlayFlags);

    const size_t total = ctx.sceneRenderer.SyncJobPhaseTotal();
    const size_t done = ctx.sceneRenderer.SyncJobPhaseDone();
    const float fraction = total > 0 ? static_cast<float>(done) / static_cast<float>(total) : 1.0f;
    const std::string label = LoadPhaseLabel(ctx.sceneRenderer.SyncJobPhase(), ctx.localization);

    const ImVec2 boxSize(420.0f, 90.0f);
    ImGui::SetCursorPos(ImVec2((viewport->Size.x - boxSize.x) * 0.5f, (viewport->Size.y - boxSize.y) * 0.5f));
    ImGui::BeginChild("##LoadProgressBox", boxSize, true, ImGuiWindowFlags_NoScrollbar);
    ImGui::TextUnformatted(label.c_str());
    ImGui::ProgressBar(fraction, ImVec2(-1, 0));
    ImGui::Text("%zu / %zu エンティティ", done, total);
    ImGui::EndChild();

    ImGui::End();
}

void DrawPendingLayoutLoadIndicator(LayoutPersistenceAction& layoutAction, const Localization& localization) {
    if (!layoutAction.HasPendingLoad()) return;
    ImGui::SetNextWindowPos(ImVec2(620, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(260, 80), ImGuiCond_FirstUseEver);
    ImGui::Begin("読込中");
    ImGui::TextUnformatted(localization.Text(kLoadPendingKey).c_str());
    ImGui::End();
    layoutAction.MarkPendingLoadIndicatorShown();
}

} // namespace rs::app
