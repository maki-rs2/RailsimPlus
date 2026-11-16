#pragma once

#include <optional>
#include <string>
#include <vector>

namespace rs::app::workspace {

// 環境のプルダウンの選択肢1つ。value_がシーンのenvPluginに入る値(空=デフォルトの空)。
struct SceneEnvOption {
    std::string name_;
    std::string value_;
    bool disabled_ = false;   // 選べない現在値(キューブの無い環境)を、選べない項目として出す
    bool operator==(const SceneEnvOption&) const = default;
};

struct ScenePanelRow {
    std::string name_;
    int warpCount_ = 0;
    int trainCount_ = 0;
    bool current_ = false;
    bool editing_ = false;
    std::string draft_;
    // 環境のプルダウン。env_は今の値に当たる選択肢の添字(の文字。プルダウンの値)で、先頭が選べない現在値のこともある。
    std::string env_ = "0";
    std::vector<SceneEnvOption> envOptions_;
    // このシーンの地形の名前(変更できない表示。無ければ空)。
    std::string surface_;
    bool operator==(const ScenePanelRow&) const = default;
};

// シーン追加のポップアップで選べる地形1つ。
struct SceneSurfaceChoice {
    std::string name_;
    std::string folderUtf8_;   // 画面には出さない。追加の操作へ渡す
    bool operator==(const SceneSurfaceChoice&) const = default;
};

struct SceneRenameRequest {
    int row_;
    std::string name_;
};

struct ScenePanelPresentation {
    std::vector<ScenePanelRow> rows_;
    std::vector<SceneSurfaceChoice> surfaceChoices_;
    std::string currentName_;
    bool active_ = false;
    bool canDelete_ = false;
    // 追加のポップアップ。canAdd_は選べる地形が1つ以上あるか(無いと追加を押せない)。
    bool adding_ = false;
    bool canAdd_ = false;
    int addSelection_ = 0;
    bool showArrows_ = false;
    int editingRow_ = -1;
    int deletingRow_ = -1;
    int draggingRow_ = -1;
    std::string deleteMessage_;

    void RefreshDerived();
    bool BeginRename(int row);
    std::optional<SceneRenameRequest> CommitRename();
    void CancelRename();
    // 追加のポップアップを開く(先頭の地形を選んだ状態)。名前の編集中なら取り消す。
    void OpenAdd();
    void CloseAdd();
    bool SelectAddSurface(int index);
    // 追加を確定するときの地形のフォルダ。選べる地形が無ければnullptr。
    const SceneSurfaceChoice* SelectedAddSurface() const;
    // 削除の確認を開く。messageは確認ダイアログの本文(呼び出し側がローカライズして作る)。
    bool OpenDelete(int row, std::string message);
    void CloseDelete();
    bool ValidRow(int row) const;
    bool CloseTransient();
};
} // namespace rs::app::workspace
