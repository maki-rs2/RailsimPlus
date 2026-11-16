// PartWorldHistory.cpp - パーツのワールド行列の履歴(前回との対応と、動いたかの判定)
#include "PartWorldHistory.h"

#include <cstring>
#include <utility>

namespace rs::render {

void PartWorldHistory::Advance(const std::vector<PartXform>& curr, uint64_t continuityKey,
                               uint32_t frameIndex) {
    const bool continuous = valid_ && key_ == continuityKey && curr_.size() == curr.size() &&
                            frameIndex == frame_ + 1;
    continuous_ = continuous;
    // 旧 curr_ がそのまま前回の行列になる(コピーせず入れ替える)。
    prev_.swap(curr_);
    curr_ = curr;
    key_ = continuityKey;
    frame_ = frameIndex;
    valid_ = true;
    if (!continuous) {
        prev_ = curr_;
        moving_ = false;
        return;
    }
    // 法線行列はワールド行列から作るので、ワールド行列だけ比べる。
    moving_ = false;
    for (size_t i = 0; i < curr_.size(); ++i) {
        if (std::memcmp(prev_[i].world, curr_[i].world, sizeof(curr_[i].world)) != 0) {
            moving_ = true;
            break;
        }
    }
}

} // namespace rs::render
