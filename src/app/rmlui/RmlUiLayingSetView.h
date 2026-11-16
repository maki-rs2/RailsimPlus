// RmlUiLayingSetView.h - 敷設エディタの「敷設プラグインセット」の段(セットの一覧と、セットから外す操作)を
// workspaceモデルへ結ぶ。
//
// 一覧の元はTrackPluginSetAction::ViewModel()。敷設テンプレートと選択区間の切り替えは
// ViewModelが行うので、ここでは分岐しない。ブラウザのカードを段へ落とすと、セットへ追加する。段の見出しの下には、セットの合成プレビュー(PluginBrowserService::LayingPreview)を出す。
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Event.h>

namespace rs::editor { class Editor; }
namespace rs::app::plugin_browser { class PluginBrowserService; }
namespace rs::app {
class PluginSelector;
class ToastQueue;
}

namespace rs::app::rmlui {
struct LayingSetModel;

class RmlUiLayingSetView {
public:
    explicit RmlUiLayingSetView(ToastQueue& queue);
    ~RmlUiLayingSetView();
    RmlUiLayingSetView(const RmlUiLayingSetView&) = delete;
    RmlUiLayingSetView& operator=(const RmlUiLayingSetView&) = delete;

    // 呼び出し側が所有し、このビューより長く生きること。
    void Attach(rs::editor::Editor& editor, PluginSelector& selector, plugin_browser::PluginBrowserService& browser);
    void RegisterModel(Rml::DataModelConstructor& constructor);
    // 敷設エディタが出ている間、毎フレーム呼ぶ。変わった変数だけ反映する。
    void Refresh();

private:
    void OnRemove(const Rml::VariantList& arguments);
    // ブラウザのカード(カタログの添字)をセットへ足す。範囲外・負なら何もしない。
    void OnDrop(int entryId);
    // セットの合成プレビューの要求と、テクスチャ名の更新(フォルダの列か読込結果の世代が変わったときだけ要求する)。
    void RefreshImage(const std::vector<std::string>& folders);

    ToastQueue& queue_;
    rs::editor::Editor* editor_ = nullptr;
    PluginSelector* selector_ = nullptr;
    plugin_browser::PluginBrowserService* browser_ = nullptr;
    // 最後に要求したときのフォルダの列と読込結果の世代。
    std::vector<std::string> requestedFolders_;
    std::uint64_t requestedGeneration_ = 0;
    bool requested_ = false;
    // 名前を差し替えた直後の古いテクスチャ。画面が使い終えるよう、次のフレームで解放する。
    std::string pendingRelease_;
    Rml::DataModelHandle modelHandle_;
    std::unique_ptr<LayingSetModel> model_;
};
}
