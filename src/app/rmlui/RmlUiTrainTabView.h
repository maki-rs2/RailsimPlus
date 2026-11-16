//
// RmlUiTrainTabView.h - 列車モードの左タブの階層(列車一覧 > プラグインブラウザ)と、編成テンプレートの分類
//
// 列車一覧は編成のカードをタブ全体に広げた画面で、列車モードに入るたびにここから始める。未配置のカードをダブルクリックするか
// 新規編成を作ると、プラグインブラウザへ移ってその編成を組む(画面下の編成編集パネルが出る)。配置済みのカードをダブルクリックすると、
// その列車の先頭車へカメラを移す。ブラウザの上の「一覧に戻る」で一覧へ戻る。
// ブラウザの種別タブの並びに、保存済みのテンプレートの分類(レール=敷設、情景=駅・施設、列車=編成)を足している。
// 編成・敷設のテンプレートのカードには画像を撮って出し(列車はあわせて両数と、見つからない車両の警告)、情景は名前だけを出す。
// 同じデータモデルへ変数とイベントを足す(別のdata-modelは作らない)。
//
#pragma once

#include <memory>
#include <string>
#include <vector>

#include <RmlUi/Core/DataModelHandle.h>

#include "../plugin_browser/BrowserProfileCatalog.h"
#include "../plugin_browser/TemplateImageStore.h"

namespace rs::editor {
class Editor;
}
namespace rs::app::plugin_browser {
class PluginBrowserService;
}
namespace rs::app {
struct FormationRefs;
class PluginSelector;
class ToastQueue;
} // namespace rs::app

namespace rs::app::rmlui {

struct TrainTabModel;

class RmlUiTrainTabView {
public:
    explicit RmlUiTrainTabView(ToastQueue& queue);
    ~RmlUiTrainTabView();
    RmlUiTrainTabView(const RmlUiTrainTabView&) = delete;
    RmlUiTrainTabView& operator=(const RmlUiTrainTabView&) = delete;

    // 編成の操作の対象を渡す(呼び出し側が所有し、このビューより長く生きること)。
    void Attach(const FormationRefs& refs);
    // レールのテンプレートを読み込む先(敷設プラグインセット)と、プラグインルートの登録順を渡す(同じく呼び出し側が所有する)。
    void AttachLaying(rs::editor::Editor& editor, PluginSelector& selector, const std::vector<std::string>& pluginRoots);
    // テンプレートのカードの画像を撮るワーカーと置き場(PluginBrowserService)を渡す(呼び出し側が所有する)。
    void AttachBrowser(plugin_browser::PluginBrowserService& browser);
    // 毎フレーム呼ぶ。今のブラウザのプロファイルで、テンプレートの分類を出すかを決める。プロファイルが列車へ変わったときは、一覧のページへ戻す。
    // テンプレートの名前は、ルートの先頭の下の保存先から、プロファイルかルートが変わったときと分類を開くときに読む。
    void Refresh(plugin_browser::BrowserProfile profile, const std::vector<std::string>& pluginRoots);
    void RegisterModel(Rml::DataModelConstructor& constructor);

private:
    void SetPage(const char* page);
    void SetTemplatesOpen(bool open);
    void ReloadTemplates();
    // 分類を開いている間、カードの画像を要求し、撮れたものをカードへ出す。
    void UpdateTemplateImages();
    // 撮れている画像を、カードの行へ入れる。変わった行があれば true(再描画の合図は呼び出し側が出す)。
    bool ApplyTemplateImages();

    ToastQueue& queue_;
    std::unique_ptr<FormationRefs> refs_;
    rs::editor::Editor* editor_ = nullptr;
    PluginSelector* selector_ = nullptr;
    const std::vector<std::string>* pluginRoots_ = nullptr;
    Rml::DataModelHandle modelHandle_;
    std::unique_ptr<TrainTabModel> model_;
    plugin_browser::BrowserProfile profile_ = plugin_browser::BrowserProfile::None;
    std::string pluginRoot_; // テンプレートを読むルート(登録順の先頭)
    plugin_browser::PluginBrowserService* browser_ = nullptr;
    // 画像を撮るカードの要求(編成・敷設のテンプレートだけ)と、そのカードの行の番号。一覧を読み直すたびに作り直す。
    std::vector<plugin_browser::WantedTemplateImage> imageRequests_;
    std::vector<std::size_t> imageRequestRows_;
};

} // namespace rs::app::rmlui
