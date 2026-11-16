//
// ViewportSceneryInstances.h - 個別配置物(Scenery)の静的インスタンスバッチの収集と配置の登録
//
// RebuildDiscretePlacementInstances(ViewportPlacementInstances.cpp)から呼ばれる。
// _CAMDISTまたは共有状態(_NIGHT/_SEASON/_SHADOW/_ENVMAP)を参照する配置(バリアントが複数)はバリアント別の
// バッチキーで管理し、配置はStaticBandedSceneryInstances(SceneRenderer所有)へ登録する。
//
#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../../render/mesh/Mesh.h"
#include "../../world/scene/SceneRegistry.h"
#include "EditorViewport.h"

namespace rs::app {

// GPUへ載せるバッチ(キーと、焼き込み済みの組立メッシュへのポインタ)。
struct PendingInstanceBatch {
    std::string key;
    const rs::render::Mesh* mesh;
};

// バリアント切り替え対象の基底キー→StaticBandedSceneryInstancesのグループ番号。
using BandedGroupIndexMap = std::unordered_map<std::string, size_t>;

// SceneRegistryのScenery全件と、信号プラグインのモデル(SceneRenderer::SignalModels。
// SceneRegistryには入れない)から、必要なバッチ(静的メッシュ)をpendingへ積む。
// 孤立Pierはtrack plugin側と同じキー規則でtrackPluginSeenへ載せる。
// バリアントが複数あって切り替え対象のもの(StaticMeshVariantSet::NeedsVariantSwitching)は
// バリアントごとのキーで積み、グループをSceneRendererのバリアント管理へ登録して
// bandedGroupsへ控える(呼び出し前にReset済みであること)。
// 積んだバッチ数(バリアント含む)をoutBatchCountへ加算する。
void CollectSceneryBatches(const EditorViewportContext& ctx,
                           std::unordered_set<std::string>& trackPluginSeen,
                           std::vector<PendingInstanceBatch>& pending,
                           BandedGroupIndexMap& bandedGroups, int& outBatchCount);

// SceneRegistryのScenery全件と信号プラグインのモデルの配置を登録する。バリアント管理対象はAddMember、それ以外は
// batchOkが真のキーへAddInstanceする。登録した配置数を返す。
int AddSceneryInstances(const EditorViewportContext& ctx,
                        const std::unordered_map<std::string, bool>& batchOk,
                        const BandedGroupIndexMap& bandedGroups);

// スイッチ値だけが変わった個体について、旧キーと新キーのバッチだけを積み直す
// (RebuildDiscretePlacementInstancesの全再構築を避ける)。changesは
// SceneRegistry::TakePendingSwitchChanges()の結果に、SignalModelSet::TakePendingSwitchChanges()の
// 結果を足したもの(個体はどちらからでも引く)。
// 次のどれかに当たったら何もせずfalseを返す(呼び出し側は全再構築へ戻す):
// 個体が見つからない/Sceneryでない・孤立Pier/旧キーか新キーがバリアント切り替え対象
// (NeedsVariantSwitching)/バリアントが1個でない。
// 静的化の判定を通らない(スロット経路の)キーはバッチを持たないので、全再構築と同じく対象外として扱う。
bool ApplySwitchChangesToSceneryInstances(
    const EditorViewportContext& ctx,
    const std::vector<rs::world::SceneRegistry::SwitchChange>& changes);

bool ApplyPlacementChangesToSceneryInstances(
    const EditorViewportContext& ctx,
    const std::vector<rs::world::SceneRegistry::PlacementChange>& changes);

} // namespace rs::app
