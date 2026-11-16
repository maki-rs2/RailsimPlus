#pragma once
#include <string>
#include <string_view>

namespace rs::app {
class LayoutPersistenceAction;
class Localization;
class ToastQueue;
// シーン画面・下部バー・Ctrl+上下から呼ぶ操作の入口。ImGuiのセーブデータ画面と同じ
// LayoutPersistenceActionの関数を通す。読込中など受け付けないときは、その旨をトーストで出す。
namespace scene_action {
void SwitchScene(LayoutPersistenceAction& action, ToastQueue& queue, int row);
void PreviousScene(LayoutPersistenceAction& action, ToastQueue& queue);
void NextScene(LayoutPersistenceAction& action, ToastQueue& queue);
void RenameScene(LayoutPersistenceAction& action, ToastQueue& queue, int row, std::string_view name);
void ReorderScene(LayoutPersistenceAction& action, ToastQueue& queue, int source, int destination);
// 地形のフォルダを原点に置いた空のシーンを末尾へ足し、そのシーンへ切り替える。足したらtrue。
bool AddScene(LayoutPersistenceAction& action, ToastQueue& queue, const std::string& surfaceFolderUtf8);
// 行のシーンを削除する(確認は呼び出し側)。削除したらtrue。1つしか無いときは何もせず、その旨をトーストで出す。
// 削除のトーストには「元に戻す」ボタンが付き、RestoreDeletedSceneと同じ処理を呼ぶ。
bool DeleteScene(LayoutPersistenceAction& action, ToastQueue& queue, const Localization& localization, int row);
// 直近に削除したシーンを戻す(Ctrl+Z・トーストのボタン共通)。戻せなければ、その旨をトーストで出す。
bool RestoreDeletedScene(LayoutPersistenceAction& action, ToastQueue& queue, const Localization& localization);
// シーンの環境を変える(envPluginが空ならデフォルトの空)。
void ApplyEnvironment(LayoutPersistenceAction& action, ToastQueue& queue, int row, const std::string& envPlugin);
}
}
