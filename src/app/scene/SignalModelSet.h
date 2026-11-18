//
// SignalModelSet.h - 信号プラグインのモデルを描くための、信号ごとの描画用エンティティ一式
//
// 信号(SpeedSignal)の位置にStationプラグインのモデルを置き、現示に合わせてスイッチを切り替える。
// SceneRegistryには入れない(保存・シーン切替・情景編集に巻き込まないため)。描画だけ
// ViewportSceneryInstances が Entities() を SceneRegistry の個体と同じ経路で積む。
// クリックの候補には SceneSceneryPicker が毎回Entities()から作って足す(鑑賞モードで信号を選ぶため)。
// GPUにもSceneRendererにも依存しない(単体テスト用)。
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../plugins/schema/sidecar/StationSidecar.h"
#include "../../world/scene/SceneEntity.h"
#include "../../world/scene/SceneRegistry.h"
#include "../../world/alignment/Guid.h"
#include "../../world/alignment/PlatformRoutine.h"
#include "../../world/alignment/SpeedSignal.h"

namespace rs::world {
struct TrackGraph;
} // namespace rs::world

namespace rs::legacy_pi { struct SwitchDef; }

namespace rs::app {

// 信号モデルが外から引くもの。実体はSceneRenderer(station.jsonとスイッチ定義のキャッシュ)と
// SignalAspectRuntime(現示)。テストではスタブを渡す。
struct SignalModelSources {
    std::function<const rs::legacy_pi::sidecar::StationSidecar&(const std::string&)> sidecar;
    // プラグインのスイッチ定義(DefineSwitch宣言順)。読めなければnullptr。
    std::function<const std::vector<rs::legacy_pi::SwitchDef>*(const std::string&)> switchDefs;
    // 信号の表示段(stagesの添字)。状態なし(担当閉塞なし・未計算)はnullopt。
    std::function<std::optional<int>(const rs::world::SpeedSignal&)> displayStage;
};

// 信号のモデルの向き。信号が対象とする向き(direction)に走る列車から、モデルの正面が見えるようにする。
// ResolveWorldMatrixはTrackPlacementのforward(モデルの+Z)を、directionで反転済みの軌道接線
// (=その向きに走る列車の進行方向)に合わせる。列車モデルも正面を+Zに作る
// (FrontCabinの向き)ので、信号の正面も+Zとみなすと、そのままでは進行方向を
// 向いて列車に背を向ける。正面を列車へ向けるため進行方向に対して180度回す。
// 実機で逆に見えたらこの定数の符号(0か180)だけ直す。
inline constexpr float kSignalModelYawOffsetDeg = 180.0f;

class SignalModelSet {
public:
    // 信号モデルの個体のguidは、信号のguidそのもの(再構築しても変わらない)。

    // graphのspeedSignalsのうち、pluginFolderUtf8が空でない信号ごとにエンティティを作り直す。
    // switchOptionsは現示(sources.displayStage)で決めた値で、溜まっているSwitchChangeは捨てる
    // (呼び出し側は直後に全再構築する前提)。
    void Rebuild(const rs::world::TrackGraph& graph, const SignalModelSources& sources);

    // 現示が前回から変わった信号のswitchOptionsを更新し、SwitchChangeを溜める。
    // 表示段が前回と同じ信号は何も計算しない。変わった信号が1つでもあればtrueを返す。
    bool UpdateAspects(const SignalModelSources& sources);

    // 溜まっているスイッチ変更を取り出して空にする(SceneRegistry::TakePendingSwitchChangesと同じ形)。
    std::vector<rs::world::SceneRegistry::SwitchChange> TakePendingSwitchChanges();

    const std::vector<rs::world::SceneEntity>& Entities() const { return entities_; }
    const rs::world::SceneEntity* FindEntity(const rs::world::Guid& id) const;
    // 信号モデルの個体のguidから信号のguidを引く。信号モデルでなければnullopt。
    std::optional<rs::world::Guid> FindSignalGuid(const rs::world::Guid& id) const;

private:
    // 信号1本のswitchOptionsを作る。信号のswitchOptions(ユーザーの値)が土台で、現示の割り当てが
    // 書くスイッチだけを上書きする。割り当て無しは土台のまま。
    std::vector<int32_t> ComputeSwitchOptions(const rs::world::SpeedSignal& signal,
                                              std::optional<int> displayStage,
                                              const SignalModelSources& sources) const;

    std::vector<rs::world::SceneEntity> entities_;
    // entities_と同じ並びで、各信号の元データ(表示段の問い合わせ用)と前回の表示段。
    std::vector<rs::world::SpeedSignal> signals_;
    std::vector<std::optional<int>> lastStages_;
    std::unordered_map<rs::world::Guid, size_t, rs::world::GuidHash> idToIndex_;  // guid -> entities_の添字
    std::vector<rs::world::SceneRegistry::SwitchChange> pendingChanges_;
};

} // namespace rs::app
