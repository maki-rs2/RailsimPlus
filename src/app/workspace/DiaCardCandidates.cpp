#include "DiaCardCandidates.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace rs::app::workspace {
namespace {

double DistanceFromCenter(const ClipPoint& clip) { return std::hypot(clip.x / clip.w, clip.y / clip.w); }

bool IsOnScreen(const ClipPoint& clip) {
    return clip.w > kClipFrontMinW && std::abs(clip.x) <= clip.w && std::abs(clip.y) <= clip.w;
}

} // namespace

std::vector<DiaCardCandidate> ChooseVisibleDiaCards(std::vector<DiaCardCandidate> candidates, std::size_t maxCount) {
    std::erase_if(candidates, [](const DiaCardCandidate& candidate) { return !IsOnScreen(candidate.clip); });
    std::stable_sort(candidates.begin(), candidates.end(), [](const DiaCardCandidate& a, const DiaCardCandidate& b) {
        return DistanceFromCenter(a.clip) < DistanceFromCenter(b.clip);
    });
    if (candidates.size() > maxCount) candidates.resize(maxCount);
    return candidates;
}

} // namespace rs::app::workspace
