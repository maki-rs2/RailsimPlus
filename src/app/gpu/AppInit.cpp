//
// AppInit.cpp - AppInit の実装(main.cppから分離)
//
#include "AppInit.h"

#include <chrono>

#include <spdlog/spdlog.h>

#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"

#include "App.h"
#include "../ui/ImGuiFontManager.h"

namespace rs::app {

bool InitGraphicsAndImGui(App& app, HWND hwnd, UINT width, UINT height,
                          ImVector<ImWchar>& fontRanges, bool viewerBackground) {
    // 起動フェーズ別の所要時間(gfx+imguiの内訳)。
    using InitClock = std::chrono::steady_clock;
    auto sinceMs = [](InitClock::time_point t) {
        return std::chrono::duration<double, std::milli>(InitClock::now() - t).count();
    };

    const InitClock::time_point tDevice0 = InitClock::now();
    if (!app.Init(hwnd, width, height, viewerBackground)) {
        spdlog::error("D3D12 init failed");
        return false;
    }
    const double deviceMs = sinceMs(tDevice0);
    spdlog::info("D3D12 initialized ({}x{})", width, height);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    // NavEnableKeyboardは付けない: 有効にするとImGuiパネルにフォーカスが
    // 移っただけ(テキスト入力中でなくても)でio.WantCaptureKeyboardが真になり、
    // WASDカメラ移動等のショートカットがウィンドウアクティブ時に広く無効化
    // されてしまう。Tab/矢印キーでのウィジェット間
    // ナビゲーションは失うが、ゲーム内ショートカットとの両立を優先する。
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();

    // 日本語フォント(Noto Sans JP可変フォント)をロードする。ASCII + 常用漢字/かな
    // (GetGlyphRangesJapanese)をベース範囲とし、プラグイン読込時に稀な漢字を
    // 追加してアトラスを再構築する(main.cppのフレームループ側で継続対応)。
    // DX12 バックエンド初期化前なので rebuildBackend=false。
    const InitClock::time_point tFont0 = InitClock::now();
    BuildJapaneseFontAtlas(io, fontRanges, nullptr, /*rebuildBackend=*/false);
    const double fontMs = sinceMs(tFont0);

    const InitClock::time_point tBackend0 = InitClock::now();
    ImGui_ImplWin32_Init(hwnd);
    D3D12_CPU_DESCRIPTOR_HANDLE fontCpu = app.SrvHeap()->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE fontGpu = app.SrvHeap()->GetGPUDescriptorHandleForHeapStart();
    fontCpu.ptr += SIZE_T(kSrvLayout.imgui) * app.SrvStride();
    fontGpu.ptr += SIZE_T(kSrvLayout.imgui) * app.SrvStride();
    ImGui_ImplDX12_Init(app.Device(), kFrameCount, kBackBufferFormat, app.SrvHeap(), fontCpu,
                        fontGpu);
    spdlog::info("ImGui {} initialized", IMGUI_VERSION);
    spdlog::info("[startup] gfx内訳: device+pso={:.0f}ms font={:.0f}ms imguiBackend={:.0f}ms",
                 deviceMs, fontMs, sinceMs(tBackend0));
    return true;
}

void ShutdownGraphicsAndImGui(App& app) {
    // ImGui_ImplDX12_Shutdown は DX12 の資源を解放するので、最後のフレームが GPU で終わってから呼ぶ
    // (待たないと --smoke の終了時にプロセスが異常終了コードで落ちることがある)
    app.WaitForGpu();
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    app.Shutdown();
}

} // namespace rs::app
