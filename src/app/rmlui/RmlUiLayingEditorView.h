//
// RmlUiLayingEditorView.h - 敷設エディタ(レールモードの移動できるウィンドウ)の敷設タブを
// データモデル"workspace"へ結ぶ。
//
// 値の読み書きはImGui版と同じTrackLayingActionを通す。表示用の文・選択肢・色はImGui版と共用の
// TrackLayingText.h/TrackLayingPreviewColor.h、入力欄ごとの違いはRmlUiLayingFields.hの表にある。
// RmlUiWorkspaceViewがメンバとして持ち、CreateDataModelの中でRegisterModelを呼んで
// 同じデータモデルへ変数とイベントを足す(別のdata-modelは作らない)。
//
// 毎フレームの更新(Refresh)は、前回データモデルへ入れた値と比べて、変わった変数だけ
// DirtyVariableする(DirtyAllVariablesは呼ばない)。
//
#pragma once

#include <memory>
#include <string>

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Event.h>

namespace rs::editor {
class Editor;
struct PlacePreview;
} // namespace rs::editor

namespace rs::app {
struct TrackLayingViewModel;
} // namespace rs::app

namespace rs::app::rmlui {

struct LayingEditorModel;
struct LayingField;

class RmlUiLayingEditorView {
public:
    RmlUiLayingEditorView();
    ~RmlUiLayingEditorView();
    RmlUiLayingEditorView(const RmlUiLayingEditorView&) = delete;
    RmlUiLayingEditorView& operator=(const RmlUiLayingEditorView&) = delete;

    // 編集対象と、直前のフレームの敷設プレビュー(1フレーム遅れで見える)を渡す。
    // どちらも呼び出し側が所有し、このビューより長く生きること。
    void Attach(rs::editor::Editor& editor, const rs::editor::PlacePreview& preview);

    // データモデルの構築中(RmlUiWorkspaceView::CreateDataModel)に呼び、変数とイベントを登録する。
    void RegisterModel(Rml::DataModelConstructor& constructor);

    // 敷設エディタが出ているフレームに毎回呼ぶ。Attach前は何もしない。
    void Refresh();

    // 敷設エディタが出ていないフレームに呼ぶ。入力欄のフォーカス記録を捨てる
    // (ウィンドウが閉じたあとにフォーカス中の欄として残らないように)。
    void ResetInputFocus();

private:
    void RefreshSettings(const TrackLayingViewModel& viewModel);
    void RefreshStatus(const TrackLayingViewModel& viewModel);

    // 入力欄のイベント(RMLのdata-event-*から来る)。引数の先頭が表のキー。
    void OnCommitField(Rml::Event& event, const Rml::VariantList& arguments);
    void OnFocusField(const Rml::VariantList& arguments);
    void OnKeyField(Rml::Event& event, const Rml::VariantList& arguments);

    // 欄の現在の値をActionへ渡し、反映後の値(読めなければ今の値)を入力欄へ書き戻す。
    void ApplyField(const LayingField& field, Rml::Element& element);
    // 文字入力欄の表示を今の値の書式に戻す(入力欄・データモデルの両方)。
    void ResetText(const LayingField& field, Rml::Element& element);

    rs::editor::Editor* editor_ = nullptr;
    const rs::editor::PlacePreview* preview_ = nullptr;
    Rml::DataModelHandle modelHandle_;
    std::unique_ptr<LayingEditorModel> model_;
    // いまフォーカスしている文字入力欄のキー(無ければ空)。Refreshで上書きしない欄を決める。
    std::string focusedField_;
    // Escで取り消して外すblurは、確定しない。
    bool suppressBlurCommit_ = false;
};

} // namespace rs::app::rmlui
