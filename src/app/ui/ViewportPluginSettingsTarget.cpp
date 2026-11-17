#include "ViewportPluginSettingsTarget.h"

#include <cfloat>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <numbers>
#include <string>
#include <vector>

#include "../../core/DebugDraw.h"
#include "../gpu/App.h"
#include "../scene/ScenePicking.h"
#include "../scene/SceneRenderer.h"
#include "OverlayColorTuning.h"
#include "OverlayLineTuning.h"
#include "ViewportInput.h"
#include "../../editor/tool/Editor.h"

namespace rs::app {
namespace {

// 赤い枠の見た目。RailSim II の CPartsInst::DrawBoxに合わせる。手前に重ねる薄い線(0x40ff0000)と、
// 深度テストを通る濃い線(0xffff0000)の2本を、1秒周期のsinで同じ係数で点滅させる(g_BlinkAlpha)。
constexpr double kPluginSettingsBoxBlinkPeriodSeconds = 1.0;
constexpr uint8_t kPluginSettingsBoxOverlayAlpha = 0x40;
constexpr uint8_t kPluginSettingsBoxFrontAlpha = 0xFF;

// ビューポートで個体を選ぶときの許容半径[px]。既存のPickSceneEntityの呼び出しと同じ値。
constexpr float kPluginSettingsPickRadiusPx = 10.0f;

// 点滅の係数(0〜1)。実時間tから 0.5 * (sin(2π t / 周期) + 1)。
float BlinkFactor(double elapsedSeconds) {
    const double phase = 2.0 * std::numbers::pi * elapsedSeconds / kPluginSettingsBoxBlinkPeriodSeconds;
    return static_cast<float>(0.5 * (std::sin(phase) + 1.0));
}

rs::core::DebugStyle MakeBoxStyle(uint8_t maxAlpha, float blink, rs::core::DebugDepthMode depthMode) {
    rs::core::DebugStyle style;
    const uint8_t alpha = static_cast<uint8_t>(std::lround(static_cast<float>(maxAlpha) * blink));
    style.color = WithAlpha(OverlayColorTuning::Embedded().pluginTargetBase, alpha);
    style.widthPx = OverlayLineTuning::Embedded().minWidthPx;
    style.distanceAttenuation = false;
    style.depthMode = depthMode;
    return style;
}

} // namespace

void HandlePluginSettingsClick(const EditorViewportContext& ctx, const ViewProjResult& vpr,
                               rs::app::input::PointerPos mouse) {
    PluginSettingsViewportLink* const link = ctx.pluginSettingsLink;
    if (link == nullptr || !link->clickEnabled) return;
    if (!ctx.input.AllowsPointer(rs::app::input::InputContext::View) || !IsMouseInViewport(mouse, ctx.width, ctx.height)) return;
    // 押してすぐ・ほとんど動かさずに離したときだけ。カメラのドラッグでは開かない。
    if (!ctx.input.ButtonTapped(ctx.input.ButtonFor(rs::app::input::PointerAction::Select))) return;

    rs::core::Ray ray;
    if (!rs::core::ScreenToRay(mouse.x, mouse.y, vpr.viewProj, ctx.width, ctx.height, ray)) return;

    // 情景の個体と編成の車両を1つの配列にまとめて、1回のレイキャストで一番手前を選ぶ。
    // どちらの収集関数も出力の配列を空にしてから詰めるので、別々に集めてからつなぐ。
    std::vector<SceneRenderer::ScenePickItem> items;
    ctx.sceneRenderer.CollectSceneryPickCandidates(ctx.sceneRegistry, ctx.editor.graph, ctx.log, items);
    std::vector<SceneRenderer::ScenePickItem> trainItems;
    ctx.sceneRenderer.CollectTrainPickCandidates(ctx.sceneRegistry, trainItems);
    items.insert(items.end(), std::make_move_iterator(trainItems.begin()), std::make_move_iterator(trainItems.end()));
    std::vector<ScreenPickCandidate> candidates;
    candidates.reserve(items.size());
    for (const SceneRenderer::ScenePickItem& item : items) candidates.push_back(item.candidate);

    size_t hitIndex = 0;
    if (!PickByRay(candidates, ray, FLT_MAX, vpr.viewProj, ctx.width, ctx.height, mouse.x, mouse.y,
                   kPluginSettingsPickRadiusPx, hitIndex)) {
        link->clickedEmpty = true;
        return;
    }
    link->clickedEntityId = items[hitIndex].entityId;
}

void DrawPluginSettingsTargetBox(const EditorViewportContext& ctx) {
    const PluginSettingsViewportLink* const link = ctx.pluginSettingsLink;
    if (link == nullptr || rs::world::GuidIsNil(link->targetEntityId)) return;

    SceneRenderer::ScenePickItem item;
    const bool found =
        ctx.sceneRenderer.TryGetSceneryPickCandidate(ctx.sceneRegistry, ctx.editor.graph, link->targetEntityId, ctx.log, item) ||
        ctx.sceneRenderer.TryGetTrainPickCandidate(ctx.sceneRegistry, link->targetEntityId, item);
    if (!found || !item.candidate.hasAabb) return;

    rs::core::Vec3 corners[8];
    if (!TransformAabbCorners(item.candidate.worldMatrix, item.candidate.aabbMin, item.candidate.aabbMax, corners)) return;

    const float blink = BlinkFactor(ctx.input.ElapsedSeconds());
    const rs::core::DebugStyle overlayStyle =
        MakeBoxStyle(kPluginSettingsBoxOverlayAlpha, blink, rs::core::DebugDepthMode::AlwaysOnTop);
    const rs::core::DebugStyle frontStyle =
        MakeBoxStyle(kPluginSettingsBoxFrontAlpha, blink, rs::core::DebugDepthMode::Test);
    // 辺は、頂点の番号が1ビットだけ違う組(TransformAabbCornersの番号付け)。
    for (int from = 0; from < 8; ++from) {
        for (int bit = 1; bit < 8; bit <<= 1) {
            if (from & bit) continue;
            const int to = from | bit;
            ctx.app.DebugDrawList().AddLine(corners[from], corners[to], overlayStyle);
            ctx.app.DebugDrawList().AddLine(corners[from], corners[to], frontStyle);
        }
    }
}

} // namespace rs::app
