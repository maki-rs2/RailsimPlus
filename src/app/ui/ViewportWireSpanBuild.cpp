#include "ViewportWireSpanBuild.h"

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../../core/Math.h"
#include "../../editor/tool/Editor.h"
#include "../../plugins/load/PluginLoader.h"
#include "../../plugins/load/LineHangerPlacement.h"
#include "../../platform/text/Encoding.h"
#include "../../render/resource/XLoader.h"
#include "../../render/mesh/MeshAssembler.h"
#include "../../world/scene/ScenePlacement.h"
#include "../scene/SceneRenderer.h"
#include "../scene/TrackPluginPlacement.h"
#include "WireConnectPoints.h"

namespace rs::app {

namespace {


// ハンガー(碍子)モデルをキャッシュ経由で取得する。未ロードなら.xを読み、
// 見つからない/ロード失敗の場合も空Meshとしてキャッシュに残して再試行しない。
// 配置対象が無い(空Mesh)ときは nullptr を返す。
const rs::render::Mesh* GetOrLoadHangerModel(HangerModelCache& cache,
                                             const std::wstring& modelPath) {
    const std::string modelKey = rs::platform_text::WideToUtf8(modelPath);
    HangerModelCache::iterator it = cache.find(modelKey);
    if (it == cache.end()) {
        rs::render::Mesh loaded;
        if (rs::legacy_plugin::FileExists(modelPath)) {
            rs::legacy_x::LoadResult hgLoad = rs::legacy_x::LoadXFileAssimp(modelPath);
            if (hgLoad.ok) loaded = std::move(hgLoad.mesh);
        }
        it = cache.emplace(modelKey, std::move(loaded)).first;
    }
    return it->second.Empty() ? nullptr : &it->second;
}

} // namespace

// 1径間ぶんのワイヤーメッシュとハンガー(碍子)モデルを assembler へ積む。
void AppendWireAndHangers(const TrackPluginEntry& lineEntry, const std::wstring& lineBaseDir,
                          const rs::core::Vec3& baseA, const rs::core::Vec3& upA,
                          const rs::core::Vec3& baseB, const rs::core::Vec3& upB,
                          HangerModelCache& hangerModelCache, rs::render::MeshAssembler& assembler,
                          std::vector<std::string>& log, int& missing) {
    rs::render::Mesh wire = rs::legacy_plugin::BuildLineWireMesh(
        lineEntry.def.line, lineBaseDir, baseA, upA, baseB, upB, log, missing);
    if (!wire.Empty()) assembler.Append(wire, true);

    // ハンガー(碍子): Wireframe(ハンガー取付点トラッキング)のラベル付き
    // Vertexと、Interval{WireTracking;ModelFileName}を突き合わせて
    // 得た配置点ごとに.xモデルを配置する。
    for (const rs::legacy_plugin::LineHangerPlacement& hp :
        rs::legacy_plugin::ComputeLineHangerPlacements(
            lineEntry.def.line, lineEntry.def.intervals, baseA, upA, baseB, upB)) {
        const std::wstring modelPath =
            rs::legacy_plugin::ResolvePath(lineBaseDir, hp.interval->modelFile);
        const rs::render::Mesh* hangerMesh = GetOrLoadHangerModel(hangerModelCache, modelPath);
        if (!hangerMesh) continue;
        rs::render::Mesh inst = rs::legacy_plugin::PlaceMeshInstance(
            *hangerMesh, hp.interval->modelScale, hp.pos, hp.fwd, hp.up);
        if (!inst.Empty()) assembler.Append(inst, true);
    }
}

void AppendMaterializedWireSpans(const EditorViewportContext& ctx,
                                 HangerModelCache& hangerModelCache,
                                 rs::render::MeshAssembler& assembler) {
    // 実体化済みPole個体のGuid参照から架線径間を再構築する。接続点の基準位置は
    // MaterializedWireBasePos(WireConnectPoints.h)で求め、接続点の丸(AppendMaterializedConnectPoints)と
    // 共有する。Pole原点から引くheightはAppendImportedWireSpansと一致する。
    // vertical == trolleyAlt + heightなら、AppendAutoWireSpansのpos + up*trolleyAltとも同じ位置になる。
    const rs::world::ManualPlacementOverlay& overlay = ctx.editor.manualOverlay;
    std::unordered_map<rs::world::Guid, const rs::world::PlacedIndividual*, rs::world::GuidHash>
        polesById;
    const std::vector<rs::world::PlacedIndividual>& linePoles =
        ctx.sceneRenderer.StructLinePoles().Poles();
    polesById.reserve(overlay.poles.size() + linePoles.size());
    for (const rs::world::PlacedIndividual& pole : overlay.poles) {
        polesById[pole.id] = &pole;
    }
    // Structの接続点から導出した柱。オーバーレイには入っておらず、Guidで径間から参照される。
    for (const rs::world::PlacedIndividual& pole : linePoles) {
        polesById[pole.id] = &pole;
    }
    const std::unordered_set<rs::world::Guid, rs::world::GuidHash> linePoleIds =
        CollectPoleIds(linePoles);
    const auto isLinePole = [&linePoleIds](const rs::world::PlacedIndividual& pole) {
        return linePoleIds.find(pole.id) != linePoleIds.end();
    };
    std::vector<std::string> tmpLog;
    int tmpMissing = 0;
    for (const rs::world::WireSpan& span : overlay.lines) {
        const std::unordered_map<rs::world::Guid, const rs::world::PlacedIndividual*,
                                 rs::world::GuidHash>::const_iterator
            poleAIt = polesById.find(span.poleA);
        const std::unordered_map<rs::world::Guid, const rs::world::PlacedIndividual*,
                                 rs::world::GuidHash>::const_iterator
            poleBIt = polesById.find(span.poleB);
        if (poleAIt == polesById.end() || poleBIt == polesById.end()) continue;
        const rs::world::PlacedIndividual& poleA = *poleAIt->second;
        const rs::world::PlacedIndividual& poleB = *poleBIt->second;
        const TrackPluginEntry* poleEntryA = poleA.folderUtf8.empty()
            ? nullptr : ctx.selector.FindTrackPluginEntry(poleA.folderUtf8);
        const TrackPluginEntry* poleEntryB = poleB.folderUtf8.empty()
            ? nullptr : ctx.selector.FindTrackPluginEntry(poleB.folderUtf8);
        const TrackPluginEntry* lineEntry = ctx.selector.FindTrackPluginEntry(span.lineFolderUtf8);
        // 径間を捨てる条件はLine側が引けないときだけにする。Pole側の
        // entryは横オフセット列を得るためだけに使うので、引けなければオフセット0で描く。
        // 架線専用アンカー(folder無し)や、Poleプラグインが未ロードのケースで径間が
        // 丸ごと消えるのを防ぐ。
        if (lineEntry == nullptr) continue;
        const std::vector<float> offsetsA =
            poleEntryA == nullptr ? std::vector<float>{0.0f} : WireLateralOffsetsFor(*poleEntryA);
        const std::vector<float> offsetsB =
            poleEntryB == nullptr ? std::vector<float>{0.0f} : WireLateralOffsetsFor(*poleEntryB);
        // 接続点添字が横オフセット列の範囲外なら、径間を捨てずオフセット0で描く。
        // Poleが公称本数(TrackNum)より多い接続点を持つ実PIがあり、範囲外を無いものとして
        // 扱うと複線用Poleの架線が片側だけ消える。
        const auto branchOffset = [](const std::vector<float>& offsets, int branch) -> float {
            if (branch < 0 || static_cast<std::size_t>(branch) >= offsets.size()) return 0.0f;
            return offsets[static_cast<std::size_t>(branch)];
        };
        rs::world::TrackFrame frameA;
        if (!ResolvePoleFrame(ctx.editor.graph, poleA, frameA)) continue;
        rs::world::TrackFrame frameB;
        if (!ResolvePoleFrame(ctx.editor.graph, poleB, frameB)) continue;
        const float height = lineEntry->def.line.height;
        const rs::core::Vec3 baseA = MaterializedWireBasePos(
            frameA, poleA, branchOffset(offsetsA, span.branchA), height);
        const rs::core::Vec3 baseB = MaterializedWireBasePos(
            frameB, poleB, branchOffset(offsetsB, span.branchB), height);
        // 切断・選択用の径間キャッシュ。軌道に載った柱と、Structの接続点の柱だけが対象
        // (それ以外のワールドアンカー柱と架線専用アンカーはedgeでもGuidでも指せない)。
        const auto pointRefOf = [&isLinePole](const rs::world::PlacedIndividual& pole, int branch)
            -> std::optional<rs::editor::WirePointRef> {
            if (isLinePole(pole)) {
                rs::editor::WirePointRef ref;
                ref.poleId = pole.id;
                ref.branch = branch;
                return ref;
            }
            if (pole.hasWorldAnchor || pole.folderUtf8.empty()) return std::nullopt;
            return rs::editor::WirePointRef{pole.edge, pole.s, pole.folderUtf8, branch};
        };
        const std::optional<rs::editor::WirePointRef> refA = pointRefOf(poleA, span.branchA);
        const std::optional<rs::editor::WirePointRef> refB = pointRefOf(poleB, span.branchB);
        if (refA.has_value() && refB.has_value()) {
            ctx.wireEditCache.spans.push_back({{*refA, *refB}, baseA, baseB, span.lineFolderUtf8});
        }
        const std::wstring lineBaseDir = rs::platform_text::Utf8ToWide(span.lineFolderUtf8);
        AppendWireAndHangers(*lineEntry, lineBaseDir, baseA, frameA.up, baseB, frameB.up,
                             hangerModelCache, assembler, tmpLog, tmpMissing);
    }

    // 径間を持たない柱も含め、実体化済みの柱の接続点をすべて積む。
    AppendMaterializedConnectPoints(ctx.editor.graph, overlay, ctx.selector,
                                    ctx.wireEditCache.points);
    AppendStructLinePoleConnectPoints(ctx.editor.graph, linePoles, ctx.wireEditCache.points);
}

} // namespace rs::app
