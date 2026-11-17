//
// Rs2SceneApplier.h - Rs2Scene 1個分をTrackGraph/配置物/架線スパンへ変換し(ImportRs2Scene)、
// PluginSelectorへのtrackPlugins一括登録+配置物のSceneEntity変換(SceneEntitiesFromImportedPlacements)
// を行う(app層のつなぎ役)。複数Scene対応は
// 呼び出し側のLayoutSession::Loadが担う。
//
// plugins/layout(rs::legacy_layout)はappへ依存しないため、TrackGraph/ImportedPlacementを
// 実際のGUI状態(PluginSelector/SceneRegistry)へ反映するオーケストレーションは
// このファイルが担う。
//
#pragma once

#include <string>
#include <vector>

#include "../../core/Math.h"
#include "../../plugins/layout/Rs2Import.h"
#include "../../plugins/layout/Rs2PlatformParent.h"
#include "../../world/alignment/TrackGraph.h"
#include "../../world/scene/ManualPlacementOverlay.h"

namespace rs::world { struct SceneEntity; }

namespace rs::world {
class SceneRegistry;
}

namespace rs::app {

class PluginSelector;

// Scene直下 LineList{Line{Link0/Link1}} 由来の架線スパン1本(隣接Pole間。
// 本数/位置はRailSim IIと厳密に一致させる)。
// 両端のPoleが軌道に載っている(PoleLinkでedgeへ紐付いている)径間は、取り込み時に
// ManualPlacementOverlay::lines のGuid参照へ正規化される。この型に残るのは、片端でも
// 軌道に載っていない径間だけで、取り込み時の絶対座標を焼き込んだ静的な径間として描く
// (線形編集に追従しない)。
struct ImportedWireSpan {
    rs::core::Vec3 posA, posB;  // 取り込み時の絶対座標
    std::string    lineFolderUtf8;
};

// ImportRs2Scene の結果(1 Scene分。Editor/SceneRegistryへは依存しないので、切替処理を単体でテストしやすい)。
struct Rs2SceneImportResult {
    rs::world::TrackGraph graph;
    rs::world::ManualPlacementOverlay overlay;
    // 二重焼き込み除外(edge厳密紐付け済みPier/Pole)・不足プラグイン除外を
    // 適用済みの配置物リスト。呼び出し側がSceneRegistryへ投入する。
    std::vector<rs::legacy_layout::ImportedPlacement> placements;
    std::vector<ImportedWireSpan> wireSpans;
    size_t nodeCount = 0, edgeCount = 0, placementCount = 0;
    size_t pluginFoldersLoaded = 0, pluginFoldersFailed = 0;
    // 読めなかったレール系プラグインのフォルダ(warningsに理由を載せたもの)。
    std::vector<std::string> failedPluginFolders;
    std::vector<std::string> warnings;
    // このSceneのAddress→Guid解決マップ(WarpList取り込み用)。
    rs::legacy_layout::AddressMap addresses;
    rs::legacy_layout::Rs2PendingTrainRoutines pendingTrainRoutines;
};

// Rs2Scene 1個分を TrackGraph + 配置物リスト + 架線スパンへ変換する
// (Editor非依存の純関数。複数Scene対応として、レイアウト内の全Sceneを
// この関数で個別に変換してから1個をアクティブへ適用する)。
// selector: TrackPluginEntries()キャッシュへ folderUtf8 を登録する(EnsureTrackPluginLoaded)。
// 複数Sceneで同じフォルダを参照してもキャッシュは共有され、二重ロードにはならない。
Rs2SceneImportResult ImportRs2Scene(const rs::legacy_layout::Rs2Scene& scene,
                                    const std::string& installRootUtf8, PluginSelector& selector);

// ImportRs2Scene() の placements を SceneEntity へ変換する(registryへは投入しない)。
// guidは住所(address)・所属Scene(sceneKeyUtf8)・
// 配置種別・同一住所内の出現順から rs::world::GuidFromSeed() で毎回同じ結果になるように導出する。同じ.rs2を読み直しても同じ配置には
// 同じguidが振られ、読み込みのたびに安定する。sceneKeyUtf8はRs2Scene::addressを
// 渡す想定(Scene間でaddressが衝突してもguidが混線しないための区別用)。
std::vector<rs::world::SceneEntity> SceneEntitiesFromImportedPlacements(
    const std::vector<rs::legacy_layout::ImportedPlacement>& placements,
    const std::string& sceneKeyUtf8);

// 取り込んだ配置の guid(SceneEntitiesFromImportedPlacements が各配置に振る値)。
// occurrence は同じ住所の配置の出現順(0始まり)。
rs::world::Guid ImportedPlacementGuid(const std::string& sceneKeyUtf8, const std::string& kind,
                                      const std::string& address, int occurrence);

// 駅の動く部品に追従するレール(FindParentedRailWays の結果)の edge に parentEntity/parentPart を、
// その駅の番線に stationEntity を載せる。ImportTrackGraph の後、駅の配置の guid が決まる
// sceneKeyUtf8(Rs2Scene::address)で呼ぶ。
void ApplyParentLinksToGraph(rs::world::TrackGraph& graph, const rs::legacy_layout::AddressMap& addresses,
                             const std::vector<rs::legacy_layout::ParentedRailWay>& parented,
                             const std::string& sceneKeyUtf8);

} // namespace rs::app
