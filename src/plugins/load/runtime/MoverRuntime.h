//
// MoverRuntime.h - 可動部の純CPUアニメ評価層
//
// 描画非依存。ModelPart::customizers（パース済みの可動部定義）を発動文脈
// （常時/ApplySwitch Case/If 条件）ごとにグループ化し、UI からの
// アクティブ切替（トグル）に応じて位相を進め、ローカル変換行列へ合成する。
//
// 行列規約は src/core/math.h の rs::core::Mat4（row-major, row-vector:
// v' = v * M）に合わせる。
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "EaseTable.h"
#include "TrackingMoveSolve.h"
#include "../../schema/PluginSchema.h"
#include "../../schema/SwitchExpr.h"
#include "../../../core/Math.h"

namespace rs::legacy_plugin {

// 発動条件でグループ化された可動部（UIのトグル単位）。
// 同一トリガ（trig.chain が構造的に等しい customizer）は、宣言順が
// 連続していなくても同一グループへまとめる。
struct MoverGroup {
    rs::legacy_pi::TriggerInfo trig; // 空chain = Always
    std::vector<rs::legacy_pi::StaticCustomizer> items; // 宣言順
};

// item 単位のアニメ状態（近似モデル）。
//   phase ∈ [0,1]（0=非適用、1=適用）: Move/Rotate/TrackingMove 用
//   delayRemaining: Pre(Reverse)Delay の残り秒数（位相で動く項目用）
//   target: 現在向かっている先（1=active化、0=inactive化。位相で動く項目用）
//   angleDeg: DynamicRotate 用の累積回転角（active切替でリセットしない）
//   curSpeedDegPerSec: DynamicRotate 用の現在角速度（同上）
struct MoverItemState {
    float phase = 0.0f;
    float delayRemaining = 0.0f;
    float target = 0.0f;
    float angleDeg = 0.0f;
    float curSpeedDegPerSec = 0.0f;
    bool pending = false; // PartMoverRuntime::pendingItems_ に載っているか(二重登録の防止)
    // 位相で適用量を決めるか。ComposeLocal が定義側の大きな StaticCustomizer を読まずに、適用量0の item を
    // 飛ばせるようにここへ写しておく(1パーツに数百の item があると、その読み出しだけで重い)。
    bool usesPhase = false;
};

// item 単位の緩急表。null は直線(緩急の指定なし)。forward=行き、reverse=戻り。
struct MoverItemEase {
    std::shared_ptr<const EaseTable> forward;
    std::shared_ptr<const EaseTable> reverse;
};

// 1パーツ分の可動部ランタイム。
class PartMoverRuntime {
public:
    // part.customizers をトリガ一致でグループ化する。Always グループは
    // Build 時点で即時 active 状態（phase=1）とする。
    void Build(const rs::legacy_pi::ModelPart& part);

    size_t GroupCount() const { return groups_.size(); }
    const MoverGroup& Group(size_t idx) const { return groups_[idx]; }
    // 時間や追従先で動く item を1件でも持つか。Move/Rotateだけなら
    // 目標位相で静止するため、静的インスタンス化の焼き込み対象にできる。
    bool HasTimeDrivenItems() const;
    bool HasTrackingMoves() const { return !trackingMoves_.empty(); }
    bool IsGroupActive(size_t idx) const { return idx < active_.size() && active_[idx]; }

    // UI からの目標状態切替（トグル）。同じ状態を渡しても現在位相からの
    // 継続（途中反転）を許すため無害。
    void SetGroupActive(size_t groupIdx, bool active);

    // 全グループの発動判定を行い SetGroupActive へ反映する（判定は
    // rs::legacy_pi::IsTriggerActive で trig.chain の全ステップをAND評価する。
    // ネストした If/ApplySwitch にも対応。エッジトリガ: 実際に状態が変化した
    // 時だけ SetGroupActive を呼ぶ、詳細は .cpp 参照）。
    // generation は PluginSession::switchGeneration。前回呼び出しと同じ
    // generationの間はスイッチ値が変わっていないため判定自体を丸ごとスキップする
    // (active_[]は前回の判定結果のまま=変化していないので何もしなくて正しい。
    // Moverが数百グループあるプラグインでは、毎フレームの再評価が
    // 重さの一因になる)。
    void UpdateActivation(const rs::legacy_pi::SwitchResolver& resolve, uint64_t generation);

    // 全グループ・全 item が「これ以上状態が変化しない」静止状態かを返す。
    // 位相で動く項目は phase==target かつ delayRemaining<=0、TrackingMove はさらに phase==0、
    // DynamicRotate は現在角速度0かつグループ非active(activeなら以後
    // 目標速度が非0になることがあるため静止扱いしない)であること。true の間は
    // Update/ComposeLocal を呼んでも結果が変わらないため、呼び出し側で
    // 再計算をスキップしてよい。
    // 実体はキャッシュ値 settled_ を返すだけの O(1) 参照(Build/SetGroupActive/
    // Update が状態変更の唯一の入口であり、それぞれが settled_ と pendingItems_ を更新する)。
    bool Settled() const { return settled_; }

    // 各グループ・各 item の位相を dt[秒] だけ進める。
    // dynamicSpeedScale: DynamicRotate の目標速度に掛ける倍率（UI の再生ON/OFF・
    // 速度倍率スライダ用。0 なら回転を止める。Move/Rotate には影響しない）。
    // clockSecondsOfDay: AnalogClock用。ゲーム内時刻の日内経過秒
    // (GameClock::SecondsOfDay()相当、[0,86400))。PI単体表示等ゲーム内時刻を
    // 持たない文脈では0を渡す(=時刻停止、RailSim II の g_RSPV 挙動に相当)。
    void Update(float dt, float dynamicSpeedScale, double clockSecondsOfDay);

    // 全グループ・全 item の変換を宣言順に合成したローカル行列を返す
    // （row-major, row-vector: v' = v * out）。
    // TrackingMove の位相が0でない間は追従先を調べ直し、それ以外は状態が変わるまで前回の結果を返す。
    void ComposeLocal(float out[16], const rs::core::Mat4& base = rs::core::Mat4::Identity(),
                      const TrackingTargetTable* targets = nullptr,
                      const rs::core::Mat4* placementToWorld = nullptr);

private:
    // まだ状態が変化する item の位置。Update はここに載っている item だけを進める。
    struct PendingItem {
        uint32_t group = 0;
        uint32_t item = 0;
    };

    void MarkItemPending(size_t groupIdx, size_t itemIdx);

    // 1item分の「これ以上状態が変化しない」静止判定(Settled()の判定ロジック
    // そのもの)。Build/Update から共通で呼べるようにヘルパへ切り出したもの。
    static bool ItemSettled(const rs::legacy_pi::StaticCustomizer& item,
                             const MoverItemState& state, bool groupActive);

    // 線形に進む位相を、行き(target=1)/戻り(target=0)の緩急表で見た目の進み具合へ直す。
    float EasedPhase(size_t groupIdx, size_t itemIdx, float phase) const;

    // 1item分の位相を dt[秒] だけ進める(Update の内側ループ本体)。
    // 「更新して抜ける」経路が複数あるため独立した関数に切り出してある。
    // ここをUpdate内のループへ戻すと、早期returnのたびに settled_ の判定を
    // 書き足す必要が生じ、1つ書き忘れただけでキャッシュが古い true のまま
    // 固まる(=可動部が動かなくなる)。判定はUpdate側の1箇所に留めること。
    static void UpdateItem(const rs::legacy_pi::StaticCustomizer& item, MoverItemState& state,
                           bool groupActive, float dt, float dynamicSpeedScale,
                           double clockSecondsOfDay);

    std::vector<MoverGroup>        groups_;
    std::vector<rs::legacy_pi::TrackingMoveCustomizer> trackingMoves_;
    std::vector<bool>              active_;   // グループごとの目標状態
    std::vector<std::vector<MoverItemState>> itemStates_; // groups_[g].items と1:1
    std::vector<std::vector<MoverItemEase>> itemEases_;   // groups_[g].items と1:1
    uint64_t cachedGeneration_ = ~uint64_t(0);

    // 静止していない item の一覧。定義に数千の Case を持つプラグインでも、1回の発動で
    // 動く item はごく一部なので、全 item を毎回走査しない。載っていない item は
    // ItemSettled が true で、UpdateItem を呼んでも状態が変わらない。
    std::vector<PendingItem> pendingItems_;

    // ComposeLocal の合成結果。位相だけでなく緩急表の向きも target で変わるので、
    // 状態を変える入口(Build/SetGroupActive/Update)が必ず localDirty_ を立てる。
    rs::core::Mat4 cachedLocal_ = rs::core::Mat4::Identity();
    bool localDirty_ = true;

    // Settled() が返すキャッシュ値。状態を変える入口(Build/SetGroupActive/
    // Update)が必ず更新する。安全側は false(=念のため再計算させる)であり、
    // true を置き間違えると Update が呼ばれなくなり可動部が永久に静止したまま
    // 動かなくなる(false側の誤りは単に無駄な再計算で済むのに対し、true側の
    // 誤りは見た目のバグに直結するという非対称なリスクがある)。
    bool settled_ = true;
};

} // namespace rs::legacy_plugin
