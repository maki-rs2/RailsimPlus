#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "../alignment/Guid.h"
#include "TrainId.h"

namespace rs::world {

struct TrainRunInfo {
    // エンジン定義の固定フィールド。空文字列/NilGuid/空配列は未設定を表す。
    std::string type;
    std::string destination;
    std::string lineName;
    std::string trainNumber;
    std::string dutyNumber;
    // 実走行からエンジンが算出する直近発車駅。NilGuidは未設定を表す。
    Guid lastDepartureStation = NilGuid();
    // 表示専用の停車駅リスト。分岐制御には使用しない。
    std::vector<Guid> stopStations;
};

class TrainRunInfoRegistry {
public:
    void SetRunInfo(TrainId trainId, TrainRunInfo info);
    const TrainRunInfo* RunInfo(TrainId trainId) const;
    void RemoveTrain(TrainId trainId);
    const std::unordered_map<TrainId, TrainRunInfo, GuidHash>& Assignments() const;

private:
    std::unordered_map<TrainId, TrainRunInfo, GuidHash> runInfo_;
};

}  // namespace rs::world
