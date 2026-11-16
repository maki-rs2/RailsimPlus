#include "ShellPickerDialog.h"

#include <chrono>

#include <Windows.h>
#include <ShObjIdl.h>
#include <wrl/client.h>

#include "../text/Encoding.h"

namespace rs::platform_file {

namespace {

// 終了時、ダイアログが閉じるまでWM_CLOSEを送り直す間隔。
constexpr std::chrono::milliseconds kClosePollInterval{10};

// IFileOpenDialogを出して、選ばれたパス(UTF-16)を返す。キャンセル・失敗は空。
std::wstring ShowDialog(HWND owner, const std::wstring& title, bool pickFolder, const std::wstring& typeName,
                        const std::wstring& typePattern, const std::wstring& initialFolder) {
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return {};
    }
    FILEOPENDIALOGOPTIONS options = 0;
    if (FAILED(dialog->GetOptions(&options))) return {};
    const FILEOPENDIALOGOPTIONS kindOption = pickFolder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST;
    if (FAILED(dialog->SetOptions(options | kindOption | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST))) return {};
    if (!pickFolder) {
        const COMDLG_FILTERSPEC filter = {typeName.c_str(), typePattern.c_str()};
        if (FAILED(dialog->SetFileTypes(1, &filter))) return {};
    }
    dialog->SetTitle(title.c_str());
    if (!initialFolder.empty()) {
        // SetFolderは前回の場所に関わらず、必ずそのフォルダで開く。実在しなければ何もしない(既定の場所で開く)。
        Microsoft::WRL::ComPtr<IShellItem> folder;
        if (SUCCEEDED(SHCreateItemFromParsingName(initialFolder.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
            dialog->SetFolder(folder.Get());
        }
    }
    if (FAILED(dialog->Show(owner))) return {}; // キャンセルもここ(ERROR_CANCELLED)

    Microsoft::WRL::ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(&item))) return {};
    PWSTR rawPath = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath)) || rawPath == nullptr) return {};
    std::wstring path = rawPath;
    CoTaskMemFree(rawPath);
    return path;
}

BOOL CALLBACK PostCloseToWindow(HWND window, LPARAM) {
    PostMessageW(window, WM_CLOSE, 0, 0);
    return TRUE;
}

} // namespace

ShellPicker::~ShellPicker() {
    if (!thread_.joinable()) return;
    // 開いたままなら閉じる。IFileDialog::Closeは別のアパートメントが作ったCOMオブジェクトを
    // 直接呼ぶことになり安全でないので、ダイアログのスレッドが持つ最上位ウィンドウへ
    // WM_CLOSE(PostMessageはどのスレッドからでも呼べる)を送る。Showの前なら、スレッドが
    // cancelRequested_を見てダイアログを出さずに終わる。
    cancelRequested_.store(true);
    while (!finished_.load()) {
        PostCloseToDialogWindows();
        std::this_thread::sleep_for(kClosePollInterval);
    }
    thread_.join();
}

bool ShellPicker::Start(void* ownerHwnd, const std::string& titleUtf8, const std::string& initialFolderUtf8) {
    return StartThread(ownerHwnd, titleUtf8, true, {}, initialFolderUtf8);
}

bool ShellPicker::StartOpenFile(void* ownerHwnd, const std::string& titleUtf8, const ShellFileType& fileType) {
    return StartThread(ownerHwnd, titleUtf8, false, fileType, {});
}

bool ShellPicker::StartThread(void* ownerHwnd, const std::string& titleUtf8, bool pickFolder,
                              const ShellFileType& fileType, const std::string& initialFolderUtf8) {
    if (open_.load()) return false;
    if (thread_.joinable()) thread_.join(); // 前回のスレッド(終わっている)の後始末
    finished_.store(false);
    cancelRequested_.store(false);
    threadId_.store(0);
    open_.store(true);
    thread_ = std::thread(&ShellPicker::Run, this, ownerHwnd, rs::platform_text::Utf8ToWide(titleUtf8), pickFolder,
                          rs::platform_text::Utf8ToWide(fileType.nameUtf8),
                          rs::platform_text::Utf8ToWide(fileType.patternUtf8),
                          rs::platform_text::Utf8ToWide(initialFolderUtf8));
    return true;
}

bool ShellPicker::TryTake(std::optional<std::string>& outPathUtf8) {
    if (!open_.load() || !finished_.load()) return false;
    {
        const std::lock_guard<std::mutex> lock(resultMutex_);
        outPathUtf8 = std::move(result_);
        result_.reset();
    }
    open_.store(false);
    return true;
}

void ShellPicker::Run(void* ownerHwnd, std::wstring title, bool pickFolder, std::wstring typeName,
                      std::wstring typePattern, std::wstring initialFolder) {
    threadId_.store(GetCurrentThreadId());
    std::optional<std::string> picked;
    const HRESULT initResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(initResult)) {
        if (!cancelRequested_.load()) {
            const std::wstring path = ShowDialog(static_cast<HWND>(ownerHwnd), title, pickFolder, typeName, typePattern,
                                             initialFolder);
            if (!path.empty() && !cancelRequested_.load()) picked = rs::platform_text::WideToUtf8(path);
        }
        CoUninitialize();
    }
    {
        const std::lock_guard<std::mutex> lock(resultMutex_);
        result_ = std::move(picked);
    }
    finished_.store(true);
}

void ShellPicker::PostCloseToDialogWindows() {
    const DWORD id = threadId_.load();
    if (id != 0) EnumThreadWindows(id, PostCloseToWindow, 0);
}

} // namespace rs::platform_file
