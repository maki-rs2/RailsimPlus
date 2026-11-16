#include "SceneryTransformGizmo.h"

#include "../../core/Epsilon.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "imgui.h"

#include "../../core/Math.h"
#include "../../editor/tool/Editor.h"
#include "../action/SceneryTransformMath.h"
#include "../../world/scene/ScenePlacement.h"
#include "../../world/scene/SceneRegistry.h"

namespace rs::app {
namespace {

using rs::core::Vec2;
using rs::core::Vec3;

constexpr float kArrowPickRadiusPx = 9.0f;
constexpr float kRingPickRadiusPx = 7.0f;
constexpr float kArrowLengthPx = 74.0f;
constexpr float kRingRadiusScale = 1.28f;
constexpr size_t kRingSegments = 48;
constexpr uint32_t kAxisColors[3] = {IM_COL32(245, 75, 75, 255), IM_COL32(80, 220, 120, 255),
                                      IM_COL32(85, 155, 255, 255)};

struct GizmoGeometry {
    Vec3 pivot;
    Vec3 axes[3];
    Vec2 center;
    Vec2 arrows[3];
    std::array<Vec2, kRingSegments + 1> rings[3];
    float worldLength = 0.0f;
    bool valid = false;
};

float Dot2(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
Vec2 Sub2(const Vec2& a, const Vec2& b) { return {a.x - b.x, a.y - b.y}; }
float Length2(const Vec2& v) { return std::sqrt(Dot2(v, v)); }

float DistanceToSegment(const Vec2& p, const Vec2& a, const Vec2& b) {
    const Vec2 segment = Sub2(b, a);
    const float lengthSq = Dot2(segment, segment);
    if (lengthSq <= rs::core::kGizmoSegmentLengthSquaredEpsPx2) return Length2(Sub2(p, a));
    const float t = std::clamp(Dot2(Sub2(p, a), segment) / lengthSq, 0.0f, 1.0f);
    return Length2(Sub2(p, {a.x + segment.x * t, a.y + segment.y * t}));
}

bool Project(const Vec3& point, const ViewProjResult& viewProj, UINT width, UINT height, Vec2& out) {
    const rs::core::ProjectedPoint p = rs::core::ProjectToScreen(point, viewProj.viewProj, width, height);
    if (p.w <= 0.0f) return false;
    out = {p.x, p.y};
    return true;
}

bool BuildGeometry(const EditorViewportContext& ctx, const ViewProjResult& viewProj, GizmoGeometry& out) {
    const rs::editor::Editor& editor = ctx.editor;
    if (!GetViewModeTraits(ctx.viewMode).scenery || editor.SelectedSceneEntities().empty()) return false;
    std::vector<const rs::world::SceneEntity*> entities;
    Vec3 pivot{};
    for (const rs::world::Guid& id : editor.SelectedSceneEntities()) {
        const rs::world::SceneEntity* entity = ctx.sceneRegistry.FindEntity(id);
        if (entity == nullptr || entity->category != rs::world::SceneEntityCategory::Scenery) continue;
        const rs::world::FreePlacement* placement = std::get_if<rs::world::FreePlacement>(&entity->placement);
        if (placement == nullptr) continue;
        entities.push_back(entity);
        pivot = pivot + placement->position;
    }
    if (entities.empty()) return false;
    pivot = pivot * (1.0f / static_cast<float>(entities.size()));

    const rs::world::SceneEntity* primary = ctx.sceneRegistry.FindEntity(editor.selectedSceneEntityId);
    if (primary == nullptr) primary = entities.back();
    const rs::core::Mat4 primaryWorld = rs::world::ResolveWorldMatrix(primary->placement, editor.graph);
    const std::array<Vec3, 3> axes = rs::app::SceneryGizmoAxes(
        Vec3(primaryWorld.m[8], primaryWorld.m[9], primaryWorld.m[10]));
    const Vec3 worldUp = axes[1];

    Vec2 center;
    if (!Project(pivot, viewProj, ctx.width, ctx.height, center)) return false;
    const float worldLength = rs::core::WorldRadiusForPixels(
        viewProj.viewProj, ctx.width, ctx.height, center.x, center.y, pivot, kArrowLengthPx);
    if (worldLength <= rs::core::kGizmoWorldLengthEpsM) return false;
    out.pivot = pivot;
    for (size_t axis = 0; axis < axes.size(); ++axis) out.axes[axis] = axes[axis];
    out.center = center;
    out.worldLength = worldLength;
    for (int axis = 0; axis < 3; ++axis) {
        if (!Project(pivot + out.axes[axis] * worldLength, viewProj, ctx.width, ctx.height,
                     out.arrows[axis])) return false;
    }

    const Vec3 planeA[3] = {worldUp, axes[0], worldUp};
    const Vec3 planeB[3] = {axes[2], axes[2], axes[0]};
    const float ringRadius = worldLength * kRingRadiusScale;
    for (int axis = 0; axis < 3; ++axis) {
        for (size_t i = 0; i <= kRingSegments; ++i) {
            const double angle = 2.0 * std::numbers::pi * static_cast<double>(i) /
                                 static_cast<double>(kRingSegments);
            const Vec3 point = pivot + planeA[axis] * (ringRadius * static_cast<float>(std::cos(angle))) +
                              planeB[axis] * (ringRadius * static_cast<float>(std::sin(angle)));
            if (!Project(point, viewProj, ctx.width, ctx.height, out.rings[axis][i])) return false;
        }
    }
    out.valid = true;
    return true;
}

bool PickHandle(const GizmoGeometry& geometry, const Vec2& mouse, bool& rotation, int& axisIndex) {
    float best = kArrowPickRadiusPx;
    bool found = false;
    for (int axis = 0; axis < 3; ++axis) {
        const float distance = DistanceToSegment(mouse, geometry.center, geometry.arrows[axis]);
        if (distance < best) { best = distance; axisIndex = axis; rotation = false; found = true; }
    }
    best = kRingPickRadiusPx;
    for (int axis = 0; axis < 3; ++axis) {
        for (size_t i = 0; i < kRingSegments; ++i) {
            const float distance = DistanceToSegment(mouse, geometry.rings[axis][i],
                                                     geometry.rings[axis][i + 1]);
            if (distance < best) { best = distance; axisIndex = axis; rotation = true; found = true; }
        }
    }
    return found;
}

double WrapRadians(double angle) {
    while (angle > std::numbers::pi) angle -= 2.0 * std::numbers::pi;
    while (angle < -std::numbers::pi) angle += 2.0 * std::numbers::pi;
    return angle;
}

void ApplyDragPose(const rs::editor::Editor::SceneEntityGizmoDragState& drag,
                   rs::world::SceneRegistry& registry, const rs::world::TrackGraph& graph,
                   const Vec3& axis,
                   double delta) {
    for (const rs::world::SceneEntity& original : drag.before) {
        const rs::world::FreePlacement* initial = std::get_if<rs::world::FreePlacement>(&original.placement);
        if (initial == nullptr) continue;
        rs::world::FreePlacement next = *initial;
        if (!drag.rotation) {
            next.position = rs::app::TransformSceneryPosition(initial->position, drag.pivot, axis, false,
                                                               static_cast<float>(delta));
        } else {
            const float radians = static_cast<float>(delta);
            next.position = rs::app::TransformSceneryPosition(initial->position, drag.pivot, axis, true, radians);
            const rs::core::Mat4 world = rs::world::ResolveWorldMatrix(original.placement, graph);
            const Vec3 forward = rs::world::RotateAroundAxis(Vec3(world.m[8], world.m[9], world.m[10]),
                                                             axis, radians);
            const Vec3 up = rs::world::RotateAroundAxis(Vec3(world.m[4], world.m[5], world.m[6]),
                                                        axis, radians);
            next = rs::world::DecomposeFreePlacement(next.position, forward, up);
        }
        registry.CommitPlacement(original.guid, next);
    }
}

} // namespace

bool HandleSceneryTransformGizmo(const EditorViewportContext& ctx, const ViewProjResult& viewProj,
                                 rs::app::input::PointerPos mouse) {
    rs::editor::Editor& editor = ctx.editor;
    rs::editor::Editor::SceneEntityGizmoDragState& drag = editor.sceneEntityGizmoDrag;
    const Vec2 cursor(mouse.x, mouse.y);
    if (drag.active) {
        if (!GetViewModeTraits(ctx.viewMode).scenery) {
            for (const rs::world::SceneEntity& entity : drag.before) {
                if (const rs::world::FreePlacement* placement =
                        std::get_if<rs::world::FreePlacement>(&entity.placement))
                    ctx.sceneRegistry.CommitPlacement(entity.guid, *placement);
            }
            drag = {};
            return true;
        }
        double delta = 0.0;
        if (!drag.rotation) {
            const Vec2 offset = Sub2(cursor, drag.startMouse);
            delta = static_cast<double>(Dot2(offset, drag.screenAxis) * drag.worldPerPixel);
            if (ctx.input.MatchesPointer(rs::app::input::PointerAction::TransformSnap)) delta = rs::app::SnapSceneryTransformDelta(delta, 0.2);
        } else {
            const double current = std::atan2(-(static_cast<double>(cursor.y) - drag.screenCenter.y),
                                               static_cast<double>(cursor.x) - drag.screenCenter.x);
            delta = WrapRadians(current - drag.startScreenAngle);
            if (ctx.input.MatchesPointer(rs::app::input::PointerAction::TransformSnap)) {
                const double step = std::max(1.0, static_cast<double>(ctx.sceneryPlacementUi.yawStepDeg)) *
                                    std::numbers::pi / 180.0;
                delta = rs::app::SnapSceneryTransformDelta(delta, step);
            }
        }
        ApplyDragPose(drag, ctx.sceneRegistry, editor.graph, drag.axis, delta);
        if (ctx.input.ButtonReleased(ctx.input.ButtonFor(rs::app::input::PointerAction::Select))) {
            editor.CommitSceneEntityTransform(ctx.sceneRegistry, drag.before);
            drag = {};
        }
        return true;
    }

    if (!GetViewModeTraits(ctx.viewMode).scenery || !ctx.input.ButtonClicked(ctx.input.ButtonFor(rs::app::input::PointerAction::Select)) ||
        !ctx.input.AllowsPointer(rs::app::input::InputContext::Edit)) return false;
    GizmoGeometry geometry;
    if (!BuildGeometry(ctx, viewProj, geometry)) return false;
    bool rotation = false;
    int axisIndex = 0;
    if (!PickHandle(geometry, cursor, rotation, axisIndex)) return false;
    const Vec2 projectedAxis = Sub2(geometry.arrows[axisIndex], geometry.center);
    const float projectedLength = Length2(projectedAxis);
    if (projectedLength <= rs::core::kGizmoProjectedLengthEpsPx) return false;
    drag.active = true;
    drag.rotation = rotation;
    drag.axisIndex = axisIndex;
    drag.axis = geometry.axes[axisIndex];
    drag.pivot = geometry.pivot;
    drag.screenCenter = geometry.center;
    drag.screenAxis = {projectedAxis.x / projectedLength, projectedAxis.y / projectedLength};
    drag.worldPerPixel = geometry.worldLength / projectedLength;
    drag.startMouse = cursor;
    drag.startScreenAngle = std::atan2(-(static_cast<double>(mouse.y) - geometry.center.y),
                                        static_cast<double>(mouse.x) - geometry.center.x);
    for (const rs::world::Guid& id : editor.SelectedSceneEntities()) {
        const rs::world::SceneEntity* entity = ctx.sceneRegistry.FindEntity(id);
        if (entity != nullptr && entity->category == rs::world::SceneEntityCategory::Scenery &&
            std::holds_alternative<rs::world::FreePlacement>(entity->placement))
            drag.before.push_back(*entity);
    }
    if (drag.before.empty()) { drag = {}; return false; }
    return true;
}

void DrawSceneryTransformGizmo(const EditorViewportContext& ctx, const ViewProjResult& viewProj) {
    GizmoGeometry geometry;
    if (!BuildGeometry(ctx, viewProj, geometry)) return;
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const ImVec2 center(geometry.center.x, geometry.center.y);
    for (int axis = 0; axis < 3; ++axis) {
        const ImVec2 end(geometry.arrows[axis].x, geometry.arrows[axis].y);
        draw->AddLine(center, end, kAxisColors[axis], 3.0f);
        const float angle = std::atan2(end.y - center.y, end.x - center.x);
        constexpr float kHeadLength = 10.0f;
        constexpr float kHeadWidth = 5.0f;
        const ImVec2 tip = end;
        const ImVec2 base(end.x - std::cos(angle) * kHeadLength, end.y - std::sin(angle) * kHeadLength);
        const ImVec2 left(base.x + std::cos(angle + std::numbers::pi_v<float> * 0.5f) * kHeadWidth,
                          base.y + std::sin(angle + std::numbers::pi_v<float> * 0.5f) * kHeadWidth);
        const ImVec2 rightPoint(base.x - std::cos(angle + std::numbers::pi_v<float> * 0.5f) * kHeadWidth,
                                base.y - std::sin(angle + std::numbers::pi_v<float> * 0.5f) * kHeadWidth);
        draw->AddTriangleFilled(tip, left, rightPoint, kAxisColors[axis]);
        for (size_t i = 0; i < kRingSegments; ++i) {
            const Vec2& a = geometry.rings[axis][i];
            const Vec2& b = geometry.rings[axis][i + 1];
            draw->AddLine(ImVec2(a.x, a.y), ImVec2(b.x, b.y), kAxisColors[axis], 2.0f);
        }
    }
    draw->AddCircleFilled(center, 5.0f, IM_COL32(245, 245, 245, 255));
}

} // namespace rs::app
