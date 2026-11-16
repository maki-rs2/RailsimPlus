//
// LayoutDocumentFlow.h - 保存・ロード・終了の流れ(未保存の警告、名前の入力、上書きの確認)の状態機械。
//
// RmlUiにもファイルにも依存しない。呼び出し側は要求と画面の答えを渡し、戻り値のStepが指示する処理を行って、
// その結果をまた渡す。「保存してから終了」のように保存のあとへ続く操作はここで覚え、途中でやめたら一緒に取りやめる。
//
#pragma once

#include <string>

namespace rs::app::workspace {

enum class LayoutFlowEffect {
    None,
    ShowUnsavedWarning, // 保存する・保存しない・キャンセルを聞く
    ConfirmQuit,        // 未保存の変更がないときに、終了してよいかを聞く
    OpenSaveAs,         // 名前を入力する画面を開く
    ConfirmOverwrite,   // pathの上書きを聞く
    Save,               // pathへ保存する
    Load,               // pathを読み込む(空なら新規レイアウトにする)。discardRecoveryなら今の復元データを消す
    Quit,               // 終了する。discardRecoveryなら今の復元データを消してから終わる
};

struct LayoutFlowStep {
    LayoutFlowEffect effect = LayoutFlowEffect::None;
    std::string pathUtf8;
    bool discardRecovery = false;
};

enum class UnsavedChoice { Save, Discard, Cancel };

class LayoutDocumentFlow {
public:
    // savedPathUtf8は今のレイアウトの保存先(未保存の新規や.rs2なら空)。どの要求も、流れの途中なら無視する。
    LayoutFlowStep RequestSave(const std::string& savedPathUtf8);
    LayoutFlowStep RequestSaveAs();
    LayoutFlowStep RequestLoad(const std::string& pathUtf8, bool dirty, const std::string& savedPathUtf8);
    LayoutFlowStep RequestQuit(bool dirty, const std::string& savedPathUtf8);

    LayoutFlowStep OnUnsavedChoice(UnsavedChoice choice);
    // 名前が決まったとき。pathUtf8は解決済みの保存先、existsはそこにファイルがあるか。
    LayoutFlowStep OnSaveAsName(const std::string& pathUtf8, bool exists);
    LayoutFlowStep OnSaveAsCancelled();
    LayoutFlowStep OnOverwriteChoice(bool overwrite);
    LayoutFlowStep OnQuitChoice(bool quit);
    LayoutFlowStep OnSaveFinished(bool ok);

    // 名前を入力する画面を待っているか(画面が閉じられたらOnSaveAsCancelledを呼ぶ)。
    bool InSaveAs() const { return stage_ == Stage::SaveAs; }
    bool Idle() const { return stage_ == Stage::Idle; }

private:
    enum class Stage { Idle, Warning, QuitConfirm, SaveAs, Overwrite, Saving };
    enum class Goal { None, Load, Quit };

    LayoutFlowStep Reset();
    LayoutFlowStep BeginSaveToSavedPathOrAsk();
    LayoutFlowStep RunGoal(bool discardRecovery);

    Stage stage_ = Stage::Idle;
    Goal goal_ = Goal::None;
    std::string savedPathUtf8_;
    std::string loadPathUtf8_;
    std::string overwritePathUtf8_;
};

} // namespace rs::app::workspace
