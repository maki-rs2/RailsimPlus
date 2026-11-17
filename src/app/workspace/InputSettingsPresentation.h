#pragma once

#include "GameSettingsPresentation.h"

namespace rs::app::workspace {

struct InputSettingsRow {
    std::string section_;
    std::string operation_;
    std::string context_;
    std::string binding_;
    bool warning_ = false;
    bool waiting_ = false;
    // 主キーのほかに、同じキーのCtrl付き(移動のダッシュ用。画面には出さない)も使う行か。
    bool ctrlDash_ = false;
};

// 画面に行を出さない(変えられない)割り当て。変えられる操作が奪えないよう、重なりの判定にだけ使う。
struct InputSettingsFixedBinding {
    std::string context_;
    std::string binding_;
};

// Conflictは変えられる別の行との重なり(確認すれば相手を未割当にして割り当てられる)、Reservedは固定の割り当てとの重なり(割り当てられない)。
enum class InputSettingsCapture { Ignored, Cancelled, Ready, Conflict, Reserved };

struct InputSettingsPresentation {
    std::string mouseMode_;
    std::string mouseLabel_;
    std::string keysLabel_;
    std::string waitingLabel_;
    std::string resetLabel_;
    std::string resetAllLabel_;
    std::string warningLabel_;
    std::string unassignedLabel_;
    std::string confirmMessage_;
    std::string confirmLabel_;
    std::string cancelLabel_;
    // 割り当てられないキーが押されたときのトースト文言(画面には出さない)。
    std::string unassignableMessage_;
    // 固定の割り当てと重なるキーが押されたときのトースト文言(画面には出さない)。
    std::string reservedKeyMessage_;
    // 「すべて既定に戻す」の確認の文言(画面の確認ダイアログに出す)。
    std::string resetAllConfirmMessage_;
    std::vector<GameSettingsChoice> mouseChoices_;
    std::vector<InputSettingsRow> rows_;
    std::vector<InputSettingsFixedBinding> fixed_;
    int waitingRow_ = -1;
    std::string pendingBinding_;

    bool BeginCapture(int row);
    void CancelCapture();
    InputSettingsCapture Capture(const std::string& binding);
    // 同じコンテキスト(commonはすべてと同時に使う)で、同じ入力が他の行か固定の割り当てにあるか。
    bool Conflicts(int row, const std::string& binding) const;
    // Conflictsのうち、変えられる別の行と重なるもの(行の番号)。相手が未割当の行は含まない。
    std::vector<int> ConflictingRows(int row, const std::string& binding) const;
    // Conflictsのうち、固定の割り当てと重なるか。
    bool ConflictsWithFixed(int row, const std::string& binding) const;
    void RefreshWarnings();
};
} // namespace rs::app::workspace
