//
// Rs2PlatformParent.h - 駅プラグインの Platform の ParentObject(動く部品に追従するレール)を持つ
// .rs2 のレールを、取り込み前の Rs2Scene の段階で休止ワールド姿勢へそろえる。
//
// .rs2 の保存形式は座標系が2種類ある。親つきレールの SplitList は親オブジェクトのローカル座標
// (基準)、RailConnector の Splitter は保存時点のワールド座標(動いた後)。親つきかどうかは .rs2 に
// 書かれず、Station の PlatformList に載るレールが、駅プラグインの同じ番号(定義順)の Platform に
// ParentObject を持つかで決まる。ここでは SplitList を休止ワールドへ変換し、コネクタの位置は
// 保存値を使わず変換後の端点で置き換える。親つきでないレールと共有するコネクタは切り離す。
//
// 駅プラグインの定義ファイルの読込には依存しない。親の情報は呼び出し側が StationParentLookup で渡す。
//
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "../../core/Math.h"
#include "../../world/alignment/RigidPose.h"
#include "Rs2Types.h"

namespace rs::legacy_layout {

// 駅プラグインの1つの Platform(定義順)の親の指定。
struct StationPlatformParent {
    int  partIndex = -1;       // 親の部品の添字。-1=親なし
    bool undefined = false;    // ParentObject を書いたが部品が見つからない(読込エラー相当)
};

struct StationParentInfo {
    std::vector<StationPlatformParent> platforms;  // 定義順
    std::vector<rs::core::Mat4> partRestMatrices;  // 部品の添字 → 休止行列(ModelPart::worldMat)
};

// 駅の配置から親の情報を返す。親を持つ番線が1つも無い駅は nullopt。
using StationParentLookup = std::function<std::optional<StationParentInfo>(const Rs2PlacedInstance&)>;

// 親つきになる RailWay 1本。
struct ParentedRailWay {
    Rs2Address wayAddress;
    Rs2Address stationAddress;
    int        partIndex = -1;
    // 休止ワールド姿勢(部品の休止行列 × 駅の配置の行列を正規化したもの)。
    rs::world::RigidPose restPose;
};

// scene を変えずに、親つきになる RailWay を求める。解決できない番線・レールは対象外にして
// warnings に積む(そのレールは通常のレールとして取り込まれる)。
std::vector<ParentedRailWay> FindParentedRailWays(const Rs2Scene& scene,
                                                  const StationParentLookup& lookup,
                                                  std::vector<std::string>& warnings);

// FindParentedRailWays の結果で scene を書き換える。SplitList を休止ワールドへ変換し、両端の
// コネクタの Splitter を変換後の端点にそろえる。同じ駅・同じ部品の親つきレールだけが刺さる
// コネクタはそのまま使い、それ以外のレールも刺さるコネクタは、親つきレールの分を新しい
// コネクタへ切り離す。
void ApplyParentedRailWays(Rs2Scene& scene, const std::vector<ParentedRailWay>& parented);

} // namespace rs::legacy_layout
