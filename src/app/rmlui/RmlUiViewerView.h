//
// RmlUiViewerView.h - プラグインビューアー画面(viewer.rml)のデータモデル"viewer"の構築と結線。
//
// 画面は2枚のウィンドウ。1枚目の「プラグインスイッチ」はワークスペースのプラグイン設定パネルと同じ部品
// (RmlUiPluginSettingsView)で、開いているプラグインの個体IDからスイッチの行を作って書き込む。
// 2枚目の「それ以外」はプラグイン情報・ライト・システムスイッチで、値の読み書きは
// PluginInfoAction・ViewerSettingsActionを通す(ImGui版のパネルと同じ)。
//
#pragma once

#include <functional>
#include <memory>

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Event.h>

#include "../../world/alignment/Guid.h"
#include "RmlUiPluginSettingsView.h"

namespace Rml {
class Context;
} // namespace Rml

namespace rs::app {
struct PluginSession;
struct ViewerState;
class SceneRenderer;
} // namespace rs::app

namespace rs::world {
class SceneRegistry;
} // namespace rs::world

namespace rs::editor {
class Editor;
} // namespace rs::editor

namespace rs::app::rmlui {

struct ViewerModel;

class RmlUiViewerView {
public:
    // 開いているプラグインのセッション。読込が終わるまではnullptr(毎回引き直すので、セッションは持たない)。
    using SessionProvider = std::function<PluginSession*()>;

    RmlUiViewerView();
    ~RmlUiViewerView();
    RmlUiViewerView(const RmlUiViewerView&) = delete;
    RmlUiViewerView& operator=(const RmlUiViewerView&) = delete;

    // Rml::Context::LoadDocument()より前に呼ぶこと(data-modelは文書の読込時に解決される)。失敗したらfalse。
    bool CreateDataModel(Rml::Context& context);

    // 引数はどれも呼び出し側が所有し、このビューより長く生きること。
    void Attach(rs::world::SceneRegistry& sceneRegistry, SceneRenderer& sceneRenderer, rs::editor::Editor& editor,
                ViewerState& viewer, SessionProvider sessionProvider);

    // 毎フレーム呼ぶ。pluginEntityIdは開いているプラグインの個体ID。セッションが引けない間は2枚目を隠す。
    void Refresh(const rs::world::Guid& pluginEntityId);

private:
    void OnSetLight(const Rml::VariantList& arguments);
    void OnSetSystemSwitch(Rml::Event& event, const Rml::VariantList& arguments);

    RmlUiPluginSettingsView pluginSettings_;
    std::unique_ptr<ViewerModel> model_;
    Rml::DataModelHandle modelHandle_;
    ViewerState* viewer_ = nullptr;
    SessionProvider sessionProvider_;
};

} // namespace rs::app::rmlui
