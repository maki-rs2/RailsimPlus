//
// UiPanelHelpers.h - プラグイン選択系パネル間で共有する小さな描画ヘルパー
// (PluginSelectorPanel.cpp/FormationPanel.cpp共通)
//
#pragma once

#include <string>

#include "ImGuiPathPicker.h"

namespace rs::app {

class PluginSelector;

// フォルダ選択ダイアログで選んだフォルダを追加する行+エラー表示。プラグイン選択系パネル共通の先頭部分。
void DrawAddRow(PluginSelector& selector, ImGuiPathPicker& pathPicker, std::string& lastError,
                const char* addButtonId);

} // namespace rs::app
