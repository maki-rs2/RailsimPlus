//
// RmlUiLoadingView.h - レイアウト文書の状態の表示(読込中に画面全体を覆う幕と、中央の文言・進捗バー・件数、
// 保存していない変更の印)をデータモデル"workspace"へ結ぶ。
//
// 値を入れるだけで、いつ出すか・何を出すかはLayoutDocumentControllerが決める。DOMには触れない。
//
#pragma once

#include <memory>
#include <string>

#include <RmlUi/Core/DataModelHandle.h>

namespace rs::app::rmlui {

struct LoadingModel;

class RmlUiLoadingView {
public:
    RmlUiLoadingView();
    ~RmlUiLoadingView();
    RmlUiLoadingView(const RmlUiLoadingView&) = delete;
    RmlUiLoadingView& operator=(const RmlUiLoadingView&) = delete;

    // データモデルの構築中(RmlUiWorkspaceView::CreateDataModel)に呼ぶ。
    void RegisterModel(Rml::DataModelConstructor& constructor);

    // 表示の値を入れる。変わった変数だけ反映される。fractionは0〜1、countは進捗バーの下の文字(空なら出さない)。
    void Set(bool visible, const std::string& label, float fraction, const std::string& count);

private:
    Rml::DataModelHandle modelHandle_;
    std::unique_ptr<LoadingModel> model_;
};

} // namespace rs::app::rmlui
