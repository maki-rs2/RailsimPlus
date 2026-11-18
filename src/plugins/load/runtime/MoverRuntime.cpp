#include "MoverRuntime.h"

#include "../../../core/Epsilon.h"

#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <numbers>
#include <string>
#include <unordered_map>
#include <utility>
#include "../../schema/PluginSchema.h"

namespace rs::legacy_plugin {

using rs::legacy_pi::StaticCustomizer;

namespace {

// axis を正規化する。ゼロベクトルなら Z 軸（RotationAxis 省略時の既定）に倒す。
void NormalizeAxisOrDefaultZ(float& x, float& y, float& z) {
    float len = std::sqrt(x * x + y * y + z * z);
    if (len < rs::core::kNormalizeLengthEps) {
        x = 0; y = 0; z = 1;
        return;
    }
    x /= len; y /= len; z /= len;
}

// 軸 (x,y,z)（正規化済み）まわり角度 deg[度] の回転行列（row-major, row-vector）。
rs::core::Mat4 AxisAngleRotation(float x, float y, float z, float deg) {
    float rad = deg * std::numbers::pi_v<float> / 180.0f;
    float c = std::cos(rad), s = std::sin(rad), t = 1.0f - c;
    rs::core::Mat4 m = rs::core::Mat4::Identity();
    m.m[0]  = t * x * x + c;
    m.m[1]  = t * x * y + s * z;
    m.m[2]  = t * x * z - s * y;
    m.m[3]  = 0;
    m.m[4]  = t * x * y - s * z;
    m.m[5]  = t * y * y + c;
    m.m[6]  = t * y * z + s * x;
    m.m[7]  = 0;
    m.m[8]  = t * x * z + s * y;
    m.m[9]  = t * y * z - s * x;
    m.m[10] = t * z * z + c;
    m.m[11] = 0;
    m.m[12] = 0; m.m[13] = 0; m.m[14] = 0; m.m[15] = 1;
    return m;
}

rs::core::Mat4 Translation(float x, float y, float z) {
    rs::core::Mat4 m = rs::core::Mat4::Identity();
    m.m[12] = x; m.m[13] = y; m.m[14] = z;
    return m;
}

// 緩急の指定が無い(両端とも0)ときは直線なので表を持たない。
// 表は(始点, 終点)の緩急だけで決まり、作るのに数百マイクロ秒かかる(1000分割×二分探索)。
// カスタマイザが数万個あるプラグインでは同じ値の表を何度も作ることになるので、値ごとに1つを共有する。
std::shared_ptr<const EaseTable> BuildEaseTable(float start, float end) {
    if (start == 0.0f && end == 0.0f) return nullptr;
    static std::mutex cacheMutex;
    static std::map<std::pair<float, float>, std::shared_ptr<const EaseTable>> cache;
    const std::lock_guard<std::mutex> lock(cacheMutex);
    std::shared_ptr<const EaseTable>& table = cache[{start, end}];
    if (!table) table = std::make_shared<const EaseTable>(start, end);
    return table;
}

MoverItemEase BuildItemEase(const rs::legacy_pi::CustomizerTiming& t) {
    MoverItemEase e;
    e.forward = BuildEaseTable(t.easeStart, t.easeEnd);
    if (t.RevEaseStart() == t.easeStart && t.RevEaseEnd() == t.easeEnd) e.reverse = e.forward;
    else e.reverse = BuildEaseTable(t.RevEaseStart(), t.RevEaseEnd());
    return e;
}

// 発動条件(TriggerInfo)の全フィールドを並べた文字列。同じ条件のグループを表で引くためのキー
// (TriggerInfo::operator== が比べるフィールドを全部含める。区切りは値に現れない制御文字)。
std::string TriggerGroupKey(const rs::legacy_pi::TriggerInfo& trig) {
    std::string key;
    for (const rs::legacy_pi::TriggerStep& step : trig.chain) {
        key += static_cast<char>('0' + static_cast<int>(step.trigger));
        key += step.switchIsExpression ? 'E' : 'e';
        key += step.isDefaultCase ? 'D' : 'd';
        key += step.negateCondition ? 'N' : 'n';
        key += '\x1f';
        key += step.switchName;
        key += '\x1f';
        for (const int value : step.caseValues) key += std::to_string(value) + ',';
        key += '\x1f';
        for (const int value : step.declaredCaseValues) key += std::to_string(value) + ',';
        key += '\x1f';
        key += step.condition;
        key += '\x1e';
    }
    return key;
}

} // namespace

float PartMoverRuntime::EasedPhase(size_t groupIdx, size_t itemIdx, float phase) const {
    const MoverItemEase& e = itemEases_[groupIdx][itemIdx];
    const bool forward = itemStates_[groupIdx][itemIdx].target >= 0.5f;
    const std::shared_ptr<const EaseTable>& table = forward ? e.forward : e.reverse;
    return table ? table->Eval(phase) : phase;
}

bool PartMoverRuntime::ItemSettled(const rs::legacy_pi::StaticCustomizer& item,
                                    const MoverItemState& state, bool groupActive) {
    if (item.kind == StaticCustomizer::Kind::AnalogClock) return false; // 時刻は常に進むため常に未静止扱い(安全側)
    if (item.kind == StaticCustomizer::Kind::TrackingMove && state.phase != 0.0f) return false;
    if (state.delayRemaining > 0.0f) return false;
    if (item.kind == StaticCustomizer::Kind::DynamicRotate) {
        if (state.curSpeedDegPerSec != 0.0f || groupActive) return false;
        return true;
    }
    return state.phase == state.target;
}

void PartMoverRuntime::Build(const rs::legacy_pi::ModelPart& part) {
    groups_.clear();
    trackingMoves_.clear();
    active_.clear();
    itemStates_.clear();
    itemEases_.clear();
    pendingItems_.clear();
    localDirty_ = true;
    cachedGeneration_ = ~uint64_t(0); // 再Build後は必ず再評価させる
    // 下の早期returnで抜ける場合(グループ0件)もここを通る。前回Build時の
    // false が残ると、0グループの runtime は Update() を呼ばれないまま
    // Settled() が永久に false を返し、呼び出し側のスキップが働かなくなる。
    settled_ = true;

    // RailSim II 互換: 可動部(StaticMove/StaticRotation/DynamicRotation)の姿勢適用は
    // 自由配置オブジェクト(Object3D)ブロックでのみ行われる。ジョイント連鎖で
    // 幾何的に位置が決まるパーツ(台車/車体/車軸など)では、ブロック内に可動部が
    // 書かれていても構文としては読まれるだけで姿勢には反映されない。
    // これを再現しないと、作者が使っていない書き残しの変位がそのまま効いて
    // パーツが宙に浮く。メッシュ切替/テクスチャ・材質変更はここでは止めない。
    // ("Object"/"Model" は RailSim II には無いブロック名だが、本ローダが自由配置オブジェクトの
    //  別名として受理しているため同じ扱いにする)
    if (part.kind != "Object3D" && part.kind != "Object" && part.kind != "Model") return;
    trackingMoves_ = part.trackingMoves;

    // 既存グループに同一トリガ(chain構造が完全一致)があれば追加、無ければ新規グループ。
    // カスタマイザが数万個あるプラグイン(駅のドアなど)で、グループを先頭から探すと二乗の時間がかかるため、
    // トリガのキーで表を引く。
    std::unordered_map<std::string, size_t> groupByTrigger;
    groupByTrigger.reserve(part.customizers.size());
    for (const auto& c : part.customizers) {
        const std::pair<std::unordered_map<std::string, size_t>::iterator, bool> inserted =
            groupByTrigger.emplace(TriggerGroupKey(c.trig), groups_.size());
        if (inserted.second) {
            MoverGroup g;
            g.trig = c.trig;
            groups_.push_back(std::move(g));
        }
        groups_[inserted.first->second].items.push_back(c);
    }

    active_.assign(groups_.size(), false);
    itemStates_.assign(groups_.size(), {});
    itemEases_.assign(groups_.size(), {});
    for (size_t i = 0; i < groups_.size(); ++i) {
        itemStates_[i].assign(groups_[i].items.size(), MoverItemState{});
        itemEases_[i].reserve(groups_[i].items.size());
        for (size_t k = 0; k < groups_[i].items.size(); ++k) {
            const StaticCustomizer& item = groups_[i].items[k];
            itemEases_[i].push_back(BuildItemEase(item.timing));
            itemStates_[i][k].usesPhase = item.kind == StaticCustomizer::Kind::Move ||
                                          item.kind == StaticCustomizer::Kind::Rotate ||
                                          item.kind == StaticCustomizer::Kind::TrackingMove;
        }
        if (groups_[i].trig.chain.empty()) {
            // Always(空chain)トリガは Build 時点から適用済み扱い（即時 active, phase=1）。
            active_[i] = true;
            for (auto& st : itemStates_[i]) {
                st.phase = 1.0f;
                st.target = 1.0f;
                st.delayRemaining = 0.0f;
            }
        }
    }

    // 一度きりの全走査で静止していない item の一覧とキャッシュ初期値を求める
    // (GroupCount()==0 の場合もループが回らず settled_ は既定の true のまま。呼び出し側は
    // GroupCount()==0 の runtime に対して Update() を呼ばずスキップするため、
    // ここで false のままにすると Settled() が永久に false を返し続け、
    // スキップ判定そのものが機能しなくなる)。
    for (size_t g = 0; g < groups_.size(); ++g) {
        const std::vector<rs::legacy_pi::StaticCustomizer>& items = groups_[g].items;
        const std::vector<MoverItemState>& states = itemStates_[g];
        const bool groupActive = active_[g];
        for (size_t i = 0; i < items.size(); ++i) {
            if (!ItemSettled(items[i], states[i], groupActive)) MarkItemPending(g, i);
        }
    }
    settled_ = pendingItems_.empty();
}

void PartMoverRuntime::MarkItemPending(size_t groupIdx, size_t itemIdx) {
    MoverItemState& state = itemStates_[groupIdx][itemIdx];
    if (state.pending) return;
    state.pending = true;
    pendingItems_.push_back({static_cast<uint32_t>(groupIdx), static_cast<uint32_t>(itemIdx)});
}

bool PartMoverRuntime::HasTimeDrivenItems() const {
    for (const MoverGroup& group : groups_) {
        for (const StaticCustomizer& item : group.items) {
            if (item.kind == StaticCustomizer::Kind::DynamicRotate ||
                item.kind == StaticCustomizer::Kind::AnalogClock ||
                item.kind == StaticCustomizer::Kind::TrackingMove)
                return true;
        }
    }
    return false;
}

void PartMoverRuntime::SetGroupActive(size_t groupIdx, bool active) {
    if (groupIdx >= groups_.size()) return;
    active_[groupIdx] = active;
    const std::vector<StaticCustomizer>& items = groups_[groupIdx].items;
    std::vector<MoverItemState>& states = itemStates_[groupIdx];
    for (size_t i = 0; i < items.size(); ++i) {
        MoverItemState& st = states[i];
        MarkItemPending(groupIdx, i);
        if (items[i].kind == StaticCustomizer::Kind::DynamicRotate) {
            // DynamicRotate は angleDeg/curSpeedDegPerSec を保持したまま
            // active_ の切替だけが反映される（Update側で目標速度が変わる）。
            continue;
        }
        st.target = active ? 1.0f : 0.0f;
        // 発動した瞬間の遅延（進行方向によって Pre/PreRev を使い分ける）。
        st.delayRemaining = active ? items[i].timing.preDelay : items[i].timing.PreRev();
    }
    // 保守的に false へ倒す。直後に呼ばれる Update() が正しい値へ直す。
    settled_ = false;
    localDirty_ = true;
}

void PartMoverRuntime::UpdateActivation(const rs::legacy_pi::SwitchResolver& resolve,
                                        uint64_t generation) {
    // スイッチ値が前回から変わっていなければ(generation不変)、active_[]は
    // 前回判定のままで正しいため判定自体を丸ごとスキップする。
    if (generation == cachedGeneration_) return;
    cachedGeneration_ = generation;

    // active 判定は共有関数 IsTriggerActive に一本化（chain の全ステップを
    // AND評価。SwitchCase の Default は TriggerStep::declaredCaseValues
    // 単体で完結するため、他グループを横断スキャンする2段階判定は不要）。
    for (size_t i = 0; i < groups_.size(); ++i) {
        bool active = rs::legacy_pi::IsTriggerActive(groups_[i].trig, resolve);
        // エッジトリガ: 実際に状態が変化したときのみ SetGroupActive を呼ぶ。
        // 毎フレーム呼ぶと delayRemaining が preDelay に戻り続けて遅延が
        // 永久に消化されず（phase が進まず）可動部が動かなくなるため。
        if (active != active_[i]) SetGroupActive(i, active);
    }
}

void PartMoverRuntime::UpdateItem(const rs::legacy_pi::StaticCustomizer& c, MoverItemState& st,
                                  bool groupActive, float dt, float dynamicSpeedScale,
                                  double clockSecondsOfDay) {
    if (c.kind == StaticCustomizer::Kind::AnalogClock) {
        if (groupActive) {
            const double period = c.clockHandType == 0 ? 43200.0
                                 : c.clockHandType == 1 ? 3600.0
                                                         : 60.0;
            const double frac = std::fmod(clockSecondsOfDay, period) / period;
            st.angleDeg = static_cast<float>(frac * 360.0);
        }
        return;
    }
    if (c.kind == StaticCustomizer::Kind::DynamicRotate) {
        // 目標角速度: グループが active なら RotationSpeed(deg/s)×倍率、
        // 非activeなら0。accel/decel(deg/s^2)=0 は「即時遷移」を表す。
        float target = groupActive ? c.rotationSpeedDegPerSec * dynamicSpeedScale : 0.0f;
        float cur = st.curSpeedDegPerSec;
        bool accelerating = std::fabs(target) > std::fabs(cur);
        float rate = accelerating ? c.accelDegPerSec2 : c.decelDegPerSec2;
        if (rate <= 0.0f) {
            cur = target;
        } else {
            float maxDelta = rate * dt;
            float diff = target - cur;
            if (std::fabs(diff) <= maxDelta) cur = target;
            else cur += (diff > 0 ? maxDelta : -maxDelta);
        }
        st.curSpeedDegPerSec = cur;
        st.angleDeg += cur * dt;
        st.angleDeg = std::fmod(st.angleDeg, 360.0f);
        return;
    }
    float remain = dt;
    if (st.delayRemaining > 0.0f) {
        float consume = std::min(st.delayRemaining, remain);
        st.delayRemaining -= consume;
        remain -= consume;
        if (remain <= 0.0f) return;
    }
    if (st.phase == st.target) return;
    // 進行方向で AnimationTime/ReverseTime を使い分ける（近似:
    // 順方向=AnimationTime、逆方向=ReverseTime。0 なら即座に適用）。
    float animTime = (st.target > st.phase) ? c.timing.animTime : c.timing.RevTime();
    if (animTime <= 0.0f) {
        st.phase = st.target;
        return;
    }
    float delta = remain / animTime;
    if (st.target > st.phase) {
        st.phase = std::min(st.target, st.phase + delta);
    } else {
        st.phase = std::max(st.target, st.phase - delta);
    }
}

void PartMoverRuntime::Update(float dt, float dynamicSpeedScale, double clockSecondsOfDay) {
    if (pendingItems_.empty()) {
        settled_ = true;
        return;
    }
    // Settled() キャッシュの再計算をこのループへ畳み込む(一覧が空になれば静止)。
    // 判定は必ずこの1箇所だけで行う(UpdateItem 側の早期returnごとに判定を書くと、1つ
    // 書き忘れただけでキャッシュが古い true のまま固まる)。
    size_t keptCount = 0;
    for (const PendingItem& pending : pendingItems_) {
        const StaticCustomizer& item = groups_[pending.group].items[pending.item];
        MoverItemState& state = itemStates_[pending.group][pending.item];
        const bool groupActive = active_[pending.group];
        UpdateItem(item, state, groupActive, dt, dynamicSpeedScale, clockSecondsOfDay);
        if (ItemSettled(item, state, groupActive)) state.pending = false;
        else pendingItems_[keptCount++] = pending;
    }
    pendingItems_.resize(keptCount);
    settled_ = pendingItems_.empty();
    localDirty_ = true;
}

void PartMoverRuntime::ComposeLocal(float out[16], const rs::core::Mat4& base,
                                   const TrackingTargetTable* targets,
                                   const rs::core::Mat4* placementToWorld) {
    if (!localDirty_ && !(HasTrackingMoves() && !Settled())) {
        std::memcpy(out, cachedLocal_.m, sizeof(cachedLocal_.m));
        return;
    }
    rs::core::Mat4 acc = rs::core::Mat4::Identity();
    for (size_t g = 0; g < groups_.size(); ++g) {
        const std::vector<StaticCustomizer>& items = groups_[g].items;
        const std::vector<MoverItemState>& states = itemStates_[g];
        for (size_t i = 0; i < items.size(); ++i) {
            // 位相0の Move/Rotate は単位行列(緩急表も0で0を返す)なので、掛け算を飛ばす。
            if (states[i].usesPhase && states[i].phase == 0.0f) continue;
            const StaticCustomizer& c = items[i];
            float phase = states[i].usesPhase ? EasedPhase(g, i, states[i].phase) : states[i].phase;
            rs::core::Mat4 m;
            if (c.kind == StaticCustomizer::Kind::Move) {
                m = Translation(phase * c.dispX, phase * c.dispY, phase * c.dispZ);
            } else if (c.kind == StaticCustomizer::Kind::TrackingMove) {
                rs::core::DVec3 displacement{c.dispX, c.dispY, c.dispZ};
                if (targets && placementToWorld && c.trackingIndex < trackingMoves_.size()) {
                    const rs::legacy_pi::TrackingMoveCustomizer& move = trackingMoves_[c.trackingIndex];
                    const TrackingTargetTable::const_iterator target = targets->find(move.trackingId);
                    if (target != targets->end()) {
                        displacement = SolveTrackingMove(ComposeTrackingWorld(acc, base, *placementToWorld),
                                                        move, target->second).displacement;
                    }
                }
                m = Translation(static_cast<float>(phase * displacement.x),
                                static_cast<float>(phase * displacement.y),
                                static_cast<float>(phase * displacement.z));
            } else if (c.kind == StaticCustomizer::Kind::DynamicRotate) {
                float ax = c.axisX, ay = c.axisY, az = c.axisZ;
                NormalizeAxisOrDefaultZ(ax, ay, az);
                m = AxisAngleRotation(ax, ay, az, states[i].angleDeg);
            } else if (c.kind == StaticCustomizer::Kind::AnalogClock) {
                // RailSim II の obj->RotZ 相当。RotationAxis指定は無くローカルZ軸固定。
                m = AxisAngleRotation(0.0f, 0.0f, 1.0f, states[i].angleDeg);
            } else {
                float ax = c.axisX, ay = c.axisY, az = c.axisZ;
                NormalizeAxisOrDefaultZ(ax, ay, az);
                m = AxisAngleRotation(ax, ay, az, phase * c.angleDeg);
            }
            // 宣言順の最後の項目ほど自パーツのローカル原点に近い側(=先に適用)。
            // 例: StaticMove→StaticRotation の宣言順では、まずRotationが自パーツ原点まわりに掛かり、その後にMoveが
            // (回転済みの姿勢とは無関係な)固定方向オフセットとして加わる。
            // 逆順(acc=mul(acc,m)、先にMove・後でRotateが全体を原点
            // まわりに振る)ではRailSim IIと符号が逆になる。
            acc = rs::core::Mat4::Mul(m, acc);
        }
    }
    cachedLocal_ = acc;
    localDirty_ = false;
    std::memcpy(out, cachedLocal_.m, sizeof(cachedLocal_.m));
}

} // namespace rs::legacy_plugin
