//
// RmlUiBrowserSelected.h - プラグインブラウザの「選択中」のプラグイン
//
// 見るフォルダはプロファイル表(BrowserProfileTraits::selectedFolder)で決まる。
// 情景は配置プレビュー中のプラグイン(Editor::sceneryPlacementFolder)、信号は選択中の
// 信号プラグイン(Editor::signalPluginFolder)。レールはセットへ複数入れる形で
// 選択中の1件が決まらないため、選択なしとして扱う。表示名はカタログから引いて覚える。
//
#pragma once

#include <cstdint>
#include <string>

#include "../plugin_browser/BrowserProfileCatalog.h"

namespace rs::editor {
class Editor;
} // namespace rs::editor

namespace rs::app::plugin_browser {
class PluginCatalog;
} // namespace rs::app::plugin_browser

namespace rs::app::rmlui {

class RmlUiBrowserSelected {
public:
    // 毎フレーム呼ぶ。sourceは今のプロファイルが見るフォルダ(プロファイル表の列)。
    void Update(const plugin_browser::PluginCatalog& catalog, const rs::editor::Editor& editor,
                plugin_browser::BrowserSelectedFolder source);

    // 選択中のフォルダ(絶対パス)。選択なしなら空。
    const std::string& Folder() const { return folder_; }
    // 段に出す名前。選択なしなら空。
    const std::string& Name() const { return name_; }

private:
    std::string folder_;
    std::string name_;
    // 名前を引いたときのカタログの版。走査中に増えたら引き直す。
    std::uint64_t resolvedRevision_ = 0;
    bool resolved_ = false;
};

} // namespace rs::app::rmlui
