#include "SceneryTransformGizmo.h"

#include "../../core/Epsilon.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
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
constexpr float kCenterPickRadiusPx = 9.0f;
constexpr float kCenterDrawRadiusPx = 5.0f;
constexpr float kLineWidthPx = 3.0f;
constexpr float kRingWidthPx = 2.0f;
constexpr float kHoverWidthScale = 1.8f;
constexpr float kHoverLighten = 0.55f;
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
    if (!GetViewModeTraits(ctx.viewMode).scenery || editor.sceneryPlacementMode ||
        editor.SelectedSceneEntities().empty()) return false;
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

using Hover = rs::editor::Editor::SceneEntityGizmoHoverState;

// 当たった部品を返す。中央の円を最優先し、次に矢印、最後にリングを見る。
Hover PickHandle(const GizmoGeometry& geometry, const Vec2& mouse) {
    Hover hover;
    if (Length2(Sub2(mouse, geometry.center)) <= kCenterPickRadiusPx) {
        hover.valid = true;
        hover.center = true;
        return hover;
    }
    float best = kArrowPickRadiusPx;
    for (int axis = 0; axis < 3; ++axis) {
        const float distance = DistanceToSegment(mouse, geometry.center, geometry.arrows[axis]);
        if (distance < best) { best = distance; hover.axisIndex = axis; hover.rotation = false; hover.valid = true; }
    }
    if (hover.valid) return hover;
    best = kRingPickRadiusPx;
    for (int axis = 0; axis < 3; ++axis) {
        for (size_t i = 0; i < kRingSegments; ++i) {
            const float distance = DistanceToSegment(mouse, geometry.rings[axis][i],
                                                     geometry.rings[axis][i + 1]);
            if (distance < best) { best = distance; hover.axisIndex = axis; hover.rotation = true; hover.valid = true; }
        }
    }
    return hover;
}

// 軸まわりの正の回転が、画面上で反時計回りに見えれば +1、時計回りなら -1。
// 座標系の手系や、軸が視点の手前を向くか奥を向くかで向きが変わるので、実際に投影して決める。
double ScreenRotationSign(const GizmoGeometry& geometry, const ViewProjResult& viewProj, UINT width,
                          UINT height, int axisIndex) {
    const Vec3& axis = geometry.axes[axisIndex];
    const Vec3 helper = std::abs(axis.y) < 0.9f ? Vec3(0.0f, 1.0f, 0.0f) : Vec3(1.0f, 0.0f, 0.0f);
    const Vec3 start = Vec3::Cross(axis, helper).Normalized() * (geometry.worldLength * kRingRadiusScale);
    const Vec3 turned = rs::world::RotateAroundAxis(start, axis, 0.5f);
    Vec2 a, b;
    if (!Project(geometry.pivot + start, viewProj, width, height, a) ||
        !Project(geometry.pivot + turned, viewProj, width, height, b)) return 1.0;
    const Vec2 ra = Sub2(a, geometry.center);
    const Vec2 rb = Sub2(b, geometry.center);
    // 画面のy軸は下向きなので、外積の符号が負なら見た目は反時計回り。
    const float cross = ra.x * rb.y - ra.y * rb.x;
    return cross < 0.0f ? 1.0 : -1.0;
}

// 視点に垂直で、基準点を通る平面とカーソルの光線の交点。
bool HitViewPlane(const EditorViewportContext& ctx, const ViewProjResult& viewProj, const Vec2& cursor,
                  const Vec3& planePoint, const Vec3& planeNormal, Vec3& out) {
    rs::core::Ray ray;
    if (!rs::core::ScreenToRay(cursor.x, cursor.y, viewProj.viewProj, ctx.width, ctx.height, ray)) return false;
    const float denom = Vec3::Dot(planeNormal, ray.dir);
    if (std::abs(denom) < 1e-6f) return false;
    const float t = Vec3::Dot(planeNormal, planePoint - ray.origin) / denom;
    if (t < 0.0f) return false;
    out = ray.origin + ray.dir * t;
    return true;
}

double WrapRadians(double angle) {
    while (angle > std::numbers::pi) angle -= 2.0 * std::numbers::pi;
    while (angle < -std::numbers::pi) angle += 2.0 * std::numbers::pi;
    return angle;
}

void ApplyDragPose(const rs::editor::Editor::SceneEntityGizmoDragState& drag,
                   rs::world::SceneRegistry& registry, const rs::world::TrackGraph& graph,
                   const Vec3& axis,
                   double delta, const Vec3& freeOffset) {
    for (const rs::world::SceneEntity& original : drag.before) {
        const rs::world::FreePlacement* initial = std::get_if<rs::world::FreePlacement>(&original.placement);
        if (initial == nullptr) continue;
        rs::world::FreePlacement next = *initial;
        if (drag.free) {
            next.position = initial->position + freeOffset;
        } else if (!drag.rotation) {
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
        editor.sceneEntityGizmoHover = {true, drag.free, drag.rotation, drag.axisIndex};
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
        Vec3 freeOffset{};
        if (drag.free) {
            Vec3 hit;
            if (HitViewPlane(ctx, viewProj, cursor, drag.startPlanePoint, drag.planeNormal, hit)) {
                freeOffset = hit - drag.startPlanePoint;
                if (ctx.input.MatchesPointer(rs::app::input::PointerAction::TransformSnap)) {
                    freeOffset.x = static_cast<float>(rs::app::SnapSceneryTransformDelta(freeOffset.x, 0.2));
                    freeOffset.y = static_cast<float>(rs::app::SnapSceneryTransformDelta(freeOffset.y, 0.2));
                    freeOffset.z = static_cast<float>(rs::app::SnapSceneryTransformDelta(freeOffset.z, 0.2));
                }
            }
        } else if (!drag.rotation) {
            const Vec2 offset = Sub2(cursor, drag.startMouse);
            delta = static_cast<double>(Dot2(offset, drag.screenAxis) * drag.worldPerPixel);
            if (ctx.input.MatchesPointer(rs::app::input::PointerAction::TransformSnap)) delta = rs::app::SnapSceneryTransformDelta(delta, 0.2);
        } else {
            const double current = std::atan2(-(static_cast<double>(cursor.y) - drag.screenCenter.y),
                                               static_cast<double>(cursor.x) - drag.screenCenter.x);
            delta = WrapRadians(current - drag.startScreenAngle) * drag.rotationSign;
            if (ctx.input.MatchesPointer(rs::app::input::PointerAction::TransformSnap)) {
                const double step = std::max(1.0, static_cast<double>(ctx.sceneryPlacementUi.yawStepDeg)) *
                                    std::numbers::pi / 180.0;
                delta = rs::app::SnapSceneryTransformDelta(delta, step);
            }
        }
        drag.currentDelta = delta;
        drag.currentFreeOffset = freeOffset;
        ApplyDragPose(drag, ctx.sceneRegistry, editor.graph, drag.axis, delta, freeOffset);
        if (ctx.input.ButtonReleased(ctx.input.ButtonFor(rs::app::input::PointerAction::Select))) {
            editor.CommitSceneEntityTransform(ctx.sceneRegistry, drag.before);
            drag = {};
        }
        return true;
    }

    editor.sceneEntityGizmoHover = {};
    if (!GetViewModeTraits(ctx.viewMode).scenery ||
        !ctx.input.AllowsPointer(rs::app::input::InputContext::Edit)) return false;
    GizmoGeometry geometry;
    if (!BuildGeometry(ctx, viewProj, geometry)) return false;
    const Hover hover = PickHandle(geometry, cursor);
    editor.sceneEntityGizmoHover = hover;
    if (!hover.valid || !ctx.input.ButtonClicked(ctx.input.ButtonFor(rs::app::input::PointerAction::Select)))
        return false;
    const int axisIndex = hover.axisIndex;
    Vec2 projectedAxis = Sub2(geometry.arrows[axisIndex], geometry.center);
    float projectedLength = Length2(projectedAxis);
    if (!hover.center && projectedLength <= rs::core::kGizmoProjectedLengthEpsPx) return false;
    if (hover.center) {
        // 視点に垂直な平面を、画面中央の光線の向きから決める。
        rs::core::Ray centerRay;
        if (!rs::core::ScreenToRay(static_cast<float>(ctx.width) * 0.5f, static_cast<float>(ctx.height) * 0.5f,
                                   viewProj.viewProj, ctx.width, ctx.height, centerRay)) return false;
        Vec3 hit;
        drag.planeNormal = centerRay.dir.Normalized();
        if (!HitViewPlane(ctx, viewProj, cursor, geometry.pivot, drag.planeNormal, hit)) return false;
        drag.startPlanePoint = hit;
        projectedAxis = {1.0f, 0.0f};
        projectedLength = 1.0f;
    }
    drag.active = true;
    drag.free = hover.center;
    drag.rotation = hover.rotation;
    drag.rotationSign = hover.rotation ? ScreenRotationSign(geometry, viewProj, ctx.width, ctx.height, axisIndex) : 1.0;
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
    const Hover hover = ctx.editor.sceneEntityGizmoHover;
    const auto lighten = [](uint32_t color) {
        const auto mix = [](uint32_t channel) {
            return static_cast<uint32_t>(static_cast<float>(channel) + (255.0f - static_cast<float>(channel)) * kHoverLighten);
        };
        return IM_COL32(mix((color >> IM_COL32_R_SHIFT) & 0xFF), mix((color >> IM_COL32_G_SHIFT) & 0xFF),
                        mix((color >> IM_COL32_B_SHIFT) & 0xFF), 255);
    };
    for (int axis = 0; axis < 3; ++axis) {
        const bool arrowHot = hover.valid && !hover.center && !hover.rotation && hover.axisIndex == axis;
        const bool ringHot = hover.valid && !hover.center && hover.rotation && hover.axisIndex == axis;
        const uint32_t arrowColor = arrowHot ? lighten(kAxisColors[axis]) : kAxisColors[axis];
        const uint32_t ringColor = ringHot ? lighten(kAxisColors[axis]) : kAxisColors[axis];
        const ImVec2 end(geometry.arrows[axis].x, geometry.arrows[axis].y);
        draw->AddLine(center, end, arrowColor, arrowHot ? kLineWidthPx * kHoverWidthScale : kLineWidthPx);
        const float angle = std::atan2(end.y - center.y, end.x - center.x);
        constexpr float kHeadLength = 10.0f;
        constexpr float kHeadWidth = 5.0f;
        const ImVec2 tip = end;
        const ImVec2 base(end.x - std::cos(angle) * kHeadLength, end.y - std::sin(angle) * kHeadLength);
        const ImVec2 left(base.x + std::cos(angle + std::numbers::pi_v<float> * 0.5f) * kHeadWidth,
                          base.y + std::sin(angle + std::numbers::pi_v<float> * 0.5f) * kHeadWidth);
        const ImVec2 rightPoint(base.x - std::cos(angle + std::numbers::pi_v<float> * 0.5f) * kHeadWidth,
                                base.y - std::sin(angle + std::numbers::pi_v<float> * 0.5f) * kHeadWidth);
        draw->AddTriangleFilled(tip, left, rightPoint, arrowColor);
        for (size_t i = 0; i < kRingSegments; ++i) {
            const Vec2& a = geometry.rings[axis][i];
            const Vec2& b = geometry.rings[axis][i + 1];
            draw->AddLine(ImVec2(a.x, a.y), ImVec2(b.x, b.y), ringColor,
                          ringHot ? kRingWidthPx * kHoverWidthScale : kRingWidthPx);
        }
    }
    const rs::editor::Editor::SceneEntityGizmoDragState& drag = ctx.editor.sceneEntityGizmoDrag;
    if (drag.active) {
        constexpr const char* kAxisNames[3] = {"X", "Y", "Z"};
        char text[96];
        if (drag.free) {
            std::snprintf(text, sizeof(text), "X %+.2f  Y %+.2f  Z %+.2f m", drag.currentFreeOffset.x,
                          drag.currentFreeOffset.y, drag.currentFreeOffset.z);
        } else if (drag.rotation) {
            std::snprintf(text, sizeof(text), "%s %+.1f deg", kAxisNames[drag.axisIndex],
                          drag.currentDelta * 180.0 / std::numbers::pi);
        } else {
            std::snprintf(text, sizeof(text), "%s %+.2f m", kAxisNames[drag.axisIndex], drag.currentDelta);
        }
        const ImVec2 textPos(center.x + 16.0f, center.y + 14.0f);
        const ImVec2 size = ImGui::CalcTextSize(text);
        draw->AddRectFilled(ImVec2(textPos.x - 4.0f, textPos.y - 2.0f),
                            ImVec2(textPos.x + size.x + 4.0f, textPos.y + size.y + 2.0f),
                            IM_COL32(20, 20, 20, 190), 3.0f);
        draw->AddText(textPos, IM_COL32(255, 255, 255, 255), text);
    }
    const bool centerHot = hover.valid && hover.center;
    draw->AddCircleFilled(center, centerHot ? kCenterDrawRadiusPx * kHoverWidthScale : kCenterDrawRadiusPx,
                          centerHot ? IM_COL32(255, 235, 120, 255) : IM_COL32(245, 245, 245, 255));
}

} // namespace rs::app
