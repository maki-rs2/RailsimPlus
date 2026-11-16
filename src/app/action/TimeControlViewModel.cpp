#include "TimeControlViewModel.h"

namespace rs::app {

bool FillTimeControlFromClock(workspace::TimeControlPresentation& control, const rs::world::GameClock& clock) {
    const bool changed = control.year_ != clock.Year() || control.month_ != clock.Month() ||
                         control.day_ != clock.Day() || control.hour_ != clock.Hour() ||
                         control.minute_ != clock.Minute() || control.second_ != clock.Second() || control.scale_ != clock.TimeScale();
    if (!changed) return false;
    control.year_ = clock.Year();
    control.month_ = clock.Month();
    control.day_ = clock.Day();
    control.hour_ = clock.Hour();
    control.minute_ = clock.Minute();
    control.second_ = clock.Second();
    control.scale_ = clock.TimeScale();
    control.RefreshDerived();
    return true;
}

} // namespace rs::app
