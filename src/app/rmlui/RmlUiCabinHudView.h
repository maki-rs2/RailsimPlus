//
// RmlUiCabinHudView.h - 運転台視点中の右上パネル(速度・運行情報)と、左上の戻るボタンの値・イベントをデータモデル"workspace"へ結ぶ
//
// 値の読み書きは編成の操作(FormationAction)を通す。RmlUiWorkspaceViewがメンバとして持ち、
// CreateDataModelの中でRegisterModelを呼んで同じデータモデルへ変数とイベントを足す(別のdata-modelは作らない)。
// 運転台視点中に左右タブなどを消す表示の切り替えは、WorkspaceShell(SetCabinView)側が担当する。
//
#pragma once

#include <memory>

#include <RmlUi/Core/DataModelHandle.h>

namespace rs::app {
struct FormationRefs;
} // namespace rs::app

namespace rs::app::rmlui {

struct CabinHudModel;

class RmlUiCabinHudView {
public:
    RmlUiCabinHudView();
    ~RmlUiCabinHudView();
    RmlUiCabinHudView(const RmlUiCabinHudView&) = delete;
    RmlUiCabinHudView& operator=(const RmlUiCabinHudView&) = delete;

    // 編成の操作の対象を渡す(呼び出し側が所有し、このビューより長く生きること)。
    void Attach(const FormationRefs& refs);

    // データモデルの構築中(RmlUiWorkspaceView::CreateDataModel)に呼び、変数とイベントを登録する。
    void RegisterModel(Rml::DataModelConstructor& constructor);

    // 毎フレーム呼ぶ。前回と比べて変わったときだけ変数をDirtyにする。Attach前は何もしない。
    void Refresh();

private:
    std::unique_ptr<FormationRefs> refs_;
    Rml::DataModelHandle modelHandle_;
    std::unique_ptr<CabinHudModel> model_;
};

} // namespace rs::app::rmlui
