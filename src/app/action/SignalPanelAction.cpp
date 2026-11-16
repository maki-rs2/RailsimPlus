#include "SignalPanelAction.h"

#include "SignalBlockAction.h"
#include "ToastQueue.h"

namespace rs::app::signal_panel_action {
void SetDirection(SignalBlockAction& action, ToastQueue& queue, const rs::world::Guid& signalGuid,
                  const std::string& direction, const rs::world::WarpLinkTable* warpLinks, std::size_t sceneIndex) {
    const rs::world::SpeedSignalPlacementError error =
        action.SetSpeedSignalDirection(signalGuid, direction == "reverse" ? -1 : +1, warpLinks, sceneIndex);
    if (error != rs::world::SpeedSignalPlacementError::None) {
        queue.Post(ToastSeverity::Warning, std::string("向きを変えられません: ") + SpeedSignalPlacementText(error));
    }
}
void SetForceRed(SignalBlockAction& action, const rs::world::Guid& signalGuid, bool forced) {
    action.SetSpeedSignalForceRed(signalGuid, forced);
}
void AddStage(SignalBlockAction& action, const rs::world::Guid& signalGuid, int speedKmh) {
    action.AddSpeedSignalStage(signalGuid, speedKmh);
}
void RemoveStage(SignalBlockAction& action, const rs::world::Guid& signalGuid, int speedKmh) {
    action.RemoveSpeedSignalStage(signalGuid, speedKmh);
}
void DeleteSignal(SignalBlockAction& action, const rs::world::Guid& signalGuid) {
    action.RemoveSpeedSignal(signalGuid);
}
} // namespace rs::app::signal_panel_action
