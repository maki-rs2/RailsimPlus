//
// ViewportWireEditInput.cpp - 架線編集モード(接続点を2つ結ぶ方式)の入力
//
// 1点目のクリック、2点目での確定、半透明の架線プレビュー、径間のホバー・選択、選択した径間の削除。
// 接続点と径間のキャッシュ(ctx.wireEditCache)はメッシュ再構築時に作り直される
// (ViewportRailMeshBuild.cpp)。
//
#include "ViewportInputInternal.h"

#include "OverlayLineTuning.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "../../core/Math.h"
#include "../../editor/tool/Editor.h"
#include "../scene/PlacementMaterialize.h"
#include "../scene/SceneRenderer.h"
#include "ViewportRailMeshBuild.h"

namespace rs::app {

namespace {

constexpr const char* kNoLineMessage = "接続できません: この柱の区間に架線プラグインがありません";
constexpr const char* kBothLinePointsMessage =
    "接続できません: Structの接続点どうしは結べません(柱の側の架線プラグインが必要です)";

// クリック位置から半径内で最も近い接続点の添字。excludeの点は候補にしない。
std::optional<std::size_t> PickWirePoint(const std::vector<WireConnectPoint>& points,
                                         const rs::core::Mat4& viewProj, UINT width, UINT height,
                                         rs::app::input::PointerPos mouse,
                                         const rs::editor::WirePointRef* exclude) {
    float bestDist = OverlayLineTuning::Embedded().wirePointPickRadiusPx;
    std::optional<std::size_t> best;
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (exclude != nullptr && rs::editor::SameWirePoint(points[i].ref, *exclude)) continue;
        const rs::core::ProjectedPoint pp =
            rs::core::ProjectToScreen(points[i].pos, viewProj, width, height);
        if (!pp.Visible()) continue;
        const float dx = mouse.x - pp.x;
        const float dy = mouse.y - pp.y;
        const float dist = std::sqrt(dx * dx + dy * dy);
        if (dist <= bestDist) {
            bestDist = dist;
            best = i;
        }
    }
    return best;
}

// 径間(pos1-pos2のスクリーン投影線分)のうち、クリック位置から半径内で最も近いものの添字。
std::optional<std::size_t> PickWireSpan(const std::vector<WireSpanCacheEntry>& spans,
                                        const rs::core::Mat4& viewProj, UINT width, UINT height,
                                        rs::app::input::PointerPos mouse) {
    float bestDist = OverlayLineTuning::Embedded().wireSpanPickRadiusPx;
    std::optional<std::size_t> best;
    for (std::size_t i = 0; i < spans.size(); ++i) {
        const rs::core::ProjectedPoint ppA =
            rs::core::ProjectToScreen(spans[i].pos1, viewProj, width, height);
        const rs::core::ProjectedPoint ppB =
            rs::core::ProjectToScreen(spans[i].pos2, viewProj, width, height);
        if (!ppA.Visible() || !ppB.Visible()) continue;
        const float dx = ppB.x - ppA.x;
        const float dy = ppB.y - ppA.y;
        const float t = static_cast<float>(rs::core::ClosestSegmentParameter(
            {ppA.x, ppA.y, 0.0}, {ppB.x, ppB.y, 0.0}, {mouse.x, mouse.y, 0.0},
            rs::core::kScreenSegmentLengthSquaredEpsPx2));
        const float ddx = mouse.x - (ppA.x + t * dx);
        const float ddy = mouse.y - (ppA.y + t * dy);
        const float dist = std::sqrt(ddx * ddx + ddy * ddy);
        if (dist <= bestDist) {
            bestDist = dist;
            best = i;
        }
    }
    return best;
}

const WireConnectPoint* FindPoint(const WireEditCache& cache, const rs::editor::WirePointRef& ref) {
    for (const WireConnectPoint& point : cache.points) {
        if (rs::editor::SameWirePoint(point.ref, ref)) return &point;
    }
    return nullptr;
}

// 2点を結ぶ架線プラグイン。柱の点(区間に割り当てられた架線)を優先し、
// Structの接続点どうしなら空(結べない)。
// ブラウザで架線を選んでいればそれを使い、無ければ結ぶ柱の組から決める。
const std::string& WireLineFolderOf(const rs::editor::Editor& editor, const WireConnectPoint& first,
                                    const WireConnectPoint& second) {
    if (!editor.wireLineFolder.empty()) return editor.wireLineFolder;
    return !first.lineFolderUtf8.empty() ? first.lineFolderUtf8 : second.lineFolderUtf8;
}

// Structの接続点は支持点そのものに印があるので、架線の基準点はプラグインのHeight分だけ下げる
// (柱の接続点は印の位置がすでに基準点)。
rs::core::Vec3 WireBasePosOf(const WireConnectPoint& point, const TrackPluginEntry& lineEntry) {
    if (!rs::editor::IsStructLinePoint(point.ref)) return point.pos;
    return point.pos - point.up * lineEntry.def.line.height;
}

// 2点の柱を実体化するスナップショット(未実体化のedge×folderだけが中身を持つ)。
// Structの接続点は導出した柱なので実体化するものが無い。
std::vector<rs::world::MaterializeSnapshot> BuildSnapshotsForPoints(
    const EditorViewportContext& ctx, const rs::editor::WirePointRef& pointA,
    const rs::editor::WirePointRef& pointB) {
    const rs::editor::Editor& editor = ctx.editor;
    std::vector<rs::world::MaterializeSnapshot> snapshots;
    if (!rs::editor::IsStructLinePoint(pointA)) {
        snapshots = BuildMaterializeSnapshotsForEdit(
            editor.graph, ctx.selector.TrackPluginEntries(), {pointA.edge}, pointA.poleFolderUtf8);
    }
    const bool sameAsA = !rs::editor::IsStructLinePoint(pointA) && pointB.edge == pointA.edge &&
                         pointB.poleFolderUtf8 == pointA.poleFolderUtf8;
    if (!rs::editor::IsStructLinePoint(pointB) && !sameAsA) {
        std::vector<rs::world::MaterializeSnapshot> more = BuildMaterializeSnapshotsForEdit(
            editor.graph, ctx.selector.TrackPluginEntries(), {pointB.edge}, pointB.poleFolderUtf8);
        for (rs::world::MaterializeSnapshot& snapshot : more) snapshots.push_back(std::move(snapshot));
    }
    return snapshots;
}

// 1点目からの半透明の架線プレビュー。終点は乗っている接続点、無ければカーソル下の地面(Y=0)。
void UpdatePreview(const EditorViewportContext& ctx, const rs::app::ViewProjResult& vpr,
                   rs::app::input::PointerPos mouse, const WireConnectPoint& first,
                   const WireConnectPoint* hovered) {
    // 架線プラグインは1点目の柱から取る。1点目がStructの接続点なら、乗っている2点目の柱から取る。
    const TrackPluginEntry* lineEntry = nullptr;
    rs::core::Vec3 endPos;
    rs::core::Vec3 endUp = first.up;
    if (hovered != nullptr) {
        lineEntry = ctx.selector.FindTrackPluginEntry(WireLineFolderOf(ctx.editor, first, *hovered));
        if (lineEntry != nullptr) {
            endPos = WireBasePosOf(*hovered, *lineEntry);
            endUp = hovered->up;
        }
    } else {
        lineEntry = ctx.selector.FindTrackPluginEntry(
            !ctx.editor.wireLineFolder.empty() ? ctx.editor.wireLineFolder : first.lineFolderUtf8);
        rs::core::Ray ray;
        if (!rs::core::ScreenToRay(mouse.x, mouse.y, vpr.viewProj, ctx.width, ctx.height, ray) ||
            !rs::core::RayHitPlaneY(ray, 0.0f, endPos)) {
            lineEntry = nullptr;
        }
    }
    if (lineEntry == nullptr) {
        ctx.sceneRenderer.ClearMeshPlacementPreview(ctx.app);
        return;
    }
    const rs::core::Vec3 startPos = WireBasePosOf(first, *lineEntry);
    // 2点の位置か架線プラグインが変わったときだけメッシュを作り直す。
    char key[256];
    std::snprintf(key, sizeof(key), "%s|%.3f,%.3f,%.3f|%.3f,%.3f,%.3f", lineEntry->folderUtf8.c_str(),
                  startPos.x, startPos.y, startPos.z, endPos.x, endPos.y, endPos.z);
    ctx.sceneRenderer.SetMeshPlacementPreview(ctx.app, key, [&]() {
        return BuildWireSpanMesh(*lineEntry, startPos, first.up, endPos, endUp, ctx.state.hangerModels);
    });
}

void ConnectPoints(const EditorViewportContext& ctx, const WireConnectPoint& first,
                   const WireConnectPoint& second) {
    rs::editor::Editor& editor = ctx.editor;
    const std::string& lineFolder = WireLineFolderOf(editor, first, second);
    if (lineFolder.empty() && rs::editor::IsStructLinePoint(first.ref) &&
        rs::editor::IsStructLinePoint(second.ref)) {
        editor.NoteOperationRejected(kBothLinePointsMessage);
        return;
    }
    if (ctx.selector.FindTrackPluginEntry(lineFolder) == nullptr) {
        editor.NoteOperationRejected(kNoLineMessage);
        return;
    }
    if (editor.ConnectWireSpan(first.ref, second.ref, lineFolder,
                               BuildSnapshotsForPoints(ctx, first.ref, second.ref))) {
        editor.ClearOperationRejected();
    }
}

// 1点目を覚えている間の入力。2点目の接続点をクリックしたら結んで1点目の記憶を消す。
void HandleSecondPoint(const EditorViewportContext& ctx, const rs::app::ViewProjResult& vpr,
                       rs::app::input::PointerPos mouse, bool leftClickForEditor,
                       const WireConnectPoint& first, const WireConnectPoint* hovered) {
    rs::editor::Editor& editor = ctx.editor;
    UpdatePreview(ctx, vpr, mouse, first, hovered);
    if (!leftClickForEditor || hovered == nullptr) return;
    ConnectPoints(ctx, first, *hovered);
    editor.wireFirstPoint.reset();
    ctx.sceneRenderer.ClearMeshPlacementPreview(ctx.app);
}

// 1点目が無い間の入力。接続点を優先して1点目にし、接続点が無ければ径間のホバー・選択を行う。
void HandleFirstPointOrSpan(const EditorViewportContext& ctx, const rs::app::ViewProjResult& vpr,
                            rs::app::input::PointerPos mouse, bool leftClickForEditor,
                            const WireConnectPoint* hovered) {
    rs::editor::Editor& editor = ctx.editor;
    ctx.sceneRenderer.ClearMeshPlacementPreview(ctx.app);
    editor.wireHoveredSpan.reset();
    if (hovered == nullptr) {
        const std::optional<std::size_t> spanIndex =
            PickWireSpan(ctx.wireEditCache.spans, vpr.viewProj, ctx.width, ctx.height, mouse);
        if (spanIndex.has_value()) editor.wireHoveredSpan = ctx.wireEditCache.spans[*spanIndex].ref;
    }
    if (!leftClickForEditor) return;
    if (hovered != nullptr) {
        editor.wireFirstPoint = hovered->ref;
        editor.ClearWireSpanSelection();
    } else if (editor.wireHoveredSpan.has_value()) {
        editor.wireSelectedSpan = editor.wireHoveredSpan;
        editor.wireSelectedRevision = editor.revision;
    } else {
        editor.wireSelectedSpan.reset();
    }
}

} // namespace

void HandleWireSpanModeInput(const EditorViewportContext& ctx, const rs::app::ViewProjResult& vpr,
                             rs::app::input::PointerPos mouse, bool leftClickForEditor) {
    rs::editor::Editor& editor = ctx.editor;
    const WireEditCache& cache = ctx.wireEditCache;

    // 配置物の構成が変わったら、径間の選択は古くなっているので捨てる。
    if (editor.wireSelectedSpan.has_value() && editor.wireSelectedRevision != editor.revision) {
        editor.wireSelectedSpan.reset();
    }
    // 1点目の柱がもう無い(削除・シーン切替)なら忘れる。
    const WireConnectPoint* first =
        editor.wireFirstPoint.has_value() ? FindPoint(cache, *editor.wireFirstPoint) : nullptr;
    if (first == nullptr) editor.wireFirstPoint.reset();

    // 取り消しは、右ボタンを短く押して離したときだけ(ドラッグ=視点操作では解除しない)。
    if (ctx.input.ButtonTapped(ctx.input.ButtonFor(rs::app::input::PointerAction::Cancel))) {
        editor.wireFirstPoint.reset();
        editor.ClearWireSpanSelection();
        first = nullptr;
    }

    const std::optional<std::size_t> pointIndex =
        PickWirePoint(cache.points, vpr.viewProj, ctx.width, ctx.height, mouse,
                      first != nullptr ? &first->ref : nullptr);
    const WireConnectPoint* hovered = pointIndex.has_value() ? &cache.points[*pointIndex] : nullptr;
    if (hovered != nullptr) editor.wireHoveredPoint = hovered->ref;
    else editor.wireHoveredPoint.reset();

    if (first != nullptr) {
        editor.ClearWireSpanSelection();
        HandleSecondPoint(ctx, vpr, mouse, leftClickForEditor, *first, hovered);
    } else {
        HandleFirstPointOrSpan(ctx, vpr, mouse, leftClickForEditor, hovered);
    }
}

bool DeleteSelectedWireSpan(const EditorViewportContext& ctx) {
    rs::editor::Editor& editor = ctx.editor;
    if (!editor.wireEditMode || !editor.wireSelectedSpan.has_value()) return false;
    const rs::editor::WireSpanRef span = *editor.wireSelectedSpan;
    editor.DisconnectWireSpan(span.a, span.b, BuildSnapshotsForPoints(ctx, span.a, span.b));
    editor.ClearWireSpanSelection();
    return true;
}

} // namespace rs::app
