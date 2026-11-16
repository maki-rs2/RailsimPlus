// GpuContext のD3D12デバイス作成(アダプタ選択と Shader Model 確認)
#include "GpuContext.h"

#include <spdlog/spdlog.h>

#include "../../platform/text/Encoding.h"

namespace rs::app {

// ハードウェアGPUを性能順に試してデバイスを作り、Shader Model 6.6(Bindless描画に必須)を確認する。
// 使えなければ false を返し、initFailure_ に理由を入れる(各アダプタの結果はログへ出す)。
bool GpuContext::CreateDevice() {
    GpuInitFailure& failure = initFailure_;
    ComPtr<IDXGIAdapter1> adapter;
    std::string usedName;
    for (UINT i = 0;
         SUCCEEDED(factory_->EnumAdapterByGpuPreference(
             i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)));
         ++i) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        const std::string adapterName = rs::platform_text::WideToUtf8(desc.Description);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            spdlog::info("adapter[{}] {}: ソフトウェアアダプタのため使いません", i, adapterName);
            continue;
        }
        const HRESULT createHr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                                   IID_PPV_ARGS(&device_));
        if (SUCCEEDED(createHr)) {
            spdlog::info("adapter: {} (VRAM {:.0f} MB)", adapterName,
                         desc.DedicatedVideoMemory / (1024.0 * 1024.0));
            usedName = adapterName;
            failure.adapterName = adapterName;
            break;
        }
        spdlog::warn("adapter[{}] {}: D3D12デバイス(Feature Level 11_0)を作れませんでした"
                     "(HRESULT 0x{:08X})",
                     i, adapterName, static_cast<unsigned>(createHr));
    }
    if (!device_) {
        spdlog::error("no D3D12 device: 使えるGPUが見つかりませんでした"
                      "(D3D12 Feature Level 11_0 に対応するハードウェアGPUが必要です)");
        failure.kind = GpuInitFailure::Kind::NoDevice;
        return false;
    }
    // D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED
    // (ResourceDescriptorHeap[]直接参照)はShader Model 6.6が必要。最低要件は
    // RTX 3050級だが、無言クラッシュを避けるため起動時に明示チェックする。
    D3D12_FEATURE_DATA_SHADER_MODEL sm = {D3D_SHADER_MODEL_6_6};
    const HRESULT smHr = device_->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm));
    if (FAILED(smHr) || sm.HighestShaderModel < D3D_SHADER_MODEL_6_6) {
        failure.kind = GpuInitFailure::Kind::ShaderModel;
        failure.shaderModelMajor = (sm.HighestShaderModel >> 4) & 0xF;
        failure.shaderModelMinor = sm.HighestShaderModel & 0xF;
        failure.hr = smHr;
        spdlog::error("{}: このGPU/ドライバはShader Model 6.6に対応していません"
                      "(Bindless描画に必須)。検出された最高: {}.{}、"
                      "問い合わせ結果 HRESULT 0x{:08X}。GPUドライバを更新するか、"
                      "対応GPU(RTX 3050以上目安)でお試しください。",
                      usedName, failure.shaderModelMajor, failure.shaderModelMinor,
                      static_cast<unsigned>(smHr));
        return false;
    }
    return true;
}

} // namespace rs::app
