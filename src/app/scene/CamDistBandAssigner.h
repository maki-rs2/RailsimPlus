//
// CamDistBandAssigner.h - 静的配置ごとにバリアント(_CAMDISTのバンド×共有状態)を決め、変化を知らせる
//
// GPU・プラグイン読込に依存しない純粋なクラス。バリアント集合(=基底キー)ごとのグループへ
// 配置(メンバ)を登録しておき、視点が動くたびに全メンバのバンドを再計算する。
// 位置・関数はスロット経路(UpdateSceneryCamDist)と同じ
// ComputeScaledCamDistance / QuantizeCamDistToBand を使うので、結果は食い違わない。
// 共有状態(_NIGHT/_SEASON/_SHADOW/_ENVMAP)はシーン全体で同じ値なので、グループごとに状態スロットを1つ引き、
// バンドと合わせてバリアントを決める。共有状態が前回から変わったときは更新間隔を待たない。
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../../world/time/SceneSharedSwitches.h"
#include "StaticMeshVariantSet.h"

namespace rs::app {

class CamDistBandAssigner {
public:
    // バリアント集合1つぶんのグループを登録し、グループ番号を返す。
    // bandStartsは昇順(_CAMDISTを参照しなければ空。その場合メンバの位置はバリアントに使わない)。
    // variantOfBandは「状態スロット×バンド数+バンド番号」→バリアントの添字
    // (StaticMeshVariantSet::variantOfBand)、sharedStateMaskは参照している共有状態の軸。
    // variantCountが1以下ならバッチキーはbaseKeyのまま(MakeStaticVariantKey)。
    size_t AddGroup(std::string baseKey, std::vector<int32_t> bandStarts,
                    std::vector<uint32_t> variantOfBand, size_t variantCount,
                    uint32_t sharedStateMask = 0);

    // groupIndexのグループへ配置を1つ足し、メンバ番号を返す。positionは配置のワールド行列の
    // 平行移動成分。次のDueForUpdateは必ず真になる。
    size_t AddMember(size_t groupIndex, const float position[3]);

    // グループもメンバも割り当ても全部消す。次のDueForUpdateは必ず真になる。
    void Clear();

    // 更新間隔の判定。初回(Clear後・メンバ追加後を含む)は必ず真。intervalSeconds<=0は常に真
    // (毎フレーム更新)。最後のUpdateからintervalSeconds以上たっていれば真。
    // 登録済みグループが参照する共有状態(sharedの該当軸)が最後のUpdateから変わっていれば、
    // 間隔に関わらず真。
    bool DueForUpdate(double nowSeconds, double intervalSeconds,
                      const rs::world::SceneSharedSwitchValues& shared) const;

    // 全メンバのバンドを再計算し、割り当てが変わったバッチキーの集合を返す。変わったメンバの
    // 古いキーと新しいキーの両方を含む。まだ割り当てのないメンバは新しいキーだけを含む。
    // nowSecondsはDueForUpdateの基準時刻として記録する。
    // sharedは現在の共有状態の値。
    std::unordered_set<std::string> Update(const float eye[3], float viewerFovYRadians,
                                           double nowSeconds,
                                           const rs::world::SceneSharedSwitchValues& shared);

    // 現在variantKeyに割り当てられているメンバ番号(追加順)。Update前や未知のキーは空。
    const std::vector<size_t>& MembersOfKey(const std::string& variantKey) const;

    // メンバの位置(float[3])。
    const float* MemberPosition(size_t memberIndex) const;

    size_t MemberCount() const { return members_.size(); }

private:
    struct Group {
        // bandStarts/variantOfBand/sharedStateMaskだけ使う(variantsは空)
        StaticMeshVariantSet bandMap;
        std::vector<std::string> variantKeys; // バリアントの添字→バッチキー
    };
    struct Member {
        size_t group = 0;
        float position[3] = {0.0f, 0.0f, 0.0f};
        int64_t assignedVariant = -1; // -1=まだ割り当てなし
    };

    // 全グループの共有状態の軸の和集合に対する、現在値の状態スロット番号。
    size_t UnionSharedSlot(const rs::world::SceneSharedSwitchValues& shared) const;

    std::vector<Group> groups_;
    std::vector<Member> members_;
    std::unordered_map<std::string, std::vector<size_t>> membersByKey_;
    uint32_t unionSharedMask_ = 0; // 全グループのsharedStateMaskの和集合
    size_t lastSharedSlot_ = 0;    // 最後のUpdateで使ったUnionSharedSlot
    bool needsUpdate_ = true;
    double lastUpdateSeconds_ = 0.0;
};

} // namespace rs::app
