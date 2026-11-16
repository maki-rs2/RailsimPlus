//
// RmlUiBrowserFolder.h - プラグインブラウザのフォルダのカードとパンくず(階層の表示と、そのイベント)
//
// 階層の状態はRmlUi非依存のPluginBrowserState側にあり、ここはデータモデルへの反映と
// イベント(enter_browser_folder / navigate_browser_path)の結線だけを受け持つ。
// カードをフォルダのカード・パンくず・戻るボタンへドラッグして落とすと、そのフォルダへの移動になる
// (browser_drop_folder / browser_drop_path。落とせる場所はドラッグ中に強調する)。
// フォルダのカードはグリッドの項目の先頭にあるので、窓の作る範囲のうちフォルダの項目だけを
// モデルへ出す(プラグインのカードはRmlUiPluginBrowserViewが出す)。
//
#pragma once

#include <functional>
#include <string>

#include <RmlUi/Core/DataModelHandle.h>

#include "../plugin_browser/PluginBrowserState.h"

namespace rs::app {
class ToastQueue;
} // namespace rs::app

namespace rs::app::rmlui {

struct PluginBrowserModel;

class RmlUiBrowserFolder {
public:
    // 階層を持つ状態を渡す。呼び出し側が所有し、このクラスより長く生きること。
    void Attach(plugin_browser::PluginBrowserState& state, ToastQueue& queue) {
        state_ = &state;
        queue_ = &queue;
    }

    // データモデルの構築中に呼び、変数2つとイベント2つを登録する。階層が変わったら
    // onNavigatedを呼ぶ(呼び出し側がグリッドを先頭へ戻す)。
    void Register(Rml::DataModelConstructor& constructor, PluginBrowserModel& model, std::function<void()> onNavigated);

    // 窓の作る範囲buildにあるフォルダのカードと、パンくずをモデルへ反映する。
    void Refresh(Rml::DataModelHandle& handle, PluginBrowserModel& model, const plugin_browser::CardRange& build);

private:
    // ドラッグしているカード(drag_element)をtargetへ移す。同じフォルダへ落としたときは何もしない。
    void DropOn(Rml::Event& event, const std::string& target, int folderIndex);

    plugin_browser::PluginBrowserState* state_ = nullptr;
    ToastQueue* queue_ = nullptr;
};

} // namespace rs::app::rmlui
