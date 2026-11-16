#include "CameraPhotoPreview.h"

#include <cmath>
#include <array>
#include <utility>

namespace rs::app::workspace {
void CameraPhotoPreview::Start(std::string source, CameraPhotoTiming timing) {
    source_ = std::move(source);
    timing_ = timing;
    elapsed_ = 0.0;
    phase_ = CameraPhotoPhase::Started;
}

void CameraPhotoPreview::Clear() {
    source_.clear();
    phase_ = CameraPhotoPhase::Hidden;
    elapsed_ = 0.0;
}

void CameraPhotoPreview::Advance(double deltaSeconds) {
    if (phase_ == CameraPhotoPhase::Hidden || !std::isfinite(deltaSeconds) || deltaSeconds < 0.0) return;
    // 最初の表示を1フレーム挟んでから、RCSSの遷移を始める。
    if (phase_ == CameraPhotoPhase::Started) {
        phase_ = CameraPhotoPhase::Flash;
        return;
    }
    elapsed_ += deltaSeconds;
    struct Transition {
        CameraPhotoPhase phase;
        double CameraPhotoTiming::* duration;
        CameraPhotoPhase next;
    };
    static constexpr std::array kTransitions{
        Transition{CameraPhotoPhase::Flash, &CameraPhotoTiming::flash, CameraPhotoPhase::Flying},
        Transition{CameraPhotoPhase::Flying, &CameraPhotoTiming::flight, CameraPhotoPhase::Holding},
        Transition{CameraPhotoPhase::Holding, &CameraPhotoTiming::hold, CameraPhotoPhase::Fading},
        Transition{CameraPhotoPhase::Fading, &CameraPhotoTiming::fade, CameraPhotoPhase::Hidden}};
    // 遷移先も必ず描画する。長いフレームで飛行やフェードの開始を飛ばさない。
    for (const Transition& transition : kTransitions) {
        if (phase_ != transition.phase || elapsed_ < timing_.*transition.duration) continue;
        elapsed_ = 0.0;
        phase_ = transition.next;
        if (phase_ == CameraPhotoPhase::Hidden) Clear();
        return;
    }
}
}
