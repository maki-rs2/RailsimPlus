//
// GpuInitFailure.h - GPUの初期化に失敗した理由
//
#pragma once

#include <windows.h>

#include <string>

namespace rs::app {

// 起動できなかった理由。ログと、起動を止めるときのダイアログの両方がこれを使う。
struct GpuInitFailure {
    enum class Kind {
        None,
        NoDevice,    // 使えるハードウェアGPUがない(Feature Level 11_0 でデバイスを作れない)
        ShaderModel, // 選んだGPUが Shader Model 6.6 に対応していない
        Other,       // デバイス作成後の資源作成などの失敗(hr に原因)
    };
    Kind kind = Kind::None;
    std::string adapterName;       // 選んだGPU(NoDevice では空)
    int shaderModelMajor = 0;      // ShaderModel のとき、検出された最高の版
    int shaderModelMinor = 0;
    HRESULT hr = S_OK;             // ShaderModel の問い合わせ結果、または Other の失敗コード
};

} // namespace rs::app
