#include "HelpWindowPlaceholder.h"

#include "EnumIndex.h"

namespace rs::app::workspace {

HelpWindowPresentation MakeHelpWindowPlaceholder() {
    // 入力の割り当てとは接続しない、操作表の表示を確かめるための仮の値。
    HelpWindowPresentation presentation;
    const HelpOperationTable kSpectate = {"仮の鑑賞", {
        {"仮の視点移動", "仮の右ドラッグ", "仮の W / A / S / D"},
        {"仮の拡大・縮小", "仮のホイール", "仮の PageUp / PageDown"},
        {"仮の個体設定", "仮の左クリック", "仮の Enter"},
    }};
    presentation.screens_.fill(kSpectate);
    presentation.workModes_.fill(kSpectate);
    presentation.workModes_[EnumIndex(WorkMode::Rail)] = {"仮のレール", {
        {"仮の敷設位置を決定", "仮の左クリック", "仮の Enter"},
        {"仮のレールを選択", "仮の左クリック", "仮の Tab"},
        {"仮の敷設を取り消す", "仮の右クリック", "仮の Esc"},
    }};
    presentation.workModes_[EnumIndex(WorkMode::Train)] = {"仮の列車", {
        {"仮の編成を配置", "仮の左クリック", "仮の Enter"},
        {"仮の車両を移動", "仮のドラッグ", "仮の ← / →"},
        {"仮の編成を選択", "仮の左クリック", "仮の ↑ / ↓"},
    }};
    presentation.workModes_[EnumIndex(WorkMode::Signal)] = {"仮の信号", {
        {"仮の信号機を配置", "仮の左クリック", "仮の Enter"},
        {"仮の閉塞を選択", "仮の左クリック", "仮の Tab"},
        {"仮の信号機を回転", "仮のホイール", "仮の ← / →"},
    }};
    presentation.workModes_[EnumIndex(WorkMode::Dia)] = {"仮のダイヤ", {
        {"仮のルーチンを開く", "仮の左クリック", "仮の Enter"},
        {"仮の行を選択", "仮の左クリック", "仮の ↑ / ↓"},
        {"仮の一覧へ戻る", "仮の戻るボタン", "仮の Esc"},
    }};
    presentation.workModes_[EnumIndex(WorkMode::Scenery)] = {"仮の情景", {
        {"仮の情景を配置", "仮の左クリック", "仮の Enter"},
        {"仮の情景を回転", "仮のホイール", "仮の ← / →"},
        {"仮の配置を取り消す", "仮の右クリック", "仮の Esc"},
    }};
    presentation.screens_[EnumIndex(Screen::Environment)] = {"仮のシーン画面", {
        {"仮のシーンを選択", "仮の左クリック", "仮の ↑ / ↓"},
        {"仮のシーンを切り替え", "仮の矢印ボタン", "仮の Ctrl + ↑ / ↓"},
        {"仮のシーン名を編集", "仮の名前をクリック", "仮の F2"},
    }};
    return presentation;
}

} // namespace rs::app::workspace
