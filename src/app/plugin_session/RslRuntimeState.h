#pragma once

#include <functional>
#include <map>
#include <optional>

#include <nlohmann/json_fwd.hpp>

#include "../../sim/TrainRuntime.h"
#include "../../sim/DiaRuntime.h"

namespace rs::world {
struct TrackGraph;
}

namespace rs::app {

struct RslRuntimeState {
    std::vector<rs::sim::TrainRuntimeSnapshot> trains;
    rs::sim::DiaRuntimeSnapshot dia;
};

// 列車の先頭の位置と所属 Scene。保存しない値で、同じ列車の車両のうち order が最小のもの(先頭車)から求める。
struct RslTrainHead {
    size_t sceneIndex = 0;
    rs::world::TrackPosition head;
};
// 列車の編成id → 先頭車の位置。値が空なのは、先頭車はあるが読み込めなかった(Scene が決まらない)列車。
using RslTrainHeads = std::map<rs::world::TrainId, std::optional<RslTrainHead>>;

nlohmann::json RslRuntimeStateToJson(const RslRuntimeState& state,
                                     const std::function<const rs::world::TrackGraph*(size_t)>& graphForScene);
// 全体の形が不正なら false。列車ごとの不正はその列車だけ飛ばし、skippedTrains に数える。
bool RslRuntimeStateFromJson(const nlohmann::json& json, const RslTrainHeads& heads,
                             RslRuntimeState& outState, size_t* skippedTrains = nullptr);

}  // namespace rs::app
