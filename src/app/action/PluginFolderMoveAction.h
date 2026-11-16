//
// PluginFolderMoveAction.h - プラグインブラウザで、カード(プラグイン・フォルダ)を別のフォルダへ移す操作
//
// ブラウザのカードをフォルダのカード・パンくず(親のフォルダ)へドラッグして落としたときの入口。
// ディスク上のフォルダはまだ動かさず、実装されていない操作の通知を出す。
//
#pragma once

#include <string_view>

namespace rs::app {
class ToastQueue;
namespace plugin_folder_move_action {
// source(移すカードの名前)を、target(移し先のフォルダの名前)へ移す。
void MoveToFolder(ToastQueue& queue, std::string_view source, std::string_view target);
} // namespace plugin_folder_move_action
} // namespace rs::app
