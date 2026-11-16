#include "CantProfile.h"

#include <algorithm>
#include <cmath>

#include "../../core/Epsilon.h"
#include "Alignment.h"
#include "CantControlPoints.h"

namespace rs::world {

float CantProfile::TransRange::RampAt(float u) const {
    if (mirrored) return 1.0f - TransitionProfileF(shape, 1.0f - u, tau);
    return TransitionProfileF(shape, u, tau);
}

CantProfile::CantProfile(const std::vector<AlignmentSegment>& curve, float totalLen,
                         float cantStart, float cantEnd, float cantMid,
                         float cantTransitionRatio,
                         AlignmentSegment::TransitionShape cantTransitionShape,
                         const std::vector<CantControlPoint>& cantPoints)
    : totalLen_(totalLen),
      cantStart_(cantStart),
      cantEnd_(cantEnd),
      cantMid_(cantMid),
      cantPoints_(cantPoints),
      cantTransitionRatio_(cantTransitionRatio),
      cantTransitionShape_(cantTransitionShape) {
    // curve中のTransition区間を検出する(曲率連動プロファイル用)。
    float sAcc = 0.0f;
    for (const AlignmentSegment& seg : curve) {
        if (seg.kind == AlignmentSegment::Kind::Transition && seg.length > rs::core::kZeroLengthEps) {
            TransRange tr;
            tr.s0 = sAcc;
            tr.s1 = sAcc + seg.length;
            tr.shape = seg.shape;
            tr.tau = 0.5f * seg.length *
                     std::fabs(CurvatureFromRadius(seg.radiusEnd) -
                               CurvatureFromRadius(seg.radius));
            tr.mirrored = std::fabs(CurvatureFromRadius(seg.radius)) >
                          std::fabs(CurvatureFromRadius(seg.radiusEnd));
            if (transCount_ == 0) firstTr_ = tr;
            lastTr_ = tr;
            ++transCount_;
        }
        sAcc += seg.length;
    }

    // カント差×逓減率モデル。
    // curveにTransitionが無い(transCount_==0)場合のみ有効。curveにTransitionがある
    // 場合は既存の曲率連動プロファイル(上のfirstTr_/lastTr_)がそのまま使われ、
    // このフィールドは無視する(緩和曲線の実長と競合させないため、実装方針5番)。
    if (transCount_ == 0 && cantTransitionRatio_ > 0.0f) {
        ratioTransLenA_ = cantTransitionRatio_ * std::fabs(cantMid_ - cantStart_);
        ratioTransLenB_ = cantTransitionRatio_ * std::fabs(cantEnd_ - cantMid_);
        // 実装方針7番: 遷移長がedge全長を超える場合はクランプする(拒否はしない)。
        // 両端の遷移長合計がedge全長を超える場合、比例縮小して重ならないようにする。
        const float sum = ratioTransLenA_ + ratioTransLenB_;
        if (sum > totalLen_ && sum > rs::core::kZeroLengthEps) {
            const float scale = totalLen_ / sum;
            ratioTransLenA_ *= scale;
            ratioTransLenB_ *= scale;
        }
    }
}

float CantProfile::CantAt(float s) const {
    if (totalLen_ <= rs::core::kZeroLengthEps) return cantStart_;
    if (!cantPoints_.empty()) return CantAtControlPoints(cantPoints_, s, cantStart_, cantEnd_, totalLen_);
    if (transCount_ == 0) return CantAtNoTransition(s);
    if (transCount_ == 1) return CantAtSingleTransition(s);
    return CantAtDoubleTransition(s);
}

float CantProfile::CantAtNoTransition(float s) const {
    if (cantTransitionRatio_ <= 0.0f) {
        // 全長線形補間。
        const float t = std::clamp(s / totalLen_, 0.0f, 1.0f);
        return std::lerp(cantStart_, cantEnd_, t);
    }
    // カント差×逓減率モデル: 始点付近でcantStart_→cantMid_、終点付近でcantMid_→
    // cantEnd_を遷移長分だけ逓減し、それ以外(中間)はcantMid_で一定。
    const float rampEndA = ratioTransLenA_;               // cantStart_→cantMid_ の終端s
    const float rampStartB = totalLen_ - ratioTransLenB_;  // cantMid_→cantEnd_ の開始s
    if (s <= rampEndA) {
        if (ratioTransLenA_ <= rs::core::kZeroLengthEps) return cantMid_;
        const float u = std::clamp(s / ratioTransLenA_, 0.0f, 1.0f);
        const float w = TransitionProfileF(cantTransitionShape_, u, 0.0f);
        return std::lerp(cantStart_, cantMid_, w);
    }
    if (s < rampStartB) return cantMid_;
    if (ratioTransLenB_ <= rs::core::kZeroLengthEps) return cantEnd_;
    const float u = std::clamp((s - rampStartB) / ratioTransLenB_, 0.0f, 1.0f);
    const float w = TransitionProfileF(cantTransitionShape_, u, 0.0f);
    return std::lerp(cantMid_, cantEnd_, w);
}

float CantProfile::CantAtSingleTransition(float s) const {
    // 単独緩和: その区間で cantStart_→cantEnd_。
    if (s <= firstTr_.s0) return cantStart_;
    if (s >= firstTr_.s1) return cantEnd_;
    const float w = firstTr_.RampAt((s - firstTr_.s0) / (firstTr_.s1 - firstTr_.s0));
    return std::lerp(cantStart_, cantEnd_, w);
}

float CantProfile::CantAtDoubleTransition(float s) const {
    // 最初のTransitionより前=cantStart_ / 最初のTransition内=cantStart_→cantMid_を
    // f(u)逓減 / 中間=cantMid_ / 最後のTransition内=cantMid_→cantEnd_をf(u)逓減 /
    // それ以降=cantEnd_。
    if (s <= firstTr_.s0) return cantStart_;
    if (s < firstTr_.s1) {
        const float w = firstTr_.RampAt((s - firstTr_.s0) / (firstTr_.s1 - firstTr_.s0));
        return std::lerp(cantStart_, cantMid_, w);
    }
    if (s <= lastTr_.s0) return cantMid_;
    if (s < lastTr_.s1) {
        const float w = lastTr_.RampAt((s - lastTr_.s0) / (lastTr_.s1 - lastTr_.s0));
        return std::lerp(cantMid_, cantEnd_, w);
    }
    return cantEnd_;
}

}  // namespace rs::world
