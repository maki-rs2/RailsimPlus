//
// EditorPicking.cpp - Editorのスクリーン投影・逆投影・ピッキング(editor.cppから分離)
//
// 状態非依存の座標変換と最近傍node/edge判定。Editorクラスのメソッド実装を
// 担う翻訳単位で、宣言は editor.h にある。
//
#include "../../core/Epsilon.h"
#include "Editor.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "../../world/alignment/TrackTraversal.h"
#include "../../world/alignment/TrackQuery.h"

namespace rs::editor {

using rs::core::Mat4;
using rs::core::Vec3;
using rs::world::EvaluateAt;
using rs::world::Guid;
using rs::world::IsCenterlineOnlyEdge;
using rs::world::NilGuid;
using rs::world::TrackEdge;
using rs::world::TrackFrame;

namespace {

// スクリーン空間での点-線分距離(a→b上の最近点パラメータtと距離d)。
struct SegmentProjection {
    float t = 0.0f;
    float d = 0.0f;
};

SegmentProjection ProjectPointToSegment(float ax, float ay, float bx, float by, float px, float py) {
    const float abx = bx - ax, aby = by - ay;
    const float t = static_cast<float>(rs::core::ClosestSegmentParameter(
        {ax, ay, 0.0}, {bx, by, 0.0}, {px, py, 0.0}, rs::core::kScreenSegmentLengthSquaredEpsPx2));
    const float cx = ax + abx * t, cy = ay + aby * t;
    const float dx = px - cx, dy = py - cy;
    return SegmentProjection{t, std::sqrt(dx * dx + dy * dy)};
}

} // namespace

// ============================== ピッキング ==============================

Vec3 Editor::NodeRailTopPos(const rs::world::TrackNode& node) const {
    float lift = 0.0f;
    for (const auto& link : node.links) {
        auto eit = graph.edges.find(link.edge);
        if (eit != graph.edges.end()) lift = std::max(lift, eit->second.railTopLift);
    }
    return node.pos + node.up * lift;
}

Guid Editor::PickNode(const rs::core::Ray& ray, float screenX, float screenY, const Mat4& viewProj, uint32_t width,
                      uint32_t height, float pxRadius) const {
    Guid best = NilGuid();
    float bestDistance = 0.0f;
    for (const auto& kv : graph.nodes) {
        const Vec3 point = NodeRailTopPos(kv.second);
        const float rayT = std::max(0.0f, Vec3::Dot(point - ray.origin, ray.dir));
        const float distance = (point - (ray.origin + ray.dir * rayT)).Length();
        const float radius = rs::core::WorldRadiusForPixels(viewProj, width, height, screenX,
                                                            screenY, point, pxRadius);
        if (distance <= radius && (best == NilGuid() || distance < bestDistance)) {
            bestDistance = distance;
            best = kv.first;
        }
    }
    return best;
}

Guid Editor::PickEdge(const rs::core::Ray& ray, float screenX, float screenY, const Mat4& viewProj, uint32_t width,
                     uint32_t height, float pxRadius, bool skipCenterlineOnly) const {
    Guid best = NilGuid();
    float bestDistance = 0.0f;
    for (const auto& kv : graph.edges) {
        const TrackEdge& e = kv.second;
        // centerlineOnly(複線中心線)は既定ではtrackPlugins編集のため選択可能にする
        // (railTopLift等の実レール専用値は0のままで
        // 差し支えない=中心線自体の見た目はEditorViewport.cppの専用ループが持つ)。
        if (skipCenterlineOnly && rs::world::IsCenterlineOnlyEdge(e)) continue;
        if (e.spline.size() < 2) continue;
        for (size_t index = 1; index < e.spline.size(); ++index) {
            const Vec3 a = e.spline[index - 1].pos + e.spline[index - 1].up * e.railTopLift;
            const Vec3 b = e.spline[index].pos + e.spline[index].up * e.railTopLift;
            float rayT = 0.0f, segmentT = 0.0f, distance = 0.0f;
            if (rs::core::RayClosestPointOnSegment(ray, a, b, rayT, segmentT, distance)) {
                const Vec3 point = a + (b - a) * segmentT;
                const float radius = rs::core::WorldRadiusForPixels(viewProj, width, height,
                    screenX, screenY, point, pxRadius);
                if (distance <= radius && (best == NilGuid() || distance < bestDistance)) {
                    bestDistance = distance;
                    best = kv.first;
                }
            }
        }
    }
    return best;
}

namespace {

// スクリーン上の線分a-bが、矩形[minX,maxX]x[minY,maxY]と交わるか(Liang-Barsky)。
bool SegmentIntersectsRect(float ax, float ay, float bx, float by, float minX, float minY,
                           float maxX, float maxY) {
    float t0 = 0.0f, t1 = 1.0f;
    const float dx = bx - ax, dy = by - ay;
    const float p[4] = {-dx, dx, -dy, dy};
    const float q[4] = {ax - minX, maxX - ax, ay - minY, maxY - ay};
    for (int i = 0; i < 4; ++i) {
        if (std::fabs(p[i]) < rs::core::kScreenParallelEpsPx) {
            if (q[i] < 0.0f) return false;
            continue;
        }
        const float t = q[i] / p[i];
        if (p[i] < 0.0f) t0 = std::max(t0, t);
        else t1 = std::min(t1, t);
        if (t0 > t1) return false;
    }
    return true;
}

} // namespace

std::vector<Guid> Editor::PickEdgesInRect(float x0, float y0, float x1, float y1,
                                          const Mat4& viewProj, uint32_t width,
                                          uint32_t height) const {
    const float minX = std::min(x0, x1), maxX = std::max(x0, x1);
    const float minY = std::min(y0, y1), maxY = std::max(y0, y1);
    std::vector<Guid> result;
    for (const auto& kv : graph.edges) {
        const TrackEdge& e = kv.second;
        // 複線中心線は実体を持たないので、まとめて選ぶ対象にしない。
        if (IsCenterlineOnlyEdge(e) || e.spline.size() < 2) continue;
        std::optional<rs::core::ProjectedPoint> previous;
        for (const rs::world::TrackNodeSample& sample : e.spline) {
            const rs::core::ProjectedPoint current = rs::core::ProjectToScreen(
                sample.pos + sample.up * e.railTopLift, viewProj, width, height);
            if (!current.Visible()) {
                previous.reset();
                continue;
            }
            if (previous && SegmentIntersectsRect(previous->x, previous->y, current.x, current.y,
                                                  minX, minY, maxX, maxY)) {
                result.push_back(kv.first);
                break;
            }
            previous = current;
        }
    }
    return result;
}

Guid Editor::PickEdgeS(const rs::core::Ray& ray, float screenX, float screenY, const Mat4& viewProj, uint32_t width,
                       uint32_t height, double& outS, float pxRadius,
                       bool skipCenterlineOnly) const {
    Guid best = NilGuid();
    float bestDistance = 0.0f;
    for (const auto& kv : graph.edges) {
        const TrackEdge& e = kv.second;
        if (skipCenterlineOnly && rs::world::IsCenterlineOnlyEdge(e)) continue;
        if (e.spline.size() < 2) continue;
        for (size_t index = 1; index < e.spline.size(); ++index) {
            const rs::world::TrackNodeSample& previous = e.spline[index - 1];
            const rs::world::TrackNodeSample& sample = e.spline[index];
            const Vec3 a = previous.pos + previous.up * e.railTopLift;
            const Vec3 b = sample.pos + sample.up * e.railTopLift;
            float rayT = 0.0f, segmentT = 0.0f, distance = 0.0f;
            if (rs::core::RayClosestPointOnSegment(ray, a, b, rayT, segmentT, distance)) {
                const Vec3 point = a + (b - a) * segmentT;
                const float radius = rs::core::WorldRadiusForPixels(viewProj, width, height,
                    screenX, screenY, point, pxRadius);
                if (distance <= radius && (best == NilGuid() || distance < bestDistance)) {
                    bestDistance = distance;
                    best = kv.first;
                    outS = static_cast<double>(previous.s) +
                        (static_cast<double>(sample.s) - static_cast<double>(previous.s)) * segmentT;
                }
            }
        }
    }
    return best;
}

bool Editor::ProjectSOnEdge(const Guid& edge, const rs::core::Ray& ray, float screenX, float screenY, const Mat4& viewProj,
                            uint32_t width, uint32_t height, double& outS) const {
    const auto it = graph.edges.find(edge);
    if (it == graph.edges.end()) return false;
    const TrackEdge& e = it->second;
    if (e.spline.size() < 2) return false;

    // 半径制限なし(ドラッグ中プレビュー用、Editor.hのコメント参照): マウスが
    // edgeの投影から一時的に離れても、最近傍点への追従を止めない。区間分割の境界ドラッグ
    // (HandleSectionSplitModeInput)がゲート無しの追従を前提にしているため。
    bool found = false;
    float bestDist = 0.0f;
    for (size_t index = 1; index < e.spline.size(); ++index) {
        const rs::world::TrackNodeSample& previous = e.spline[index - 1];
        const rs::world::TrackNodeSample& sample = e.spline[index];
        const Vec3 a = previous.pos + previous.up * e.railTopLift;
        const Vec3 b = sample.pos + sample.up * e.railTopLift;
        float rayT = 0.0f, segmentT = 0.0f, distance = 0.0f;
        if (rs::core::RayClosestPointOnSegment(ray, a, b, rayT, segmentT, distance)) {
            if (!found || distance < bestDist) {
                bestDist = distance;
                found = true;
                outS = static_cast<double>(previous.s) +
                    (static_cast<double>(sample.s) - static_cast<double>(previous.s)) * segmentT;
            }
        }
    }
    return found;
}

Editor::PlacementEdgeProjection Editor::ProjectPlacementOffsetOnEdgeAtPoint(
    const Guid& edge, const Vec3& hit, float verticalOffsetM, int dir) const {
    PlacementEdgeProjection result;
    const auto it = graph.edges.find(edge);
    if (it == graph.edges.end()) return result;
    const TrackEdge& e = it->second;
    if (e.spline.size() < 2) return result;

    // ResolveWorldMatrixのorigin計算(frame.pos + right*lateral + frame.up*
    // verticalOffsetM)と同じ高さ基準に揃える(railTopLiftではなくverticalOffsetM。
    // railTopLiftだと、trolleyAlt+height分のverticalOffsetMを持つPoleで
    // planeYと探索用polylineの高さが食い違い、レイ-平面交点が最近傍探索の基準線から外れる)。
    const rs::world::NearestSplineHit nearest = rs::world::FindNearestSplinePosition(
        e, hit, verticalOffsetM, rs::core::kPlacementSegmentLengthSquaredEpsM2);
    if (!nearest.found) return result;

    // ResolveWorldMatrix/Rs2Import.cpp::AppendImportedPluginLinksと同じ規約
    // (横オフセットは yawOffsetDeg で回す前の right=cross(frame.up,frame.tangent) で測る)。
    // ここは呼び出し側が渡すdirを使う
    // (ResolveWorldMatrixが実際の描画で使うdirと揃えないと、dir=-1の候補edgeで
    // lateralOffsetMの左右が逆転する)。
    const TrackFrame frame = rs::world::EvaluateAtRest(e, nearest.s, dir);
    const Vec3 right = Vec3::Cross(frame.up, frame.tangent).Normalized();
    const Vec3 residual = hit - (frame.pos + frame.up * verticalOffsetM);
    result.found = true;
    result.s = nearest.s;
    result.lateralOffsetM = Vec3::Dot(residual, right);
    result.dist = static_cast<float>(nearest.distanceM);
    return result;
}

bool Editor::ProjectPlacementOffsetOnEdge(const Guid& edge, float screenX, float screenY,
                                          const Mat4& viewProj, uint32_t width, uint32_t height,
                                          float planeY, float verticalOffsetM, double& outS,
                                          float& outLateralOffsetM) const {
    rs::core::Ray ray;
    if (!rs::core::ScreenToRay(screenX, screenY, viewProj, width, height, ray)) return false;
    Vec3 hit;
    if (!rs::core::RayHitPlaneY(ray, planeY, hit)) return false;

    const PlacementEdgeProjection proj = ProjectPlacementOffsetOnEdgeAtPoint(edge, hit, verticalOffsetM);
    if (!proj.found) return false;
    outS = proj.s;
    outLateralOffsetM = proj.lateralOffsetM;
    return true;
}

void Editor::UpdateHover(const rs::core::Ray& ray, float screenX, float screenY, const Mat4& viewProj, uint32_t width,
                         uint32_t height) {
    // 接線ハンドルの当たり判定を先に行う(ノード本体判定より優先)。ハンドルの
    // 判定円がノード本体の判定円と重なるケース(ズームアウト時等)で、ノード側が
    // 誤ってヒットしないようにするため。
    hoveredTangentHandle = false;
    hoveredTangentHandleSide = 0;
    if (selectedNode != NilGuid() && CanMoveNode(selectedNode)) {
        const auto nit = graph.nodes.find(selectedNode);
        // 接線ハンドルは次数1〜2のノードだけ(分岐の接線は変えられない)。
        if (nit != graph.nodes.end() && nit->second.links.size() <= 2) {
            const Vec3 tangent = nit->second.tangent;
            const float len = tangent.Length();
            if (len > rs::core::kDegenerateVectorEps) {
                const Vec3 basePos = NodeRailTopPos(nit->second);
                const Vec3 unitTangent = tangent * (1.0f / len);
                const bool degreeTwo = nit->second.links.size() == 2;
                const int sides[2] = {+1, degreeTwo ? -1 : 0};
                float bestDistance = 0.0f;
                for (const int side : sides) {
                    if (side == 0) continue;
                    const Vec3 handleTip = basePos + unitTangent * (kTangentHandleLengthM * static_cast<float>(side));
                    const float rayT = std::max(0.0f, Vec3::Dot(handleTip - ray.origin, ray.dir));
                    const float distance = (handleTip - (ray.origin + ray.dir * rayT)).Length();
                    const float radius = rs::core::WorldRadiusForPixels(viewProj, width, height, screenX,
                                                                        screenY, handleTip, 6.0f);
                    if (distance <= radius && (hoveredTangentHandleSide == 0 || distance < bestDistance)) {
                        bestDistance = distance;
                        hoveredTangentHandleSide = side;
                    }
                }
                hoveredTangentHandle = (hoveredTangentHandleSide != 0);
            }
        }
    }

    // ハンドルにヒットした場合はノード本体をヒット対象から外す(優先順位付け)。
    hoveredNode = hoveredTangentHandle ? NilGuid() : PickNode(ray, screenX, screenY, viewProj, width, height);
    if (!(hoveredNode != NilGuid())) {
        hoveredEdge = PickEdge(ray, screenX, screenY, viewProj, width, height);
    } else {
        hoveredEdge = NilGuid();
    }
}

} // namespace rs::editor
