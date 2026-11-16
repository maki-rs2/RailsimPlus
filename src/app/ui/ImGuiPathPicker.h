//
// ImGuiPathPicker.h - ImGuiパネルのパス欄を、Windowsのフォルダ・ファイル選択ダイアログで埋める共有状態
//
// ダイアログ(ShellPicker)はアプリに1つだけで、どの欄が要求したかを覚える。結果の受け取りは
// BeginFrameで毎フレーム1か所だけ行うので、パネルが閉じていてもダイアログが開いたままで固まらない。
// 選ばれたパスは、要求した欄が描かれたフレームにだけ届く(そのフレームに描かれなかった結果は捨てる。
// 別の欄が他の欄の結果を受け取ることもない)。
//
#pragma once

#include <optional>
#include <string>

#include "../../platform/file/ShellPickerDialog.h"

namespace rs::app {

// 何を選ばせるか。fileTypeがnullならフォルダ、あればその種類のファイルを1つ。
struct PathPickSpec {
    const char* titleUtf8 = "";
    const rs::platform_file::ShellFileType* fileType = nullptr;
};

inline PathPickSpec FolderPick(const char* titleUtf8) { return {titleUtf8, nullptr}; }
inline PathPickSpec FilePick(const char* titleUtf8, const rs::platform_file::ShellFileType& fileType) {
    return {titleUtf8, &fileType};
}

class ImGuiPathPicker {
public:
    // 毎フレームの先頭(パネルを描く前)に1回呼ぶ。前のフレームで受け取られなかった結果を捨て、
    // ダイアログが終わっていれば結果を取り出す。ownerWindowはダイアログの親(HWND)。
    void BeginFrame(void* ownerWindow);

    // 読み取り専用の表示欄+「参照…」ボタン。選ばれたパスが届いたフレームだけ値を返す。
    // labelは"表示名##識別子"の形で、同じウィンドウ内で一意にする。フォルダを選ぶときは
    // shownを最初に開く場所にする。
    std::optional<std::string> Field(const char* label, const std::string& shown, const PathPickSpec& spec);
    // ボタンだけ(表示すべき現在値が無いとき)。initialFolderUtf8は最初に開くフォルダ(フォルダ選択のみ)。
    std::optional<std::string> Button(const char* label, const PathPickSpec& spec,
                                      const std::string& initialFolderUtf8 = {});

private:
    struct Result {
        unsigned int requesterId = 0;
        std::string pathUtf8;
    };

    rs::platform_file::ShellPicker picker_;
    void* ownerWindow_ = nullptr;
    unsigned int requesterId_ = 0;  // 開いているダイアログを要求した欄のID
    std::optional<Result> result_;  // 届いた結果。次のBeginFrameで捨てる
};

} // namespace rs::app
