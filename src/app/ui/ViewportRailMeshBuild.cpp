#include "ViewportRailMeshBuild.h"

#include "../../plugins/schema/PluginTypeTraits.h"

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "../../core/Math.h"
#include "../../editor/tool/Editor.h"
#include "../../plugins/load/PluginLoader.h"
#include "../../plugins/load/LineHangerPlacement.h"
#include "../../platform/text/Encoding.h"
#include "../../render/resource/XLoader.h"
#include "../../render/mesh/MeshAssembler.h"
#include "../../world/alignment/SplineSlice.h"
#include "../../world/scene/ScenePlacement.h"
#include "../gpu/App.h"
#include "../plugin_session/RailTieHeights.h"
#include "../scene/SceneRenderer.h"
#include "../scene/TrackPluginPlacement.h"
#include "ViewportWireSpanBuild.h"
#include "WireConnectPoints.h"

namespace rs::app {

namespace {

// Interval反復モデルはedgeに依存しないため、エントリ単位で1回だけロード
// する(edgeごとのループ内で毎回読み直さないためのキャッシュ)。
using IntervalCache = std::unordered_map<std::string, std::vector<rs::legacy_plugin::LoadedIntervalModel>>;

// Interval反復モデルをRailInfoを持つエントリ単位で先読みする。
IntervalCache LoadIntervalModels(const EditorViewportContext& ctx) {
    IntervalCache intervalCache;
    for (const TrackPluginEntry& entry : ctx.selector.TrackPluginEntries()) {
        const bool isExtrudable = rs::legacy_pi::GetPluginTypeTraits(entry.type).edgeExtrusion;
        if (!isExtrudable || !entry.def.hasRailInfo) continue;
        const std::wstring baseDir = rs::platform_text::Utf8ToWide(entry.folderUtf8);
        intervalCache.emplace(entry.folderUtf8, rs::legacy_plugin::LoadIntervalModels(entry.def, baseDir));
    }
    return intervalCache;
}

// 連続押し出し(Rail/Tie/Girder): RailInfoを持つ全エントリ×全edgeをmergedへ
// マージする。Lineは実際には
// 隣接Pole間を直線で結ぶ別方式(AppendAutoWireSpansで処理)の
// ためこの関数では対象外。
void AppendEdgeExtrusions(const EditorViewportContext& ctx, const IntervalCache& intervalCache,
                          rs::render::MeshAssembler& assembler) {
    rs::editor::Editor& gEditor = ctx.editor;
    std::vector<std::string> tmpLog;
    int tmpMissing = 0;

    // edgeごとに、そのedgeのtrackPluginsに含まれるエントリのみを
    // 重ねて配置する(全edge共通の単一リストではなくedge独立)。
    for (auto& kv : gEditor.graph.edges) {
        rs::world::TrackEdge& edge = kv.second;
        // 中心線edge(centerlineOnly)は実レール(Rail/Tie)を押し出さないが、
        // 複線一括Girder(trackNum>1)はここでのみ押し出す(下の entry ループの
        // Girder分岐参照)。
        // よって edge レベルでは centerline も継続処理する。
        if (edge.spline.size() < 2) continue;

        // Rail/Tie/Girderの積み上げ沈め量(RailSim II の CProfilePlugin::AfterDump相当):
        // Tieはその下にRail.Height分、Girderはさらにその下にTie.Height分沈める
        // (相対関係は RailSim II 準拠、同種複数エントリは最大Heightを使う)。
        // このedgeが有効化しているエントリのみを対象に算出する(区間指定
        // (sStart/sEnd)は高さ計算には影響しない、対象外: 1edge=1本の
        // railTopLiftのまま。folderUtf8だけ取り出して既存シグネチャへ渡す)。
        const std::vector<std::string> activeFolders = rs::world::ActiveFolders(edge);
        const RailTieHeights h = ComputeRailTieHeights(activeFolders, ctx.selector);

        // spline(pos/up)は地面(ノード標高)基準のまま据え置き、Rail+Tie分だけ
        // 全体をup方向へ底上げする(Girderは含まない=Girder上面は地面基準のまま、
        // Tieの下面がちょうど地面に来るようにする=敷設時にRail/Tieが地面に
        // 埋まらない)。列車(TrainVehicle.cpp AxleFrame)・敷設エディタの軌道
        // 基準線描画はこのedge.railTopLiftを参照して同じ持ち上げ量を反映する。
        //
        // .rs2取り込みedge(curveを持たずsplineのみ)も含め全edgeで同一の式を使う。取り込み時点でRs2SceneApplier.cppが
        // node.pos.y/edge.spline[].pos.yをこのbaseLift分だけ地面基準へ正規化
        // するため、ここで再度baseLiftを乗せても二重底上げにはならず、
        // かつ取り込みノードから新規敷設した場合も接続点の高さが一致する。
        const float baseLift = h.TopLift();
        edge.railTopLift = baseLift;
        // 親に追従するedgeはメッシュを描かない(RailSim II の CRailWay::Dump が親つきなら何も出さないのと同じ)。
        // 休止位置に焼くと動かないレールが残るため。railTopLift は走行が参照するので上で設定する。
        if (rs::world::IsParentDrivenEdge(edge)) continue;

        for (const TrackPluginEntry& entry : ctx.selector.TrackPluginEntries()) {
            const bool isExtrudable = rs::legacy_pi::GetPluginTypeTraits(entry.type).edgeExtrusion;
            if (!isExtrudable || !entry.def.hasRailInfo) continue;
            // 複線一括Girder(trackNum>1)は中心線edgeにのみ、単線Girder/Rail/Tieは
            // 実レールedgeにのみ押し出す(trackNum routingのGirder版)。
            if (rs::legacy_pi::GetPluginTypeTraits(entry.type).multiTrackSource ==
                rs::legacy_pi::MultiTrackSource::Profile) {
                const int trackNum = MultiTrackNum(entry.def, entry.type);
                if (trackNum > 1) {
                    if (!edge.centerlineOnly || edge.trackCount != trackNum) continue;
                } else if (edge.centerlineOnly) {
                    continue;
                }
            } else if (edge.centerlineOnly) {
                continue;
            }
            const double edgeTotal = static_cast<double>(edge.spline.back().s);
            const std::vector<std::pair<double, double>> ranges =
                rs::world::FolderRanges(edge, entry.folderUtf8, edgeTotal);
            if (ranges.empty()) continue;
            const rs::legacy_pi::PluginTypeTraits& traits = rs::legacy_pi::GetPluginTypeTraits(entry.type);
            const float extraOffsetY = ExtrusionOffsetY(entry.type, h);
            const std::wstring baseDir = rs::platform_text::Utf8ToWide(entry.folderUtf8);
            const auto cacheIt = intervalCache.find(entry.folderUtf8);

            // edge内プラグイン区間: folderUtf8が有効な各範囲
            // (飛び地があれば複数)ごとに独立して押し出し・反復配置を行う。
            // 反復の位相は各範囲の先頭からリセットする。
            for (const std::pair<double, double>& range : ranges) {
                const std::vector<rs::world::TrackNodeSample> sliced =
                    rs::world::SliceSpline(edge, range.first, range.second);
                if (sliced.size() < 2) continue;
                rs::render::Mesh em = rs::legacy_plugin::BuildRailMeshAlongSpline(
                    entry.def, baseDir, sliced, tmpLog, tmpMissing, extraOffsetY);
                if (!em.Empty()) assembler.Append(em, traits.profileSupport == rs::legacy_pi::ProfileSupport::Rail);

                // まくらぎ等のInterval反復モデル表示: Offset(PI側の位相ずれ定義値)
                // 込みでこの範囲のsplineに沿って反復配置する(押し出しプロファイルは
                // 維持したうえで実モデルを追加表示する方針)。
                if (cacheIt == intervalCache.end()) continue;
                for (const rs::legacy_plugin::LoadedIntervalModel& li : cacheIt->second) {
                    rs::render::Mesh im = rs::legacy_plugin::BuildIntervalMeshAlongSpline(
                        li.mesh, li.interval->modelScale, li.interval->interval, li.interval->offset,
                        sliced, entry.def.gauge, li.interval->ignoreCant, extraOffsetY);
                    if (!im.Empty()) assembler.Append(im);
                }
            }
        }
    }
}

// Line(架線)ワイヤー本体。
// WalkPoleRuns(Pole配置と同じ走査)で求めた隣接Pole間のスパン
// ごとにワイヤーメッシュを生成し、Rail/Tie/Girderと同じmergedへ重ねる。
// ワイヤーのローカルz=0/1基準点は架線支持点(Pole原点=TrolleyAlt+Height)
// ではなくトロリ線高さ(TrolleyAltのみ)なので、Pole配置のverticalOffsetM
// とは別に求める(RailSim II の CLine::Render(): `p1 = m_Link[0].GetPos()
// -m_LinePlugin->m_Height*V3UP`、Pole原点からHeight分だけ差し引いた
// 高さがワイヤー基準点であることに対応)。
void AppendAutoWireSpans(const EditorViewportContext& ctx, HangerModelCache& hangerModelCache,
                         rs::render::MeshAssembler& assembler) {
    rs::editor::Editor& gEditor = ctx.editor;
    std::vector<std::string> tmpLog;
    int tmpMissing = 0;

    // edgeのRail+Tie積み上げ量(railTopLift相当)。a.pos/b.posはspline(地面)基準
    // (PoleWalkPoint定義コメント参照)なので、レール天端との相対を合わせるには
    // そのedgeの積み上げ量も足す必要がある。edge.railTopLiftはメッシュ
    // 再構築時にのみ書き込まれるキャッシュであり、この関数自体がRebuildExtrudedRailMesh
    // 内でAppendEdgeExtrusions(キャッシュ書き込み元)より後に呼ばれるため現状は
    // キャッシュ参照でも安全だが、その前提を暗黙に固定しないよう
    // ActiveFolders+ComputeRailTieHeightsで直接算出する
    // (TrackPluginPlacement.cpp/PlacementMaterialize.cppと同じ式・同じ理由)。
    const auto railTopLiftAt = [&gEditor, &ctx](const rs::world::Guid& edge) -> float {
        const auto edgeIt = gEditor.graph.edges.find(edge);
        if (edgeIt == gEditor.graph.edges.end()) return 0.0f;
        const RailTieHeights h = ComputeRailTieHeights(
            rs::world::ActiveFolders(edgeIt->second), ctx.selector.TrackPluginEntries());
        return h.maxRailHeight + h.maxTieHeight;
    };

    ctx.wireEditCache.points.clear();
    ctx.wireEditCache.spans.clear();
    for (const PoleWalkRun& run :
        WalkPoleRuns(ctx.selector.TrackPluginEntries(), gEditor.graph)) {
        const float trolleyAlt = run.lineEntry->def.line.trolleyAlt;
        const std::wstring lineBaseDir =
            rs::platform_text::Utf8ToWide(run.lineEntry->folderUtf8);
        const std::vector<PoleWalkPoint>& points = run.points;
        const std::vector<float> lateralOffsets = WireLateralOffsetsFor(*run.poleEntry);
        for (size_t k = 0; k + 1 < points.size(); ++k) {
            const PoleWalkPoint& a = points[k];
            const PoleWalkPoint& b = points[k + 1];
            const float aVertical = railTopLiftAt(a.position.edge) + trolleyAlt;
            const float bVertical = railTopLiftAt(b.position.edge) + trolleyAlt;
            for (size_t branch = 0; branch < lateralOffsets.size(); ++branch) {
                const float lateralOffsetM = lateralOffsets[branch];
                const rs::core::Vec3 rightA =
                    rs::core::Vec3::Cross(a.up, a.dir).Normalized();
                const rs::core::Vec3 rightB =
                    rs::core::Vec3::Cross(b.up, b.dir).Normalized();
                // manualLateralOffsetM(手動移動)をWireLateralOffsetsForの複線
                // ワイヤーパターンへ加算し、Poleを横へ動かした際にワイヤーも
                // 追従させる。auto生成点は常に0なので自動配置の挙動は不変。
                const rs::core::Vec3 baseA =
                    a.pos + a.up * aVertical +
                    rightA * (lateralOffsetM + a.manualLateralOffsetM);
                const rs::core::Vec3 baseB =
                    b.pos + b.up * bVertical +
                    rightB * (lateralOffsetM + b.manualLateralOffsetM);

                // 接続点と径間のキャッシュ: 実際の描画位置(baseA/baseB)をそのまま持つ
                // (画面の丸と架線メッシュの端を一致させるため)。runの内側の柱は2回出るので、
                // 点は各柱を1回だけ積む(先頭から順にaを、最後だけbも)。
                const rs::editor::WirePointRef refA{a.position.edge, a.position.s,
                                                    run.poleEntry->folderUtf8,
                                                    static_cast<int>(branch)};
                const rs::editor::WirePointRef refB{b.position.edge, b.position.s,
                                                    run.poleEntry->folderUtf8,
                                                    static_cast<int>(branch)};
                const std::string& lineFolder = run.lineEntry->folderUtf8;
                ctx.wireEditCache.points.push_back({refA, baseA, a.up, lineFolder});
                if (k + 2 == points.size())
                    ctx.wireEditCache.points.push_back({refB, baseB, b.up, lineFolder});
                ctx.wireEditCache.spans.push_back({{refA, refB}, baseA, baseB, lineFolder});

                AppendWireAndHangers(*run.lineEntry, lineBaseDir, baseA, a.up, baseB, b.up,
                                     hangerModelCache, assembler, tmpLog, tmpMissing);
            }
        }
    }
}

// .rs2インポート由来の架線スパンのうち、片端でも軌道に載っていない径間を、取り込み時の絶対座標のまま描く。
// 両端が軌道に載っている径間は正規化済みで、AppendMaterializedWireSpansが描く。
void AppendImportedWireSpans(const EditorViewportContext& ctx,
                             HangerModelCache& hangerModelCache,
                             rs::render::MeshAssembler& assembler) {
    std::vector<std::string> tmpLog;
    int tmpMissing = 0;

    const rs::core::Vec3 kWorldUp(0.0f, 1.0f, 0.0f);
    for (const rs::app::ImportedWireSpan& span : ctx.importedWireSpans) {
        const TrackPluginEntry* lineEntry =
            ctx.selector.FindTrackPluginEntry(span.lineFolderUtf8);
        if (!lineEntry) continue;
        // 解決済みのPole原点(=軌道面基準でTrolleyAlt+Height、
        // TrackPluginPlacement.cpp:500-506参照)から、ワイヤー基準点
        // (トロリ線高さ=TrolleyAltのみ)へはHeight分だけ引き下げる
        // (RailSim II の CLine::Render(): `p1 = m_Link[0].GetPos()
        // -m_LinePlugin->m_Height*V3UP`に対応。ここでTrolleyAltを加算すると
        // TrolleyAlt+Height分ワイヤーが高く描画されるので注意)。
        const float height = lineEntry->def.line.height;
        const std::wstring lineBaseDir = rs::platform_text::Utf8ToWide(span.lineFolderUtf8);
        const rs::core::Vec3 poleOriginA = span.posA;
        const rs::core::Vec3 poleUpA = kWorldUp;
        const rs::core::Vec3 poleOriginB = span.posB;
        const rs::core::Vec3 poleUpB = kWorldUp;
        const rs::core::Vec3 baseA = poleOriginA - poleUpA * height;
        const rs::core::Vec3 baseB = poleOriginB - poleUpB * height;

        AppendWireAndHangers(*lineEntry, lineBaseDir, baseA, poleUpA, baseB, poleUpB,
                             hangerModelCache, assembler, tmpLog, tmpMissing);
    }
}

} // namespace

rs::render::Mesh BuildWireSpanMesh(const TrackPluginEntry& lineEntry, const rs::core::Vec3& baseA,
                                   const rs::core::Vec3& upA, const rs::core::Vec3& baseB,
                                   const rs::core::Vec3& upB, HangerModelCache& hangerModelCache) {
    rs::render::MeshAssembler assembler;
    std::vector<std::string> log;
    int missing = 0;
    AppendWireAndHangers(lineEntry, rs::platform_text::Utf8ToWide(lineEntry.folderUtf8), baseA,
                         upA, baseB, upB, hangerModelCache, assembler, log, missing);
    return assembler.Build();
}

// Rail/Tie/Girderの連続押し出しメッシュ + Line(架線)ワイヤーの再構築(g_editor.revision等が
// 変化した際にメインのメッシュパイプラインへ上げる1枚のmergedメッシュを作る)。
void RebuildExtrudedRailMesh(const EditorViewportContext& ctx) {
    // edges.empty()でもmerged(空)を上げること(下のメッシュが載っているときだけ)。
    // 上げないと、Undo等で最後の1本を消したときに直前のメッシュが画面に残る。
    rs::render::MeshAssembler assembler;

    const IntervalCache intervalCache = LoadIntervalModels(ctx);
    AppendEdgeExtrusions(ctx, intervalCache, assembler);

    HangerModelCache hangerModelCache;
    // wireEditCacheはAppendAutoWireSpansがクリアし、以降のappenderが追記する。
    AppendAutoWireSpans(ctx, hangerModelCache, assembler);
    AppendImportedWireSpans(ctx, hangerModelCache, assembler);
    AppendMaterializedWireSpans(ctx, hangerModelCache, assembler);

    rs::render::Mesh merged = assembler.Build();
    const double compressionRatio = assembler.SourceSubmeshCount() == 0
        ? 1.0
        : static_cast<double>(merged.submeshes.size()) /
              static_cast<double>(assembler.SourceSubmeshCount());
    spdlog::debug("[TrackPlugin] assembled rawSubmeshes={} mergedSubmeshes={} "
                 "compressionRatio={:.4f}",
                 assembler.SourceSubmeshCount(), merged.submeshes.size(), compressionRatio);
    // 空で、消すべき直前のメッシュも無いとき(敷設前の起動直後)は、空メッシュの警告を避けて上げない。
    if (merged.vertices.empty() && !ctx.app.HasModel()) return;
    ctx.app.UploadMeshToGpu(merged);
}

} // namespace rs::app
