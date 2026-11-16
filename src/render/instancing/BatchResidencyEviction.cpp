#include "BatchResidencyEviction.h"

#include <algorithm>

namespace rs::render {

std::vector<std::string> BatchResidencyEviction::SelectEvictions(
    const std::unordered_map<std::string, uint64_t>& lastLiveTickByKey,
    const std::unordered_set<std::string>& liveKeys, std::size_t maxResidentBatches) {
    std::vector<std::string> evictionCandidates;
    evictionCandidates.reserve(lastLiveTickByKey.size());
    for (const std::pair<const std::string, uint64_t>& entry : lastLiveTickByKey) {
        if (liveKeys.find(entry.first) == liveKeys.end()) evictionCandidates.push_back(entry.first);
    }
    // lastLiveTickが古い順(値が小さい順)に並べる。同値の場合はキー名で安定させる。
    std::sort(evictionCandidates.begin(), evictionCandidates.end(),
              [&lastLiveTickByKey](const std::string& lhs, const std::string& rhs) {
                  const uint64_t lhsTick = lastLiveTickByKey.at(lhs);
                  const uint64_t rhsTick = lastLiveTickByKey.at(rhs);
                  if (lhsTick != rhsTick) return lhsTick < rhsTick;
                  return lhs < rhs;
              });
    const std::size_t totalBatchCount = lastLiveTickByKey.size();
    if (totalBatchCount <= maxResidentBatches) return {};
    const std::size_t overflowCount = totalBatchCount - maxResidentBatches;
    const std::size_t evictCount = std::min(overflowCount, evictionCandidates.size());
    evictionCandidates.resize(evictCount);
    return evictionCandidates;
}

} // namespace rs::render
