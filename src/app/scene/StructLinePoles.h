//
// StructLinePoles.h - Structの架線接続点(LinePoint)を、架線の端点になる柱として導出する純関数
//
// 柱は保存せず、置かれたStructと定義のLinePointから毎回導出する。導出した柱は
// folderが空のワールドアンカー個体(PlacedIndividual)で、Guidはエンティティと添字から決まる。
// 架線(WireSpan)はこのGuidで柱を指す。D3D12・GUIに依存しない。
//
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

#include "../../world/alignment/TrackGraph.h"
#include "../../world/scene/ManualPlacementOverlay.h"

namespace rs::legacy_pi { struct LinePointDef; }
namespace rs::world { struct SceneEntity; }

namespace rs::app {

// プラグインフォルダからStructのLinePoint一覧を引く関数。Structでない・LinePointが無いときはnullptr。
using LinePointLookup =
    std::function<const std::vector<rs::legacy_pi::LinePointDef>*(const std::string& folderUtf8)>;

// 導出した柱のGuid。エンティティのGuidとLinePointの添字から毎回同じ値になる。
rs::world::Guid StructLinePoleId(const rs::world::Guid& entityGuid, std::size_t index);

// Scenery区分のエンティティごとに、LinePointをワールド座標へ変換して柱にする。
// 位置はCoordをエンティティの行列で変換した値、worldYawDegはDirを回した向き(+Zが0度)。
std::vector<rs::world::PlacedIndividual> DeriveStructLinePoles(
    const std::vector<rs::world::SceneEntity>& entities, const rs::world::TrackGraph& graph,
    const LinePointLookup& lookup);

// 柱のGuid一覧(SanitizeOverlayへ渡す導出した柱の集合)。
std::unordered_set<rs::world::Guid, rs::world::GuidHash> CollectPoleIds(
    const std::vector<rs::world::PlacedIndividual>& poles);

// folderが空のワールドアンカー柱(プラグインなしの柱)のうち、導出した柱の位置とtoleranceM以内で
// 重なるものを導出した柱へ置き換える。overlay.linesの端点を導出した柱のGuidへ書き換え、元の柱は
// 消す。一致しない柱はそのまま残す。何度呼んでも結果は同じ。付け替えた柱の数を返す。
int BindPlainPolesToStructLinePoles(rs::world::ManualPlacementOverlay& overlay,
                                    const std::vector<rs::world::PlacedIndividual>& derived,
                                    double toleranceM = 0.01);

}  // namespace rs::app
