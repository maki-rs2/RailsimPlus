//
// StaticHeadlightRoster.h - 静的バッチで描いている個体のうち、灯具を持つものの一覧
//
// 静的バッチのインスタンスを積み直すとき(ViewportSceneryInstances)に、灯具を持つ個体だけを個体IDで控える。
// 毎フレームの収集(HeadlightCollector)は、この一覧の個体だけ配置を解決する(灯具を持たない大多数の
// 個体には触らない)。灯具の内容は静的判定の結果(StaticMeshVariantSet::headlightsOfVariant)と共有し、
// 実行時に共有状態と視点からどの組の灯具を出すかを選ぶ(Entry::Select)。
//
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../../world/alignment/Guid.h"
#include "../../world/time/SceneSharedSwitches.h"
#include "StaticHeadlight.h"
#include "StaticMeshVariantSet.h"

namespace rs::app {

class StaticHeadlightRoster {
public:
    struct Entry {
        // 信号のモデル(SignalModelSet の個体)か。偽なら SceneRegistry の個体。個体の引き先が違う。
        bool signalModel = false;
        // 焼き込んだ集合から写した、組の選択に要る情報(キャッシュを切り詰めても残るように持つ)。
        std::vector<int32_t> bandStarts;
        uint32_t sharedStateMask = 0;
        std::vector<std::shared_ptr<const std::vector<StaticHeadlight>>> headlightsOfVariant;

        // 共有状態と、個体と視点の距離から決まる組の灯具。点灯する灯具が無ければ nullptr。
        // hasView が偽(視点が未設定)のときは、距離の区分を参照する個体でも区分0として扱う。
        const std::vector<StaticHeadlight>* Select(const rs::world::SceneSharedSwitchValues& shared,
                                                   bool hasView, const float eye[3], float fovYRadians,
                                                   const float position[3]) const;
    };

    void Clear() { entries_.clear(); }
    using EntryMap = std::unordered_map<rs::world::Guid, Entry, rs::world::GuidHash>;

    void Set(const rs::world::Guid& entityId, bool signalModel, const StaticMeshVariantSet& variants);
    void Erase(const rs::world::Guid& entityId) { entries_.erase(entityId); }
    const EntryMap& Entries() const { return entries_; }

private:
    EntryMap entries_;
};

} // namespace rs::app
