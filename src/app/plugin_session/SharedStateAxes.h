//
// SharedStateAxes.h - シーン内で共有する状態のシステムスイッチ(_NIGHT/_SEASON/_SHADOW/_ENVMAP)の扱いを1か所に集める
//
// これらの軸は全配置で同じ値になり、取りうる値が少数(SystemSwitchAxes.hの表のdomainMin〜
// domainMax)。静的インスタンス化では、参照している軸の値の直積を「状態スロット」とし、
// 状態スロットごとにメッシュを焼いておく。ここでは次の3つを受け持つ。
//   - どの軸を参照しているかのビットマスク(ビット番号は kSharedStateAxes の添字)
//   - 参照している軸だけの混合基数で決める状態スロット番号(値→番号、番号→値)
//   - systemSwitches への書き込み(スロット経路の毎フレーム更新と、焼き込みの両方が使う)
// スイッチ名の文字列はSystemSwitchAxes.hの表から引き、ここ以外に散らさない。
//
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

#include "../../plugins/schema/SystemSwitchAxes.h"
#include "../../world/time/SceneSharedSwitches.h"

namespace rs::app {

struct SharedStateAxisEntry {
    rs::legacy_pi::SystemSwitchId id;
    int32_t rs::world::SceneSharedSwitchValues::* member;
};

// 共有状態の軸の一覧。ビット番号・状態スロットの桁の順はこの並びと同じ。
inline constexpr std::array<SharedStateAxisEntry, 4> kSharedStateAxes = {{
    {rs::legacy_pi::SystemSwitchId::Night, &rs::world::SceneSharedSwitchValues::night},
    {rs::legacy_pi::SystemSwitchId::Season, &rs::world::SceneSharedSwitchValues::season},
    {rs::legacy_pi::SystemSwitchId::Shadow, &rs::world::SceneSharedSwitchValues::shadow},
    {rs::legacy_pi::SystemSwitchId::EnvMap, &rs::world::SceneSharedSwitchValues::envMap},
}};

namespace shared_state_detail {

// 軸の表で StaticBySharedState になっている軸が、kSharedStateAxes と過不足なく一致するか。
constexpr bool AxesMatchTable() {
    size_t verdictCount = 0;
    for (const rs::legacy_pi::SystemSwitchAxis& axis : rs::legacy_pi::kSystemSwitchAxes) {
        if (axis.staticVerdict == rs::legacy_pi::SystemSwitchStaticVerdict::StaticBySharedState)
            ++verdictCount;
    }
    if (verdictCount != kSharedStateAxes.size()) return false;
    for (const SharedStateAxisEntry& entry : kSharedStateAxes) {
        const rs::legacy_pi::SystemSwitchAxis& axis =
            rs::legacy_pi::kSystemSwitchAxes[static_cast<size_t>(entry.id)];
        if (axis.staticVerdict != rs::legacy_pi::SystemSwitchStaticVerdict::StaticBySharedState)
            return false;
        if (axis.domainMax < axis.domainMin) return false;
    }
    return true;
}

} // namespace shared_state_detail

static_assert(shared_state_detail::AxesMatchTable(),
              "kSharedStateAxes が軸の表のStaticBySharedStateと一致していません");

// systemSwitches(プラグインが参照しているシステムスイッチ名→値)に載っている共有状態の軸の
// ビットマスク。
uint32_t SharedStateAxisMask(const std::map<std::string, int32_t>& systemSwitches);

// maskの軸の取りうる値の直積の個数(maskが0なら1)。
size_t SharedStateSlotCount(uint32_t mask);

// 現在値から状態スロット番号を引く(0以上SharedStateSlotCount(mask)未満)。
// maskに無い軸の値は見ない。表の範囲外の値は範囲の端へ寄せる。
size_t SharedStateSlotIndex(uint32_t mask, const rs::world::SceneSharedSwitchValues& values);

// 状態スロット番号から値へ戻す。maskに無い軸は表のdomainMinにする。
rs::world::SceneSharedSwitchValues SharedStateSlotValues(uint32_t mask, size_t slot);

// systemSwitchesに載っている共有状態の軸だけへ値を書く(載っていない軸は触らない)。
void WriteSharedStateSwitches(std::map<std::string, int32_t>& systemSwitches,
                              const rs::world::SceneSharedSwitchValues& values);

} // namespace rs::app
