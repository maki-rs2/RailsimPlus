#include "CameraScreenAction.h"
#include <optional>
#include <string>
#include <vector>

#include "NotImplementedNotice.h"
#include "../camera/PhotoCamera.h"
#include "../workspace/CameraScreenPresentation.h"

namespace rs::app::camera_screen_action {
void TakePhoto(workspace::CameraScreenPresentation& screen) { screen.RequestPhoto(); }
bool FocusAtDepth(workspace::CameraScreenPresentation& screen, double depthM) {
    const std::size_t index = static_cast<std::size_t>(workspace::CameraScreenParameter::FocusDistance);
    if (index >= screen.fields_.size()) return false;
    workspace::CameraScreenField& field = screen.fields_[index];
    const int nearest = NearestPhotoStepIndex(workspace::CameraScreenParameter::FocusDistance, field.steps_, depthM);
    return nearest >= 0 && field.SetText(field.steps_[static_cast<std::size_t>(nearest)]);
}
void GoToBookmark(ToastQueue& queue, int) { NotifyNotImplemented(queue, "ブックマークへの移動"); }
void AddBookmark(ToastQueue& queue) { NotifyNotImplemented(queue, "カメラブックマークの追加"); }
void RenameBookmark(ToastQueue& queue, int, std::string_view) { NotifyNotImplemented(queue, "カメラブックマーク名の変更"); }
void DeleteBookmark(ToastQueue& queue, int) { NotifyNotImplemented(queue, "カメラブックマークの削除"); }
}
