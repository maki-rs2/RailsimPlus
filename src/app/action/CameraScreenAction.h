#pragma once
#include <string_view>

namespace rs::app {
class ToastQueue;
namespace workspace { struct CameraScreenPresentation; }
namespace camera_screen_action {
void TakePhoto(workspace::CameraScreenPresentation& screen);
// ファインダーでクリックした点の奥行き[m。空は無限大]に、段の値のうち対数で最も近いものをフォーカス距離にする。
// 奥行き[m]に最も近いフォーカスの段を入力欄へ入れる。入れたら true。
bool FocusAtDepth(workspace::CameraScreenPresentation& screen, double depthM);
void GoToBookmark(ToastQueue& queue, int row);
void AddBookmark(ToastQueue& queue);
void RenameBookmark(ToastQueue& queue, int row, std::string_view name);
void DeleteBookmark(ToastQueue& queue, int row);
}
}
