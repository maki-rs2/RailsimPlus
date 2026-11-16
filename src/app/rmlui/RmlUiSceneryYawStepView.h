//
// RmlUiSceneryYawStepView.h - 情景配置の「矢印キーの回転刻み(度)」の入力欄をデータモデル"workspace"へ結ぶ
//
// 値の持ち主はImGui版と同じSceneryPlacementUiState::yawStepDegで、ここは参照だけを持つ(二重に持たない)。
// 範囲の丸めはSceneryYawAction::ClampStepDegで、ImGui版と共用する。入力欄はEnter・フォーカスが離れた
// ときだけ反映し(commit_scenery_yaw_step)、Escは入力を取り消す(key_scenery_yaw_step)。
// 入力欄はプラグインブラウザの情景配置の設定(browser-placement-options)にある。
//
#pragma once

#include <memory>
#include <string>

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Event.h>

namespace Rml {
class Element;
} // namespace Rml

namespace rs::app::rmlui {

struct SceneryYawStepModel;

class RmlUiSceneryYawStepView {
public:
    RmlUiSceneryYawStepView();
    ~RmlUiSceneryYawStepView();
    RmlUiSceneryYawStepView(const RmlUiSceneryYawStepView&) = delete;
    RmlUiSceneryYawStepView& operator=(const RmlUiSceneryYawStepView&) = delete;

    // 刻み角度[度]の持ち主の変数を渡す。呼び出し側が所有し、このビューより長く生きること。
    void Attach(float& stepDeg) { stepDeg_ = &stepDeg; }

    // データモデルの構築中に呼び、変数とイベントを登録する。
    void RegisterModel(Rml::DataModelConstructor& constructor);

    // 入力欄の表示を今の値へ合わせる(フォーカス中は上書きしない)。Attach前は何もしない。
    void Refresh();

private:
    void OnCommit(Rml::Event& event);
    void OnKey(Rml::Event& event);
    // 持ち主の値を表示用の文字にする。
    std::string CurrentText() const;
    void ResetText(Rml::Element& element);

    float* stepDeg_ = nullptr;
    Rml::DataModelHandle modelHandle_;
    std::unique_ptr<SceneryYawStepModel> model_;
    bool focused_ = false;
    bool suppressBlurCommit_ = false;
};

} // namespace rs::app::rmlui
