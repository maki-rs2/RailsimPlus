//
// RmlUiRenderSettingsView.h - ゲーム設定ウィンドウの描画タブを、データモデル"workspace"へ結ぶ。
//
// RmlUiGameSettingsViewと同じ形で、RmlUiWorkspaceViewがメンバとして持ち、RegisterModelで変数と
// イベントを足す。行はRenderSettingsRows.hの表から作り(値の組み立ては純粋関数)、値の書き込みは
// RenderSettingsActionを通す(ImGui版と同じ呼び出し)。RML側は行の配列renderSettingRowsを
// data-forで種類(toggle・choice・slider)ごとの部品に展開するだけで、項目を足してもRMLは変わらない。
// 描画距離は対数スライダーで、位置(0〜kRenderSliderSteps)と距離の変換は純粋関数。この項目は保存せず
// 反映だけ行う(RenderSettingsActionの組はSetRenderDistanceのみ)ので、離したときの保存は無い。
// DOMには触れない(チェックボックスの状態を読むため、イベントの対象の要素だけ見る)。
//
#pragma once

#include <memory>
#include <optional>

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Event.h>

#include "../action/RenderSettingsAction.h"
#include "../action/RenderSettingsRows.h"
#include "../workspace/RenderSettingsPresentation.h"

namespace rs::app { class ToastQueue; }
namespace rs::app::rmlui {

struct RenderSettingsModel;

class RmlUiRenderSettingsView {
public:
    explicit RmlUiRenderSettingsView(ToastQueue& queue);
    ~RmlUiRenderSettingsView();
    RmlUiRenderSettingsView(const RmlUiRenderSettingsView&) = delete;
    RmlUiRenderSettingsView& operator=(const RmlUiRenderSettingsView&) = delete;

    // 操作の対象を渡す。どれも呼び出し側が所有し、このビューより長く生きること。
    void Attach(const RenderSettingsRefs& refs, const Localization& localization);

    // データモデルの構築中(RmlUiWorkspaceView::CreateDataModel)に呼ぶ。
    void RegisterModel(Rml::DataModelConstructor& constructor);

    // ウィンドウが見えているフレームだけ呼ぶ。Attach前は何もしない。
    void Tick();

private:
    void OnToggle(Rml::Event& event, const Rml::VariantList& arguments);
    void OnChoice(Rml::Event& event, const Rml::VariantList& arguments);
    void OnSlider(Rml::Event& event, const Rml::VariantList& arguments);

    std::optional<RenderSettingsRefs> refs_;
    const Localization* localization_ = nullptr;
    Rml::DataModelHandle modelHandle_;
    std::unique_ptr<RenderSettingsModel> model_;
    ToastQueue& queue_;
    workspace::RenderSettingsPresentation presentation_;
    int shownLanguage_ = -1;
};

} // namespace rs::app::rmlui
