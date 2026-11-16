//
// KeyRepeatStepper.cpp
//
#include "KeyRepeatStepper.h"

#include <cmath>

#include "PlacementHeightTuning.h"

namespace rs::app {

void KeyRepeatStepper::Reset() {
    direction_ = 0;
    heldSeconds_ = 0.0;
    repeatStepsDone_ = 0;
}

int KeyRepeatStepper::Update(int direction, double dtSeconds) {
    const PlacementHeightTuning& tuning = PlacementHeightTuning::Embedded();
    return Update(direction, dtSeconds, {tuning.repeatDelaySeconds, tuning.repeatStepsPerSecond});
}

int KeyRepeatStepper::Update(int direction, double dtSeconds, const KeyRepeatTiming& timing) {
    if (direction == 0) {
        Reset();
        return 0;
    }
    int steps = 0;
    if (direction != direction_) {
        // 押した瞬間(向きを変えた瞬間を含む)に1歩。
        Reset();
        direction_ = direction;
        steps = 1;
    } else {
        heldSeconds_ += dtSeconds;
        if (heldSeconds_ > timing.delaySeconds) {
            const long long due = static_cast<long long>(
                std::floor((heldSeconds_ - timing.delaySeconds) * timing.stepsPerSecond));
            steps = static_cast<int>(due - repeatStepsDone_);
            repeatStepsDone_ = due;
        }
    }
    return direction * steps;
}

} // namespace rs::app
