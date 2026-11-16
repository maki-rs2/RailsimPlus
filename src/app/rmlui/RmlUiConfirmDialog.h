//
// RmlUiConfirmDialog.h - 汎用の確認ダイアログ(文書に1つだけ置く要素 #confirm-dialog)。
//
// 中身は本文と最大3つのボタンだけ(タイトルなし)。左から[キャンセル][第2の選択][確定]で、
// キャンセルと第2の選択は文言が空なら出さない。C++から Open() で開く。要素の構造は #confirm-dialog の
// 直下に画面全体を覆う幕(.confirm-dialog-scrim)と箱(.confirm-dialog-box)を持ち、箱の中に
// #confirm-dialog-message、#confirm-dialog-cancel、#confirm-dialog-secondary、#confirm-dialog-ok を置く。
//
// 開いている間は幕がクリックを受け止め(閉じはしない)、背後のUIとビューポートへそのまま通さない。
// キーは RmlUiContextMenu と同じ流儀で、Enter=確定、Esc=取消を呼び出し側(RmlUiIntegration)が
// HandleKeyDown へ回して部品が取る。ウィンドウのフォーカスが外れたときも取消として閉じる。
//
#pragma once

#include <functional>
#include <string>

#include <RmlUi/Core/EventListener.h>

#include "RmlUiContextMenu.h"

namespace Rml {
class Element;
class ElementDocument;
class Event;
} // namespace Rml

namespace rs::app::rmlui {

using ConfirmCallback = std::function<void()>;

struct ConfirmDialogContent {
    std::string message; // 改行(\n)はそのまま改行として出す
    std::string confirmLabel;
    bool danger = false; // 確定ボタンを危険な操作の色にする
    ConfirmCallback onConfirm;
    std::string secondaryLabel; // 空なら第2のボタンを出さない
    ConfirmCallback onSecondary;
    std::string cancelLabel = "キャンセル"; // 空ならキャンセルのボタンを出さない
    // キャンセルのボタン・Esc・フォーカスが外れて閉じたときに呼ぶ。後の処理を予約している呼び出し側が、
    // 予約を取り消すために使う。
    ConfirmCallback onCancel;
};

class RmlUiConfirmDialog : public Rml::EventListener {
public:

    // documentに #confirm-dialog が無ければ何もしないダイアログになる(開かない)。
    explicit RmlUiConfirmDialog(Rml::ElementDocument& document);
    ~RmlUiConfirmDialog() override;
    RmlUiConfirmDialog(const RmlUiConfirmDialog&) = delete;
    RmlUiConfirmDialog& operator=(const RmlUiConfirmDialog&) = delete;

    // 開く。開いている間に呼ぶと、前の内容を捨てて開き直す(前のコールバックは呼ばない)。
    void Open(ConfirmDialogContent content);
    // [キャンセル][確定]の2つのボタンで開く。
    void Open(const std::string& message, const std::string& confirmLabel, bool danger, ConfirmCallback onConfirm);

    bool IsOpen() const { return open_; }
    // 確定せずに閉じ、onCancelを呼ぶ。
    void Close();

    // 開いている間のキー。Enter=確定、Esc=取消。開いていればtrue(ほかのキーも含め、呼び出し側は他へ流さない)。
    // isRepeatは押しっぱなしの繰り返し(メニューのEnterで開いた直後に確定してしまうのを避けるため、無視する)。
    bool HandleKeyDown(ContextMenuKey key, bool isRepeat);

    void ProcessEvent(Rml::Event& event) override;

private:
    // 閉じてから、選ばれたボタンの処理を呼ぶ(処理の中でダイアログが開き直されても影響しない)。
    void Finish(ConfirmCallback ConfirmDialogContent::*choice);
    // targetからdialogの根へたどって、elementの中か。
    bool IsInside(Rml::Element* target, const Rml::Element* element) const;

    Rml::ElementDocument& document_;
    Rml::Element* root_ = nullptr; // 文書内に固定。nullなら無効
    Rml::Element* message_ = nullptr;
    Rml::Element* cancelButton_ = nullptr;
    Rml::Element* secondaryButton_ = nullptr;
    Rml::Element* confirmButton_ = nullptr;

    bool open_ = false;
    ConfirmDialogContent content_;
};

} // namespace rs::app::rmlui
