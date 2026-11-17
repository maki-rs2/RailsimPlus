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

    const double fraction = ctx.sceneRenderer.SyncJobOverallFraction();
    const std::string label = LoadPhaseLabel(ctx.sceneRenderer.SyncJobPhase(), ctx.localization);

    const ImVec2 boxSize(420.0f, 90.0f);
    ImGui::SetCursorPos(ImVec2((viewport->Size.x - boxSize.x) * 0.5f, (viewport->Size.y - boxSize.y) * 0.5f));
    ImGui::BeginChild("##LoadProgressBox", boxSize, true, ImGuiWindowFlags_NoScrollbar);
    ImGui::TextUnformatted(label.c_str());
    ImGui::ProgressBar(static_cast<float>(fraction), ImVec2(-1, 0));
    ImGui::TextUnformatted(LoadProgressPercentText(fraction, ctx.localization).c_str());
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
