#include "Rs2SceneApplier.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

#include "PluginSelector.h"
#include "RailTieHeights.h"
#include "Rs2PlacementNormalize.h"
#include "Rs2StationParentLookup.h"
#include "../../plugins/layout/Rs2Import.h"
#include "../../world/scene/SceneRegistry.h"
#include "../scene/TrackPluginPlacement.h"

namespace rs::app {

namespace {

// Struct/Station/Pier/Pole/Surface(標準配置)は全てSceneEntityCategory::Scenery。
rs::world::SceneEntityCategory CategoryForKind(const std::string&) {
    return rs::world::SceneEntityCategory::Scenery;
}

// .rs2取り込みedgeのnode.pos.y/edge.spline[].pos.yは、RailSim II のSplitter.Posが
// 「レール天端の最終描画位置」であるため敷設エディタの「地面(ノード標高)基準」
// 規約(TrackGraph.h の TrackEdge::railTopLift コメント参照)と食い違う。
// EditorViewport.cppが全edge共通でrailTopLift(=Rail+Tie積み上げ量)を地面基準へ
// 加算する前提のため、ここで取り込み直後に同じbaseLift分だけ差し引いて地面基準へ
// 正規化する(しないと、取り込みノードから新規敷設した際に接続点の高さがずれる。
// 正規化後にEditorViewport.cppが同じbaseLiftを足し戻すため、.rs2単体
// 表示時の見た目は変わらない)。
//
// 配置物(Struct/Station/Pier/Pole)の絶対座標は変えない。splineを下げた分は
// importedPiers/importedPolesのverticalOffsetMへ足し戻す。
//
// 唯一の呼び出し元ImportRs2Sceneは、result.graphをImportTrackGraphから毎回新規生成した
// 直後にこの関数を呼んでおり、その時点のresult.graphは取り込みedgeのみで構成される
// (既存シーンのgraphとマージされるのはこの関数より後)。そのためedge出自の判定は不要
// (CollectLinkedAddressesも同様)。この関数を
// 混在graph(敷設エディタ産edgeを含む既存シーン)に対して呼ぶと、既に地面基準のedgeへ
// 誤って二重降下を適用してしまうため、呼び出し元をこの前提から変えないこと。
void NormalizeImportedGroundBasis(rs::world::TrackGraph& graph, const PluginSelector& selector) {
    using rs::world::Guid;
    using rs::world::GuidHash;
    std::unordered_map<Guid, float, GuidHash> edgeBaseLift;
    for (auto& [guid, edge] : graph.edges) {
        const std::vector<std::string> folders = rs::world::ActiveFolders(edge);
        const RailTieHeights h = ComputeRailTieHeights(folders, selector);
        const float baseLift = h.maxRailHeight + h.maxTieHeight;
        edgeBaseLift.emplace(guid, baseLift);
        if (baseLift == 0.0f) continue;
        for (rs::world::TrackNodeSample& smp : edge.spline) smp.pos.y -= baseLift;
        // importedPiers/importedPolesのverticalOffsetMは正規化前(レール天端基準)の
        // splineに対して算出済み。splineを下げた分だけ足し戻し、Pier/Poleの
        // ワールド座標(JointPos/Pos)を取り込み前と一致させる。
        for (rs::world::ImportedPluginLink& link : edge.importedPiers)
            link.verticalOffsetM += baseLift;
        for (rs::world::ImportedPluginLink& link : edge.importedPoles)
            link.verticalOffsetM += baseLift;
    }
    // ノードは複数edgeの結節点でありうる。接続する取り込みedgeのbaseLift最大値を
    // 差し引く(NodeRailTopPosが接続edgeの最大liftを採る慣習に合わせる)。
    for (auto& [nodeGuid, node] : graph.nodes) {
        (void)nodeGuid;
        float maxLift = 0.0f;
        bool any = false;
        for (const rs::world::TrackNode::Link& link : node.links) {
            const auto it = edgeBaseLift.find(link.edge);
            if (it == edgeBaseLift.end()) continue;
            maxLift = std::max(maxLift, it->second);
            any = true;
        }
        if (any) node.pos.y -= maxLift;
    }
}

// TrackEdge::importedPiers/importedPolesにedge厳密紐付け済みの RailSim II の Address集合
// (Pier/Pole共通)。静的Scenery配置としての二重焼き込みを避けるために使う。
std::unordered_set<std::string> CollectLinkedAddresses(const rs::world::TrackGraph& graph) {
    std::unordered_set<std::string> out;
    for (const auto& [guid, edge] : graph.edges) {
        (void)guid;
        for (const rs::world::ImportedPluginLink& link : edge.importedPiers)
            if (!link.address.empty()) out.insert(link.address);
        for (const rs::world::ImportedPluginLink& link : edge.importedPoles)
            if (!link.address.empty()) out.insert(link.address);
    }
    return out;
}

// trackPluginsのフォルダを重複排除しながらキャッシュへ登録し、結果を集計する。
class FolderRegistrar {
public:
    FolderRegistrar(Rs2SceneImportResult& result, PluginSelector& selector)
        : result_(result), selector_(selector) {}

    std::string Register(const std::string& folder) {
        if (folder.empty()) return folder;
        const auto found = loadedFolders_.find(folder);
        if (found != loadedFolders_.end()) return found->second;
        std::string err;
        const std::optional<std::string> registered = selector_.EnsureTrackPluginLoaded(folder, err);
        if (registered) {
            ++result_.pluginFoldersLoaded;
        } else {
            ++result_.pluginFoldersFailed;
            result_.failedPluginFolders.push_back(folder);
            result_.warnings.push_back("trackPlugins読込失敗: " + folder + " (" + err + ")");
        }
        const std::string resolved = registered.value_or(folder);
        loadedFolders_.emplace(folder, resolved);
        return resolved;
    }

private:
    Rs2SceneImportResult& result_;
    PluginSelector& selector_;
    std::unordered_map<std::string, std::string> loadedFolders_;
};

// edgeの区間・橋脚・架線柱のプラグインを登録し、参照を登録済みの表記へ揃える。メッシュ生成は
// 登録済みの一覧(TrackPluginEntries)を区間と完全一致で比べるので、登録も表記揃えも欠かせない
// (未登録の橋脚は高さ連動の組み立てに届かない)。
void RegisterEdgePluginFolders(rs::world::TrackGraph& graph, FolderRegistrar& registrar,
                                const PluginSelector& selector) {
    for (auto& [guid, edge] : graph.edges) {
        (void)guid;
        for (const std::string& folder : rs::world::ActiveFolders(edge)) registrar.Register(folder);
        for (const rs::world::ImportedPluginLink& link : edge.importedPiers)
            registrar.Register(link.folderUtf8);
        for (const rs::world::ImportedPluginLink& link : edge.importedPoles)
            registrar.Register(link.folderUtf8);
        selector.CanonicalizeTrackPluginFolders(edge);
    }
}

// 配置物(Struct/Station/Pier/Pole/Surface)。SceneRegistryへの投入は呼び出し側
// (UpsertImportedPlacements)が行う。
//
// Pier/Poleのうち、RailWay埋め込みPierList/PoleList.PierLink/PoleLinkでedge厳密
// 紐付けできたもの(TrackEdge::importedPiers/importedPoles)は、そちら経由で
// ManualPlacementOverlayのPier/Pole個体へ正規化される(NormalizeImportedPlacements、
// Rs2PlacementNormalize.cpp)ため、ここでは静的Scenery配置として焼き込まない
// (二重描画防止。curve編集にも追従できる)。
// 紐付けられなかった孤立Pier/Pole(万一PierLink/PoleLinkが無い/未解決の場合の
// フォールバック)や、Struct/Stationは絶対座標のまま焼き込む。
void BakeStandalonePlacements(std::vector<rs::legacy_layout::ImportedPlacement>& placements,
                              Rs2SceneImportResult& result, FolderRegistrar& registrar) {
    const std::unordered_set<std::string> linkedAddresses = CollectLinkedAddresses(result.graph);
    for (rs::legacy_layout::ImportedPlacement& p : placements) {
        if (p.pluginFolderUtf8.empty()) continue;  // 不足プラグイン/PolePlugin=""はv0ではスキップ
        if ((p.kind == "Pier" || p.kind == "Pole") && !p.address.empty() &&
            linkedAddresses.count(p.address) > 0)
            continue;  // edge厳密紐付け済み、TrackPluginPlacement側で描画する
        if (p.kind == "Pier" || p.kind == "Pole") {
            // 軌道へ紐付かない橋脚・架線柱も、描画キャッシュと同じ表記で保持する。
            // 橋脚の接合点高さは絶対座標のまま使う。
            p.pluginFolderUtf8 = registrar.Register(p.pluginFolderUtf8);
        }
        result.placements.push_back(std::move(p));
    }
}

using PoleByAddress =
    std::unordered_map<rs::legacy_layout::Rs2Address, const rs::legacy_layout::Rs2PlacedInstance*>;
using PoleFolderMap = std::unordered_map<rs::legacy_layout::Rs2Address, std::string>;
using WorldAnchoredPoleMap =
    std::unordered_map<rs::legacy_layout::Rs2Address, ImportedWorldAnchoredPole>;

// 複線一括Pole(TrackNum>1)は架線接続点がPole原点から
// 左右にオフセットされた複数点を持つ(WireLateralOffsetsFor、EditorViewport.cppの
// native径間走査と同じ規約)。TrackNum/TrackIntervalを引くにはPole側の
// TrackPluginEntryが要るが、Pole配置はSceneRegistry経由でPluginSelectorの
// TrackPluginEntries()キャッシュを経由しないため、ここで明示的にロードする
// (Line.Link0/Link1の第2フィールド(branchIdx)がPole側のどの接続点かを指す。
// 複線用(TrackNum=2)の架線柱プラグインでは、branchIdx=0/1が両方出現する)。
PoleFolderMap LoadPoleFolders(const PoleByAddress& poleByAddress,
                              const std::string& installRootUtf8, PluginSelector& selector) {
    PoleFolderMap poleFolderByAddress;
    for (const auto& [addr, p] : poleByAddress) {
        const std::string folder =
            rs::legacy_layout::ResolveTrackPluginFolder(installRootUtf8, "Pole", p->pluginName);
        if (folder.empty()) continue;  // PolePlugin=""(架線専用アンカー)等はオフセット無し扱い
        std::string err;
        // 正規化後のWireLateralOffsetsForが参照するため、Poleのentryも事前にロードする。
        const std::optional<std::string> registered = selector.EnsureTrackPluginLoaded(folder, err);
        poleFolderByAddress[addr] = registered.value_or(folder);
    }
    return poleFolderByAddress;
}

// PoleLinkでedgeへ厳密紐付けされたPole Address。ここに無いPoleは軌道に載っておらず
// (レイアウト作成時に手動設置されたPole等)、正規化後のPlacedIndividualにもならない。
std::unordered_set<std::string> CollectTrackAnchoredPoles(const rs::world::TrackGraph& graph) {
    std::unordered_set<std::string> trackAnchoredPoles;
    for (const auto& [guid, edge] : graph.edges) {
        (void)guid;
        for (const rs::world::ImportedPluginLink& link : edge.importedPoles)
            if (!link.address.empty()) trackAnchoredPoles.insert(link.address);
    }
    return trackAnchoredPoles;
}

// 軌道に載らないPoleを、絶対座標を持つ個体として登録する(登録済み/軌道紐付け済みは無視)。
void AppendWorldAnchoredPole(const rs::legacy_layout::Rs2PlacedInstance& pole,
                             const PoleFolderMap& poleFolderByAddress,
                             const std::unordered_set<std::string>& trackAnchoredPoles,
                             WorldAnchoredPoleMap& out) {
    if (trackAnchoredPoles.count(pole.address) > 0 || out.count(pole.address) > 0) return;
    ImportedWorldAnchoredPole worldAnchoredPole;
    worldAnchoredPole.address = pole.address;
    worldAnchoredPole.individual.id = rs::world::GenerateGuid();
    worldAnchoredPole.individual.hasWorldAnchor = true;
    // Pole配置のposは絶対座標。軌道紐付けPoleもNormalizeImportedGroundBasisで
    // 取り込み前のワールド座標へ戻されるため、そのまま保持すれば高さが揃う。
    worldAnchoredPole.individual.worldPos = pole.pos;
    // 柱の姿勢はOrigDirの方位だけで決まる(架線の接続点の横並びにも使う)。
    worldAnchoredPole.individual.worldYawDeg = rs::world::HeadingDeg(pole.dir);
    const PoleFolderMap::const_iterator folderIt = poleFolderByAddress.find(pole.address);
    if (folderIt != poleFolderByAddress.end()) {
        worldAnchoredPole.individual.folderUtf8 = folderIt->second;
    }
    out.emplace(pole.address, std::move(worldAnchoredPole));
}

// 架線スパン(Scene直下LineList{Line{Link0/Link1}})をPole Addressで解決し、
// 正規化後のWireSpan端点参照へ引き継ぐ。
// Link0/Link1のconnectorフィールドはRailWayと同じ構造だが
// 参照先はRailConnectorではなくPoleのAddress(実データ検証済み)。
// PolePlugin=""の中間アンカーもここでは対象に含める(SceneEntity化はスキップ
// されているが、架線の端点としては有効)。
void NormalizeWireLines(const rs::legacy_layout::Rs2Scene& scene,
                        const std::string& installRootUtf8, PluginSelector& selector,
                        Rs2SceneImportResult& result) {
    PoleByAddress poleByAddress;
    for (const rs::legacy_layout::Rs2PlacedInstance& p : scene.placements) {
        if (p.kind != "Pole" || rs::legacy_layout::Rs2AddressIsNull(p.address)) continue;
        poleByAddress[p.address] = &p;
    }
    const PoleFolderMap poleFolderByAddress =
        LoadPoleFolders(poleByAddress, installRootUtf8, selector);
    const std::unordered_set<std::string> trackAnchoredPoles =
        CollectTrackAnchoredPoles(result.graph);

    std::vector<ImportedWireLine> normalizedLines;
    normalizedLines.reserve(scene.lines.size());
    WorldAnchoredPoleMap worldAnchoredPolesByAddress;
    for (const rs::legacy_layout::Rs2Line& l : scene.lines) {
        const PoleByAddress::const_iterator itA = poleByAddress.find(l.link0.connector);
        const PoleByAddress::const_iterator itB = poleByAddress.find(l.link1.connector);
        if (itA == poleByAddress.end() || itB == poleByAddress.end()) {
            result.warnings.push_back("Line " + l.address + ": Pole Address解決失敗、架線スパンをスキップ");
            continue;
        }
        const std::string lineFolder = selector.RegisteredTrackPluginFolder(
            rs::legacy_layout::ResolveTrackPluginFolder(installRootUtf8, "Line", l.linePluginName));
        if (lineFolder.empty()) continue;  // 不足プラグインはv0ではスキップ

        // 両端とも軌道に載っているときだけ、Pole個体のGuid参照へ正規化する。片端でも
        // 載っていない径間は個体化できる端点が無いので、絶対座標を焼き込んだ静的な
        // 径間として残す(線形編集には追従しない)。
        AppendWorldAnchoredPole(*itA->second, poleFolderByAddress, trackAnchoredPoles,
                                worldAnchoredPolesByAddress);
        AppendWorldAnchoredPole(*itB->second, poleFolderByAddress, trackAnchoredPoles,
                                worldAnchoredPolesByAddress);
        normalizedLines.push_back(
            {l.link0.connector, l.link0.branchIdx, l.link1.connector, l.link1.branchIdx, lineFolder});
    }

    std::vector<ImportedWorldAnchoredPole> worldAnchoredPoles;
    worldAnchoredPoles.reserve(worldAnchoredPolesByAddress.size());
    for (const auto& [address, pole] : worldAnchoredPolesByAddress) {
        (void)address;
        worldAnchoredPoles.push_back(pole);
    }
    result.placements.erase(
        std::remove_if(result.placements.begin(), result.placements.end(),
                       [&worldAnchoredPolesByAddress](
                           const rs::legacy_layout::ImportedPlacement& placement) {
                           return placement.kind == "Pole" &&
                                  worldAnchoredPolesByAddress.count(placement.address) > 0;
                       }),
        result.placements.end());
    result.overlay = NormalizeImportedPlacements(
        result.graph, normalizedLines, result.warnings, worldAnchoredPoles);
}

} // namespace

Rs2SceneImportResult ImportRs2Scene(const rs::legacy_layout::Rs2Scene& scene,
                                    const std::string& installRootUtf8, PluginSelector& selector) {
    Rs2SceneImportResult result;

    std::vector<std::string> importWarnings;
    // 駅の動く部品に追従するレールがあるときだけ、SplitListとコネクタを休止ワールドにそろえた複製で取り込む。
    const std::vector<rs::legacy_layout::ParentedRailWay> parented =
        rs::legacy_layout::FindParentedRailWays(scene, MakeStationParentLookup(installRootUtf8),
                                                importWarnings);
    rs::legacy_layout::Rs2Scene parentedScene;
    const rs::legacy_layout::Rs2Scene* trackScene = &scene;
    if (!parented.empty()) {
        parentedScene = scene;
        rs::legacy_layout::ApplyParentedRailWays(parentedScene, parented);
        trackScene = &parentedScene;
    }
    result.graph = rs::legacy_layout::ImportTrackGraph(*trackScene, installRootUtf8, importWarnings,
                                                        &result.addresses,
                                                        &result.pendingTrainRoutines);
    ApplyParentLinksToGraph(result.graph, result.addresses, parented, scene.address);
    result.warnings.insert(result.warnings.end(), importWarnings.begin(), importWarnings.end());

    std::vector<std::string> placementWarnings;
    std::vector<rs::legacy_layout::ImportedPlacement> placements =
        rs::legacy_layout::ImportPlacements(scene, installRootUtf8, placementWarnings);
    result.warnings.insert(result.warnings.end(), placementWarnings.begin(), placementWarnings.end());

    result.nodeCount = result.graph.nodes.size();
    result.edgeCount = result.graph.edges.size();
    result.placementCount = placements.size();

    FolderRegistrar registrar(result, selector);
    RegisterEdgePluginFolders(result.graph, registrar, selector);

    // trackPlugins読込完了後(=Rail/Tie高さがselector経由で引ける状態)でノード/edge
    // 標高を地面基準へ正規化する(NormalizeImportedGroundBasis参照)。
    NormalizeImportedGroundBasis(result.graph, selector);

    BakeStandalonePlacements(placements, result, registrar);
    NormalizeWireLines(scene, installRootUtf8, selector, result);

    return result;
}

std::vector<rs::world::SceneEntity> SceneEntitiesFromImportedPlacements(
    const std::vector<rs::legacy_layout::ImportedPlacement>& placements,
    const std::string& sceneKeyUtf8) {
    // guidは住所から導出する安定した値(世代番号は挟まない)。
    //
    // 同じ住所でも中身(folderUtf8)が別プラグインに差し替わったエンティティは、
    // SceneRenderer::Sync側で、guid一致に加えfolderUtf8/switchOptions/categoryの
    // 内容比較でstale-contentとして検知し、正しくparkされる。中身が変わっていない配置は
    // guidも内容も一致するため再ロードされない(保管(park)/復活(revive)機構、
    // SceneSlotPool参照)。guidに世代番号を挟んで強制的に一意化すると、同じSceneへ
    // 戻るだけで全配置が毎回「削除→新規追加」になって再ロードされるため、挟まない。
    std::vector<rs::world::SceneEntity> entities;
    entities.reserve(placements.size());
    // 同一住所(address)が複数配置を指すことは通常無いが、保険として出現順を
    // guid種に含める(住所重複時にguidが衝突しないため)。
    std::unordered_map<std::string, int> occurrenceByAddress;
    for (size_t i = 0; i < placements.size(); ++i) {
        const rs::legacy_layout::ImportedPlacement& p = placements[i];
        rs::world::SceneEntity e;
        e.folderUtf8 = p.pluginFolderUtf8;
        e.sourceAddress = p.address;
        e.category = CategoryForKind(p.kind);
        e.placement = p.placement;
        e.switchOptions = p.switchOptions;
        const int occurrence = occurrenceByAddress[p.address]++;
        e.guid = ImportedPlacementGuid(sceneKeyUtf8, p.kind, p.address, occurrence);
        entities.push_back(std::move(e));
    }
    return entities;
}

rs::world::Guid ImportedPlacementGuid(const std::string& sceneKeyUtf8, const std::string& kind,
                                      const std::string& address, int occurrence) {
    return rs::world::GuidFromSeed(sceneKeyUtf8 + "|" + kind + "|" + address + "|" +
                                   std::to_string(occurrence));
}

void ApplyParentLinksToGraph(rs::world::TrackGraph& graph, const rs::legacy_layout::AddressMap& addresses,
                             const std::vector<rs::legacy_layout::ParentedRailWay>& parented,
                             const std::string& sceneKeyUtf8) {
    for (const rs::legacy_layout::ParentedRailWay& entry : parented) {
        const auto edgeIt = graph.edges.find(addresses.ResolveWay(entry.wayAddress));
        if (edgeIt == graph.edges.end()) continue;
        // 駅の配置は同じ住所で重複しないので、最初の出現(occurrence=0)のguidを使う。
        const rs::world::Guid stationEntity =
            ImportedPlacementGuid(sceneKeyUtf8, "Station", entry.stationAddress, 0);
        edgeIt->second.parentEntity = stationEntity;
        edgeIt->second.parentPart = entry.partIndex;
        for (rs::world::PlatformStopInfo& platform : graph.platforms)
            if (platform.stationAddress == entry.stationAddress) platform.stationEntity = stationEntity;
    }
    for (rs::world::PlatformStopInfo& platform : graph.platforms) {
        if (!rs::world::GuidIsNil(platform.stationEntity)) continue;
        const rs::world::StationInfo* station = nullptr;
        for (const rs::world::StationInfo& candidate : graph.stations) {
            if (candidate.id == platform.stationId) {
                station = &candidate;
                break;
            }
        }
        if (station == nullptr || station->sourceAddress.empty()) continue;
        platform.stationEntity = ImportedPlacementGuid(sceneKeyUtf8, "Station",
                                                       station->sourceAddress, 0);
    }
}

} // namespace rs::app
