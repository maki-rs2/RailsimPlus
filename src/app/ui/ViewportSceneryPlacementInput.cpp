#include "ViewportInputInternal.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <numbers>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../../core/Math.h"
#include "../../editor/tool/Editor.h"
#include "../../editor/tool/SceneEntityCommand.h"
#include "../../plugins/load/PluginDefinitionLookup.h"
#include "../../plugins/schema/PluginSchema.h"
#include "../../world/alignment/TrackQuery.h"
#include "../../world/alignment/TrackTraversal.h"
#include "../../world/scene/ScenePlacement.h"
#include "../gpu/App.h"
#include "../plugin_session/RailTieHeights.h"
#include "../scene/SceneRenderer.h"
#include "OverlayColorTuning.h"
#include "OverlayLineTuning.h"
#include "ViewportOverlayGeometry.h"

namespace rs::app {
namespace {
// 配置プレビュー用に、駅の番線レールの計画を持っておく。駅の位置・向き・レールのつながりが
// 変わらない間は、計画を作り直さない。
struct PlatformRailPreviewCache {
    std::string folder;
    rs::core::Mat4 world{};
    size_t graphEdgeCount = 0;
    size_t graphNodeCount = 0;
    bool valid = false;
    bool plansBuilt = false;
    rs::legacy_pi::ParsedDefinitionResult definition;
    std::vector<rs::editor::PlatformRailPlan> plans;
};

bool SameMatrix(const rs::core::Mat4& a, const rs::core::Mat4& b) {
    return std::memcmp(a.m, b.m, sizeof(a.m)) == 0;
}

// Platform{} を持つ駅を置こうとしているとき、作られるレールの中心線をプレビュー色で描く。
void DrawPlatformRailPreview(const EditorViewportContext& ctx, const rs::world::ScenePlacement& scenePlacement) {
    static PlatformRailPreviewCache cache;
    rs::editor::Editor& editor = ctx.editor;
    const rs::core::Mat4 world = rs::world::ResolveWorldMatrix(scenePlacement, editor.graph);
    if (!cache.valid || cache.folder != editor.sceneryPlacementFolder) {
        cache = PlatformRailPreviewCache{};
        cache.folder = editor.sceneryPlacementFolder;
        cache.definition = rs::legacy_plugin::LoadPluginDefinition(cache.folder);
        cache.valid = true;
        cache.plansBuilt = false;
    }
    // 駅の位置・向き・レールのつながりが変わらない間は、前のフレームの計画を使う。
    if (!cache.plansBuilt || !SameMatrix(cache.world, world) || cache.graphEdgeCount != editor.graph.edges.size() ||
        cache.graphNodeCount != editor.graph.nodes.size()) {
        cache.world = world;
        cache.graphEdgeCount = editor.graph.edges.size();
        cache.graphNodeCount = editor.graph.nodes.size();
        cache.plansBuilt = true;
        cache.plans.clear();
        if (cache.definition.ok && !cache.definition.def.platforms.empty()) {
            rs::world::SceneEntity entity;
            entity.folderUtf8 = cache.folder;
            entity.placement = scenePlacement;
            cache.plans = editor.PlanPlatformRails(entity, cache.definition.def);
        }
    }
    if (cache.plans.empty()) return;
    const float lift = ComputeRailTieHeights(editor.layingSettings.defaultTrackPlugins, ctx.selector).TopLift();
    const uint32_t color = OverlayColorTuning::Embedded().valid;
    const float width = OverlayLineTuning::Embedded().minWidthPx;
    for (const rs::editor::PlatformRailPlan& railPlan : cache.plans) {
        std::vector<rs::core::Vec3> points;
        points.reserve(railPlan.shape.spline.size());
        for (const rs::world::TrackNodeSample& sample : railPlan.shape.spline)
            points.push_back(sample.pos + sample.up * lift);
        ctx.app.DebugDrawList().AddPolyline(points.data(), points.size(), false, MakeTopStyle(color, width));
    }
}
} // namespace

void HandleSceneryPlacementModeInput(const EditorViewportContext& ctx, const rs::app::ViewProjResult& vpr,
                                    rs::app::input::PointerPos mouse, bool leftClickForEditor) {
    rs::editor::Editor& editor = ctx.editor;
    if (editor.sceneryPlacementFolder.empty()) {
        editor.sceneryPlacementMode = false;
        return;
    }
    rs::core::Ray ray;
    rs::core::Vec3 groundHit;
    if (!rs::core::ScreenToRay(mouse.x, mouse.y, vpr.viewProj, ctx.width, ctx.height, ray) ||
        !rs::core::RayHitPlaneY(ray, 0.0f, groundHit)) {
        ctx.sceneRenderer.SetPlacementPreview(ctx.app, std::nullopt, ctx.log);
        return;
    }
    // 置けない範囲(Surfaceの外)にマウスがあるときは、画面外と同じにプレビューを消して置かせない。
    if (!editor.ClipToSurface(groundHit)) {
        ctx.sceneRenderer.SetPlacementPreview(ctx.app, std::nullopt, ctx.log);
        return;
    }
    rs::world::FreePlacement placement;
    placement.position = groundHit + rs::core::Vec3(0.0f, editor.sceneryPlacementHeightOffset, 0.0f);
    placement.yawDeg = ctx.sceneryPlacementUi.yawDeg;
    placement.pitchDeg = ctx.sceneryPlacementUi.pitchDeg;
    placement.rollDeg = ctx.sceneryPlacementUi.rollDeg;
    if (ctx.sceneryPlacementUi.snapToRail) {
        const float yaw = placement.yawDeg * std::numbers::pi_v<float> / 180.0f;
        const rs::core::Vec3 forward(std::sin(yaw), 0.0f, std::cos(yaw));
        const rs::world::NearestTrackHit hit = rs::world::FindNearestTrackPosition(
            editor.graph, placement.position, forward, std::numeric_limits<double>::max());
        if (hit.found) {
            const std::unordered_map<rs::world::Guid, rs::world::TrackEdge, rs::world::GuidHash>::const_iterator edge =
                editor.graph.edges.find(hit.position.edge);
            if (edge != editor.graph.edges.end()) {
                const rs::world::TrackFrame frame =
                    rs::world::EvaluateAt(edge->second, hit.position.s, hit.position.direction);
                placement = rs::world::DecomposeFreePlacement(frame.pos, frame.tangent, frame.up);
            }
        }
    }
    const rs::world::ScenePlacement scenePlacement = placement;
    ctx.sceneRenderer.SetPlacementPreview(
        ctx.app, SceneRenderer::PlacementPreviewSpec{editor.sceneryPlacementFolder,
                                                      rs::world::ResolveWorldMatrix(scenePlacement, editor.graph)},
        ctx.log);
    // レールに合わせないときは、置いたあとに作られる番線のレールを先に見せる。
    DrawHeightLine(ctx, placement.position);
    if (!ctx.sceneryPlacementUi.snapToRail) DrawPlatformRailPreview(ctx, scenePlacement);
    if (!leftClickForEditor) return;

    rs::world::SceneEntity entity;
    entity.folderUtf8 = editor.sceneryPlacementFolder;
    entity.category = rs::world::SceneEntityCategory::Scenery;
    entity.placement = placement;
    rs::editor::SceneEntityCommand::Change change;
    change.after = entity;
    change.afterIndex = ctx.sceneRegistry.Entities().size();
    if (!editor.ExecuteCommand(
            std::make_unique<rs::editor::SceneEntityCommand>(std::vector{std::move(change)}),
            &ctx.sceneRegistry)) return;
    editor.NotePlaced();
    const rs::world::SceneEntity* created = ctx.sceneRegistry.Entities().empty()
        ? nullptr : &ctx.sceneRegistry.Entities().back();
    const rs::world::Guid placedId = created ? created->guid : rs::world::NilGuid();
    editor.SelectSceneEntity(placedId);
    // 既存のレールに吸着しないときは、Platform{} を持つ駅のレールと番線も一緒に作る。
    if (created != nullptr && !ctx.sceneryPlacementUi.snapToRail) {
        const rs::legacy_pi::ParsedDefinitionResult definition =
            rs::legacy_plugin::LoadPluginDefinition(editor.sceneryPlacementFolder);
        if (definition.ok && !definition.def.platforms.empty())
            editor.GeneratePlatformRails(ctx.sceneRegistry.Entities().back(), definition.def);
    }
    if (!editor.sceneryPlacementContinuous) {
        editor.sceneryPlacementMode = false;
        editor.sceneryPlacementFolder.clear();
        editor.sceneryPlacementHeightOffset = 0.0f;
        ctx.sceneRenderer.SetPlacementPreview(ctx.app, std::nullopt, ctx.log);
    }
}

} // namespace rs::app
