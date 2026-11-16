//
// ShellPickerDialog.h - Windowsのフォルダ・ファイルの選択ダイアログを、メインのスレッドを止めずに出す。
//
// メインのスレッドはCOINIT_MULTITHREADEDなので、
// シェルのダイアログはCOINIT_APARTMENTTHREADEDの専用スレッドで出す。メインは毎フレームTryTakeで
// 結果を見る(ダイアログが開いている間も描画は止まらない)。ウィンドウのハンドルはヘッダに出さず、
// void*で受ける。
//
#pragma once

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace rs::platform_file {

// ファイルの種類の絞り込み1つ(例: 名前「レイアウト」、パターン「*.rsl;*.rs2」)。
struct ShellFileType {
    std::string nameUtf8;
    std::string patternUtf8;
};

class ShellPicker {
public:
    ShellPicker() = default;
    // ダイアログが開いたままなら閉じて、スレッドを待つ。
    ~ShellPicker();
    ShellPicker(const ShellPicker&) = delete;
    ShellPicker& operator=(const ShellPicker&) = delete;

    // ダイアログを出す。すでに開いている(結果をTryTakeで受け取るまで)ならfalseを返して何もしない。
    // ownerHwndはダイアログの親にするウィンドウ(HWND。nullptrでもよい)、titleUtf8はタイトル。
    // initialFolderUtf8が空でなく実在するフォルダなら、前回の場所に関わらず最初にそこを開く。
    bool Start(void* ownerHwnd, const std::string& titleUtf8, const std::string& initialFolderUtf8 = {});
    // ファイルを1つ選ぶダイアログを出す。それ以外はStartと同じ。
    bool StartOpenFile(void* ownerHwnd, const std::string& titleUtf8, const ShellFileType& fileType);

    // 終わっていれば、選ばれたパス(UTF-8。キャンセルや失敗ならnullopt)をoutPathUtf8へ渡して
    // trueを返す。渡したあとは次のStartを受け付ける。まだ開いているときはfalseを返す。
    bool TryTake(std::optional<std::string>& outPathUtf8);

    // Startしてから、結果をTryTakeで受け取るまでの間true。
    bool Open() const { return open_.load(); }

private:
    bool StartThread(void* ownerHwnd, const std::string& titleUtf8, bool pickFolder, const ShellFileType& fileType,
                     const std::string& initialFolderUtf8);
    void Run(void* ownerHwnd, std::wstring title, bool pickFolder, std::wstring typeName, std::wstring typePattern,
             std::wstring initialFolder);
    // ダイアログのスレッドが持つ最上位ウィンドウへWM_CLOSEを送る(どのスレッドから呼んでもよい)。
    void PostCloseToDialogWindows();

    std::thread thread_;
    std::atomic<bool> open_{false};      // Startから、結果をTryTakeで渡すまで
    std::atomic<bool> finished_{false};  // スレッドが結果を置き終えた
    std::atomic<bool> cancelRequested_{false};
    std::atomic<unsigned long> threadId_{0};
    std::mutex resultMutex_;
    std::optional<std::string> result_;  // resultMutex_で守る
};

} // namespace rs::platform_file
