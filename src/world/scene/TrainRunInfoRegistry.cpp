#include "TrainRunInfoRegistry.h"

#include <utility>

namespace rs::world {

void TrainRunInfoRegistry::SetRunInfo(TrainId trainId, TrainRunInfo info) {
    runInfo_[trainId] = std::move(info);
}

const TrainRunInfo* TrainRunInfoRegistry::RunInfo(TrainId trainId) const {
    const std::unordered_map<TrainId, TrainRunInfo, GuidHash>::const_iterator iterator = runInfo_.find(trainId);
    return iterator == runInfo_.end() ? nullptr : &iterator->second;
}

void TrainRunInfoRegistry::RemoveTrain(TrainId trainId) {
    runInfo_.erase(trainId);
}

const std::unordered_map<TrainId, TrainRunInfo, GuidHash>& TrainRunInfoRegistry::Assignments() const {
    return runInfo_;
}

}  // namespace rs::world
