//
// PlacementDragInput.cpp - PlacementDragInput.h の実装
//
// 配置物(Pier/Pole)のホバー/選択/ドラッグ処理。
//
#include "PlacementDragInput.h"

#include "../../core/Epsilon.h"

#include <cfloat>
#include <cmath>
#include <vector>

#include "../../core/Math.h"
#include "../../editor/tool/Editor.h"
#include "../../world/scene/ScenePlacement.h"
#include "ViewportInput.h"
#include "../scene/PlacementMaterialize.h"

namespace rs::app {

namespace {

// クロスエッジ移動(個別移動時に隣のedgeへ移る)の候補1件。selfCandidate(現在ドラッグ中のedge)は常に含み、dirは現在の
// g_editor.placementDragUi.dirをそのまま使う(hopしていなければdir不変)。隣接候補は
// currentEdgeのnodeA/nodeB各端が「通過点」(links.size()==2、TrackChain.cppの
// BuildEdgeChainsが分岐/行き止まりでチェーンを終端するのと同じ規約)の場合のみ
// 追加し、dirはWalkChainDirection(TrackChain.cpp)と同じ式
// (相手edgeの自edge接続端==nodeA?+1:-1)×placementDragUi.originDir で求める
// (originDirは経路全体で不変)。
// centerlineOnly種別が食い違うedge(実レール⇔複線中心線)は候補から除外する
// (TrackNum>1のPoleは中心線edgeにアンカーする既存規約=ResolveCenterlineEdgeForを
// 崩さないため)。
struct PlacementDragCandidate {
    rs::world::Guid edge = rs::world::NilGuid();
    int dir = +1;
};

void AppendNeighborCandidate(const rs::world::TrackGraph& graph, const rs::world::Guid& atNode,
                             const rs::world::Guid& cameFromEdge, bool wantCenterlineOnly,
                             int originDir, std::vector<PlacementDragCandidate>& out) {
    const auto nodeIt = graph.nodes.find(atNode);
    if (nodeIt == graph.nodes.end()) return;
    const rs::world::TrackNode& node = nodeIt->second;
    if (node.links.size() != 2) return; // 分岐/行き止まりでは自動で跨がない(v1スコープ)
    for (const rs::world::TrackNode::Link& link : node.links) {
        if (link.edge == cameFromEdge || link.edge == rs::world::NilGuid()) continue;
        const auto edgeIt = graph.edges.find(link.edge);
        if (edgeIt == graph.edges.end()) continue;
        if (edgeIt->second.centerlineOnly != wantCenterlineOnly) continue;
        if (edgeIt->second.spline.size() < 2) continue;
        const int dir = (link.end == 0 ? +1 : -1) * originDir;
        out.push_back({link.edge, dir});
        return; // このnodeの通過先は1本のみ(links.size()==2で確定済み)
    }
}

std::vector<PlacementDragCandidate> BuildPlacementDragCandidates(
    const rs::world::TrackGraph& graph, const rs::world::Guid& currentEdge, int currentDir,
    int originDir) {
    std::vector<PlacementDragCandidate> out;
    out.push_back({currentEdge, currentDir});
    const auto edgeIt = graph.edges.find(currentEdge);
    if (edgeIt == graph.edges.end()) return out;
    const bool wantDummy = edgeIt->second.centerlineOnly;
    AppendNeighborCandidate(graph, edgeIt->second.nodeA, currentEdge, wantDummy, originDir, out);
    AppendNeighborCandidate(graph, edgeIt->second.nodeB, currentEdge, wantDummy, originDir, out);
    return out;
}

// ドラッグ中の毎フレーム更新(プレビュー更新+マウスアップでの確定)。
void UpdatePlacementDrag(const EditorViewportContext& ctx, const rs::app::ViewProjResult& vpr,
                         rs::app::input::PointerPos mouse) {
    rs::editor::Editor& gEditor = ctx.editor;
    const rs::app::input::InputSource& input = ctx.input;
    const UINT gCurWidth = ctx.width;
    const UINT gCurHeight = ctx.height;
    rs::core::Ray ray;
    rs::core::Vec3 hit;
    if (rs::core::ScreenToRay(mouse.x, mouse.y, vpr.viewProj, gCurWidth, gCurHeight, ray) &&
        rs::core::RayHitPlaneY(ray, gEditor.placementDragUi.planeY, hit)) {
        const std::vector<PlacementDragCandidate> candidates = BuildPlacementDragCandidates(
            gEditor.graph, gEditor.placementDragUi.edge, gEditor.placementDragUi.direction,
            gEditor.placementDragUi.originDir);
        bool found = false;
        float bestDist = 0.0f;
        rs::world::Guid bestEdge = gEditor.placementDragUi.edge;
        int bestDir = gEditor.placementDragUi.direction;
        double bestS = gEditor.placementDragUi.previewS;
        float bestLateral = gEditor.placementDragUi.previewLateralOffsetM;
        for (const PlacementDragCandidate& cand : candidates) {
            // 候補edgeごとにverticalOffsetMを再解決する(Poleの高さ基準=対の
            // Line entryはedge単位で異なりうるため。現在drag中の
            // edgeについてもここで再解決した値をそのまま使い、キャッシュ済み
            // placementDragUi.verticalOffsetMは上書きしない=hop確定時のみ更新する)。
            const PlacementAttrs attrs = ResolvePlacementAttrs(
                gEditor.placementDragUi.folder, cand.edge, gEditor.graph,
                ctx.selector.TrackPluginEntries());
            const rs::editor::Editor::PlacementEdgeProjection proj =
                gEditor.ProjectPlacementOffsetOnEdgeAtPoint(cand.edge, hit,
                                                              attrs.verticalOffsetM, cand.dir);
            if (!proj.found) continue;
            if (!found || proj.dist < bestDist) {
                found = true;
                bestDist = proj.dist;
                bestEdge = cand.edge;
                bestDir = cand.dir;
                bestS = proj.s;
                bestLateral = proj.lateralOffsetM;
            }
        }
        if (found) {
            if (bestEdge != gEditor.placementDragUi.edge) {
                // 隣接edgeへhopした: verticalOffsetM/keepUprightもhop先の
                // 実際の値へ更新する(ホップ前のedgeの値のままだとPoleの高さが
                // 元edgeのLine構成に固定されたままになる)。
                const PlacementAttrs attrs = ResolvePlacementAttrs(
                    gEditor.placementDragUi.folder, bestEdge, gEditor.graph,
                    ctx.selector.TrackPluginEntries());
                gEditor.placementDragUi.verticalOffsetM = attrs.verticalOffsetM;
            }
            gEditor.placementDragUi.edge = bestEdge;
            gEditor.placementDragUi.direction = bestDir;
            gEditor.placementDragUi.previewS = bestS;
            gEditor.placementDragUi.previewLateralOffsetM = bestLateral;
        }
    }
    if (input.ButtonReleased(input.ButtonFor(rs::app::input::PointerAction::Select))) {
        const std::vector<rs::world::MaterializeSnapshot> snapshots =
            BuildMaterializeSnapshotsForEdit(
                gEditor.graph, ctx.selector.TrackPluginEntries(),
                {gEditor.placementDragUi.originEdge, gEditor.placementDragUi.edge},
                gEditor.placementDragUi.folder);
        gEditor.MovePlacementTo(gEditor.placementDragUi.originEdge, gEditor.placementDragUi.originS,
                                 gEditor.placementDragUi.folder, gEditor.placementDragUi.edge,
                                 gEditor.placementDragUi.previewS,
                                 gEditor.placementDragUi.keepUpright,
                                 gEditor.placementDragUi.verticalOffsetM,
                                 gEditor.placementDragUi.originLateralOffsetM,
                                 gEditor.placementDragUi.previewLateralOffsetM,
                                 gEditor.placementDragUi.direction, snapshots);
        gEditor.selectedPlacementEdge = gEditor.placementDragUi.edge;
        gEditor.selectedPlacementS = gEditor.placementDragUi.previewS;
        gEditor.selectedPlacementFolder = gEditor.placementDragUi.folder;
        gEditor.placementDragUi.active = false;
    }
}

// 選択中の配置物からドラッグ状態を初期化する。
void BeginPlacementDrag(const EditorViewportContext& ctx) {
    rs::editor::Editor& gEditor = ctx.editor;
    gEditor.placementDragUi.active = true;
    gEditor.placementDragUi.edge = gEditor.selectedPlacementEdge;
    gEditor.placementDragUi.originEdge = gEditor.selectedPlacementEdge;
    gEditor.placementDragUi.originS = gEditor.selectedPlacementS;
    gEditor.placementDragUi.folder = gEditor.selectedPlacementFolder;
    gEditor.placementDragUi.previewS = gEditor.selectedPlacementS;
    // 選択中placementの現在のkeepUpright/verticalOffsetM/lateralOffsetM/dirを
    // キャッシュから引き継ぐ(auto/addition由来どちらでも、実際に描画されている
    // 値をそのまま使うのが正確。見つからなければ既定false/0/+1)。dirは
    // クロスエッジ移動のdirSign基準としてplacementDragUi.originDirへも
    // 固定保存する(ドラッグ中に複数edgeを跨いでもこの値自体は書き換えない、
    // Editor.hのコメント参照)。ドラッグ開始時のワールドYをplacementDragUi.planeYへ
    // 記録し、ProjectPlacementOffsetOnEdgeAtPointの水平面射影に使う
    // (nodeDragUi.planeYと同じ規約)。
    gEditor.placementDragUi.keepUpright = false;
    gEditor.placementDragUi.verticalOffsetM = 0.0f;
    gEditor.placementDragUi.originLateralOffsetM = 0.0f;
    gEditor.placementDragUi.previewLateralOffsetM = 0.0f;
    gEditor.placementDragUi.originDir = +1;
    gEditor.placementDragUi.direction = +1;
    gEditor.placementDragUi.planeY = 0.0f;
    for (const TrackPluginPlacementItem& item : ctx.placementCache) {
        if (item.placement.position.edge == gEditor.selectedPlacementEdge &&
            item.folderUtf8 == gEditor.selectedPlacementFolder &&
            std::fabs(item.placement.position.s - gEditor.selectedPlacementS) < rs::core::kPlacementPositionEpsM) {
            gEditor.placementDragUi.keepUpright = item.placement.keepUpright;
            gEditor.placementDragUi.verticalOffsetM = item.placement.verticalOffsetM;
            gEditor.placementDragUi.originLateralOffsetM = item.placement.lateralOffsetM;
            gEditor.placementDragUi.previewLateralOffsetM = item.placement.lateralOffsetM;
            gEditor.placementDragUi.originDir = item.placement.position.direction;
            gEditor.placementDragUi.direction = item.placement.position.direction;
            const rs::core::Mat4 m = rs::world::ResolveWorldMatrix(item.placement, gEditor.graph);
            gEditor.placementDragUi.planeY = m.m[13]; // ComposeRigidのorigin.y格納位置
            break;
        }
    }
}

} // namespace

bool HandlePlacementPickAndDrag(const EditorViewportContext& ctx, const rs::app::ViewProjResult& vpr,
                                rs::app::input::PointerPos mouse, bool leftClickForEditor) {
    rs::editor::Editor& gEditor = ctx.editor;
    const rs::app::input::InputSource& input = ctx.input;
    const UINT gCurWidth = ctx.width;
    const UINT gCurHeight = ctx.height;
    rs::core::Ray pickRay;
    if (!rs::core::ScreenToRay(mouse.x, mouse.y, vpr.viewProj, gCurWidth, gCurHeight, pickRay)) {
        return false;
    }

    // 自動配置物(Pier/Pole)のホバー判定。node/edgeと違い専用ヘルパーが無いため
    // ここで直接ピッキングする(Editor::Pick*には置かず
    // app層に置く)。
    size_t hoverIdx = 0;
    const bool hoverPlacement =
        PickTrackPluginPlacement(ctx.placementCache, gEditor.graph, ctx.selector, ctx.sceneRenderer, ctx.log,
                                pickRay, mouse.x, mouse.y, vpr.viewProj, gCurWidth, gCurHeight,
                                10.0f, hoverIdx);
    if (hoverPlacement) {
        const TrackPluginPlacementItem& it = ctx.placementCache[hoverIdx];
        gEditor.hoveredPlacementEdge = it.placement.position.edge;
        gEditor.hoveredPlacementS = it.placement.position.s;
        gEditor.hoveredPlacementFolder = it.folderUtf8;
    } else {
        gEditor.hoveredPlacementEdge = rs::world::NilGuid();
        gEditor.hoveredPlacementFolder.clear();
    }

    // 配置物のedge沿い移動＋左右自由移動＋隣接edgeへのクロスエッジ移動。ドラッグ中は毎フレーム、起点edge上の水平面
    // (placementDragUi.planeY、hopしても再計算しない=既知の制限)へレイを射影し、
    // 現在のedge＋通過点で繋がる隣接edgeを候補として最短距離のものへプレビュー
    // s/lateralOffsetM/edge/dirを更新する。マウスアップで一括確定する(毎フレーム
    // overlayをmutateしない)。
    if (gEditor.placementDragUi.active) {
        UpdatePlacementDrag(ctx, vpr, mouse);
        return false;
    }

    if (leftClickForEditor) {
        if (hoverPlacement) {
            // 配置物ヒット時は他の選択(node/edge、SceneEntity)より優先する
            // (小さい的なので奥にある対象より手前の意図を汲む、既存Railモードの
            // 規約をSceneryモードにもそのまま適用)。
            gEditor.selectedPlacementEdge = gEditor.hoveredPlacementEdge;
            gEditor.selectedPlacementS = gEditor.hoveredPlacementS;
            gEditor.selectedPlacementFolder = gEditor.hoveredPlacementFolder;
            return true;
        }
        gEditor.ClearSelectedPlacement();
        return false;
    }

    if (gEditor.HasSelectedPlacement() && !input.MatchesPointer(rs::app::input::PointerAction::CameraHeight) &&
        input.ButtonDragging(input.ButtonFor(rs::app::input::PointerAction::Select), 4.0f)) {
        // 選択中の配置物を(押下フレームで選択済みのまま)しきい値超過分動かした
        // 瞬間にドラッグへ移行する。押下がplacementでなかった場合は
        // HasSelectedPlacement()が偽(通常の他選択に落ちている)ためここには
        // 来ない。
        BeginPlacementDrag(ctx);
    }
    return false;
}

} // namespace rs::app
