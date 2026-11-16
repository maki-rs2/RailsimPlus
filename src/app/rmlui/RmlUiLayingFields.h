//
// RmlUiLayingFields.h - 敷設エディタ(RmlUi版)のデータモデルの値と、入力欄ごとの違いの表
//
// 入力欄(文字入力・トグル・コンボ)ごとに違うのは、キー名・入力の種類・今の値の書式・
// 値を読んでTrackLayingActionへ渡す処理だけ。その違いをkLayingFieldsの表にまとめ、
// RmlUiLayingEditorViewは項目ごとの分岐を持たずに表を引く。文や選択肢の表は
// TrackLayingText.h(ImGui版と共用)にある。
//
#pragma once

#include <span>
#include <string>
#include <string_view>

#include <RmlUi/Core/Types.h>

#include "../action/TrackLayingAction.h"

namespace rs::app::rmlui {

// 文の1行(表示する文と、警告・エラーの色分け用のキー。ToEnumKey(TextSeverity))。
struct LayingLineModel {
    Rml::String text;
    Rml::String severity;
    bool operator==(const LayingLineModel&) const = default;
};

// データモデル"workspace"へ結ぶ、敷設エディタの値。変数名はバインド名と同じ"laying..."で、
// 入力欄の変数名は下の表のキーと同じにする。
struct LayingEditorModel {
    // バナーの元に戻す・やり直すを押せるか(エディタの操作の履歴があるか)。
    bool canUndo = false;
    bool canRedo = false;
    // ---- 設定の組(入力欄の表示用。フォーカス中の文字入力欄は更新で上書きしない) ----
    Rml::String angleSnapDegrees;
    Rml::String trackCount;
    Rml::String trackSpacing;
    Rml::String transitionLength;
    // コンボの選択中の添字(文字列。<select>のvalueと同じ型に揃える)。
    Rml::String transition;
    bool angleSnapEnabled = false;
    bool transitionEnabled = false;
    bool trackSpacingDisabled = false;
    // 最小化中(本体を畳んでタイトルバーだけ見せる)。ウィンドウの開閉とは別。
    bool minimized = false;

    // ---- プレビュー・状態の組(毎フレーム変わりうる文) ----
    Rml::Vector<LayingLineModel> noticeLines;
    Rml::Vector<LayingLineModel> statusLines;

    // ---- 固定(起動時に1回だけ入れる) ----
    Rml::Vector<Rml::String> transitionLabels;
};

enum class LayingFieldKind { Text, Toggle, Combo };

// 入力欄1つ分。イベントの引数(commit_laying_field('キー')など)とバインド名がkey。
struct LayingField {
    const char* key;
    LayingFieldKind kind;
    // Text/Comboのとき、変数(LayingEditorModel内の文字列)。Toggleはnullptr。
    Rml::String LayingEditorModel::* text;
    // Text/Comboのとき、今の値の書式(TrackLayingViewModelから作る)。Toggleはnullptr。
    std::string (*format)(const TrackLayingViewModel&);
    // 入力欄から読んだ文字列(Textは入力文字、Toggleは"1"/"0"、Comboはoptionのvalue)を
    // Actionへ渡す。読めなければ何も変えずにfalseを返す。範囲の丸めはActionの決まりに従う。
    bool (*apply)(TrackLayingAction&, const std::string&);
};

// Toggle/Comboのバインド名。表のキーと、データモデルへの登録・更新で同じ名前を使う。
inline constexpr const char* kLayingAngleSnapEnabledKey = "layingAngleSnapEnabled";

std::span<const LayingField> LayingFields();
// keyの行。無ければnullptr。
const LayingField* FindLayingField(std::string_view key);

} // namespace rs::app::rmlui
