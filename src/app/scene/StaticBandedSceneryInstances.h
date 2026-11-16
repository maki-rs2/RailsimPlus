//
// StaticBandedSceneryInstances.h - 静的配置の「バンド・共有状態別バリアント」管理
//
// 静的インスタンス化された情景のうち、_CAMDISTまたは共有状態(_NIGHT/_SEASON/_SHADOW/_ENVMAP)を参照して
// バリアントが2個以上あるもの(StaticMeshVariantSet::NeedsVariantSwitching)だけを扱う。
// 配置ごとにカメラ距離からバンドを決め、シーン共通の共有状態の値から状態を決めて
// (CamDistBandAssigner)、バリアントが変わった配置が入るバッチだけを積み直す。GPUには依存せず、積み直しの操作は関数オブジェクトで受ける
// (実際のInstanceBatchManagerへの結線はSceneRenderer側)。
//
#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

#include "CamDistBandAssigner.h"
#include "StaticMeshVariantSet.h"

namespace rs::app {

class StaticBandedSceneryInstances {
public:
    // 変化したバッチキーの配置を空にする操作(InstanceBatchManager::ClearInstances(keys))。
    using ClearKeysFn = std::function<void(const std::unordered_set<std::string>&)>;
    // バッチキーへ配置を1つ足す操作(InstanceBatchManager::AddInstance)。
    using AddInstanceFn = std::function<void(const std::string&, const float world[16])>;

    // グループもメンバも全部消す(リビルドの先頭で呼ぶ)。視点は保持する。
    void Reset();

    // baseKey(MakeStaticInstanceKey)のバリアント集合を1グループとして登録し、番号を返す。
    size_t AddGroup(const std::string& baseKey, const StaticMeshVariantSet& variants);

    // groupIndexのグループへ配置を足す。位置はworld(列優先4x4)の平行移動成分m[12..14]で、
    // スロット経路(UpdateSceneryCamDist)と同じ。
    void AddMember(size_t groupIndex, const float world[16]);

    // 最新の視点と視野角を保存する(毎フレーム)。
    void SetView(const float eye[3], float viewerFovYRadians);

    size_t MemberCount() const { return assigner_.MemberCount(); }

    // 更新間隔(intervalSeconds)に達していれば全メンバのバリアントを再計算し、変化したキーを
    // clearKeysで空にしてから、そのキーの配置をaddInstanceで積み直す。視点が未設定なら何もしない。
    // sharedは現在の共有状態の値。登録済みグループが参照する軸の値が前回から変わっていたら、
    // 更新間隔を待たずに再計算する。
    // 積み直したキーの数を返す。intervalSeconds<=0は毎フレーム更新。
    size_t ApplyPending(double nowSeconds, double intervalSeconds,
                        const rs::world::SceneSharedSwitchValues& shared,
                        const ClearKeysFn& clearKeys, const AddInstanceFn& addInstance);

private:
    CamDistBandAssigner assigner_;
    std::vector<std::array<float, 16>> worlds_; // メンバ番号→ワールド行列
    float eye_[3] = {0.0f, 0.0f, 0.0f};
    float fovYRadians_ = 0.0f;
    bool hasView_ = false;
};

} // namespace rs::app
