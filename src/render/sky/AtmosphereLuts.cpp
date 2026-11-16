#include "AtmosphereLuts.h"

#include <cstddef>
#include <cstdint>

#include <d3d12.h>
#include <wrl/client.h>

#include <string>

#include <spdlog/spdlog.h>

#include "../postprocess/PostProcessFormats.h"
#include "../postprocess/SceneHdrTarget.h"
#include "AtmosphereGpuUtil.h"
#include "AtmosphereMath.h"
#include "AtmosphereParams.h"
#include "AtmosphereShader.h"

using Microsoft::WRL::ComPtr;

namespace rs::render {

namespace {

// 透過率・多重散乱のLUTの形式。入れ物のSceneHdrTargetの既定の形式(Half16)と同じ(PSOのRTV形式を合わせるため)。
constexpr DXGI_FORMAT kAtmosphereLutFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
static_assert(kAtmosphereLutFormat == kSceneHdrFormat,
              "LUTの入れ物にSceneHdrTargetを再利用しているため、形式を一致させること");
// Sky-View LUT(太陽用・月用)はR32G32B32A32_FLOAT。薄明の空の放射輝度は小さく、fp16だと最小の非正規数に近い値が
// 量子化され、後から掛ける暗順応の倍率で空の色が段になるため。
constexpr DXGI_FORMAT kSkyViewLutFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;

struct SkyViewShaderConstants {
    float sunCosZenith;
    float observerHeightKm;
};
constexpr UINT kSkyViewConstantDwords = sizeof(SkyViewShaderConstants) / sizeof(std::uint32_t);
static_assert(sizeof(SkyViewShaderConstants) == 8 && offsetof(SkyViewShaderConstants, observerHeightKm) == 4,
              "SkyViewConstants の太陽方向・高さと配置を一致させる");

// 多重散乱は透過率(t0)を、Sky-Viewは透過率+多重散乱(t0,t1)を引く。そのSRV数。
constexpr UINT kMultiScatSrvCount = 1;
constexpr UINT kSkyViewSrvCount = 2;

} // namespace

struct AtmosphereLuts::Impl {
    rs::rhi::SrvHeap srvHeap;
    SceneHdrTarget transmittance;
    SceneHdrTarget multiScat;
    SceneHdrTarget skyView;
    SceneHdrTarget moonSkyView; // 月用のSky-View(太陽用と同じシェーダ・大きさ。光源の方向だけ月)

    // パスごとのルート署名・PSO(透過率=SRVなし、多重散乱=t0、Sky-View=t0..t1+定数)。
    ComPtr<ID3D12RootSignature> rootSigTransmittance;
    ComPtr<ID3D12RootSignature> rootSigMultiScat;
    ComPtr<ID3D12RootSignature> rootSigSkyView;
    ComPtr<ID3D12PipelineState> psoTransmittance;
    ComPtr<ID3D12PipelineState> psoMultiScat;
    ComPtr<ID3D12PipelineState> psoSkyView;

    std::uint32_t firstSrvIndex = 0; // 3枠の先頭(=透過率)の絶対番号
    bool staticLutsBuilt = false; // 透過率・多重散乱は1回だけ作る
    bool hasSkyView = false;
    rs::core::Vec3 lastSunDirection;
    double lastSkyViewHeightKm = 0.0;
    bool hasMoonSkyView = false;
    rs::core::Vec3 lastMoonDirection;
    double lastMoonSkyViewHeightKm = 0.0;
};

AtmosphereLuts::AtmosphereLuts() = default;
AtmosphereLuts::~AtmosphereLuts() = default;

bool AtmosphereLuts::Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap,
                          std::uint32_t firstSrvIndex, std::uint32_t moonSrvIndex) {
    impl_.reset();
    if (!device.Valid() || !srvHeap.Valid()) return false;
    ID3D12Device* native = device.Native();
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    impl->srvHeap = srvHeap;
    impl->firstSrvIndex = firstSrvIndex;

    // 3枚のLUT(連続した3枠のSRV)。クリア値は使わない(毎回全面を描く)。
    const float clearColor[3] = {0.0f, 0.0f, 0.0f};
    SceneHdrTarget* targets[AtmosphereLuts::kSrvCount] = {&impl->transmittance, &impl->multiScat, &impl->skyView};
    const HdrTargetFormat formats[AtmosphereLuts::kSrvCount] = {HdrTargetFormat::Half16, HdrTargetFormat::Half16,
                                                                HdrTargetFormat::Float32};
    const std::uint32_t widths[AtmosphereLuts::kSrvCount] = {atmosphere::kTransmittanceLutWidth,
                                             atmosphere::kMultiScatLutSize, atmosphere::kSkyViewLutWidth};
    const std::uint32_t heights[AtmosphereLuts::kSrvCount] = {atmosphere::kTransmittanceLutHeight,
                                              atmosphere::kMultiScatLutSize, atmosphere::kSkyViewLutHeight};
    for (std::uint32_t i = 0; i < AtmosphereLuts::kSrvCount; ++i) {
        if (!targets[i]->Init(device, srvHeap, firstSrvIndex + i, clearColor, formats[i]) ||
            !targets[i]->Resize(widths[i], heights[i])) {
            spdlog::error("[atmosphere] LUT {} の作成に失敗しました", i);
            return false;
        }
    }

    if (!impl->moonSkyView.Init(device, srvHeap, moonSrvIndex, clearColor, HdrTargetFormat::Float32) ||
        !impl->moonSkyView.Resize(atmosphere::kSkyViewLutWidth, atmosphere::kSkyViewLutHeight)) {
        spdlog::error("[atmosphere] 月用のSky-View LUT の作成に失敗しました");
        return false;
    }

    impl->rootSigTransmittance = CreateAtmosphereRootSignature(native, 0, 0);
    impl->rootSigMultiScat = CreateAtmosphereRootSignature(native, kMultiScatSrvCount, 0);
    impl->rootSigSkyView = CreateAtmosphereRootSignature(native, kSkyViewSrvCount, kSkyViewConstantDwords);
    if (!impl->rootSigTransmittance || !impl->rootSigMultiScat || !impl->rootSigSkyView) return false;

    const std::string source = BuildAtmosphereCommonShader() + kAtmosphereLutPassesShader;
    ComPtr<ID3DBlob> vs = CompileAtmosphereShader(source, "VSMain", "vs_5_0", false);
    ComPtr<ID3DBlob> transmittancePs = CompileAtmosphereShader(source, "TransmittancePS", "ps_5_0", false);
    ComPtr<ID3DBlob> multiScatPs = CompileAtmosphereShader(source, "MultiScatPS", "ps_5_0", false);
    ComPtr<ID3DBlob> skyViewPs = CompileAtmosphereShader(source, "SkyViewPS", "ps_5_0", true);
    if (!vs || !transmittancePs || !multiScatPs || !skyViewPs) return false;

    impl->psoTransmittance = CreateAtmosphereFullscreenPso(native, impl->rootSigTransmittance.Get(), vs.Get(),
                                                           transmittancePs.Get(), kAtmosphereLutFormat);
    impl->psoMultiScat = CreateAtmosphereFullscreenPso(native, impl->rootSigMultiScat.Get(), vs.Get(),
                                                       multiScatPs.Get(), kAtmosphereLutFormat);
    impl->psoSkyView = CreateAtmosphereFullscreenPso(native, impl->rootSigSkyView.Get(), vs.Get(),
                                                     skyViewPs.Get(), kSkyViewLutFormat);
    if (!impl->psoTransmittance || !impl->psoMultiScat || !impl->psoSkyView) {
        spdlog::error("[atmosphere] PSO");
        return false;
    }
    impl_ = std::move(impl);
    return true;
}

bool AtmosphereLuts::Ready() const { return impl_ != nullptr; }

bool AtmosphereLuts::Update(const rs::rhi::CommandList& commandList, const rs::core::Vec3& sunDirectionToSun,
                            const rs::core::Vec3& moonDirectionToMoon, float moonRatio, float observerHeightKm) {
    if (!Ready() || !commandList.Valid()) return false;
    Impl& impl = *impl_;
    const rs::core::Vec3 sunDirection = sunDirectionToSun.Normalized();
    const bool needSkyView =
        atmosphere::ShouldRegenerateSkyView(impl.hasSkyView, impl.lastSunDirection, sunDirection,
                                            atmosphere::kSkyViewRegenerateThresholdDeg) ||
        atmosphere::ShouldRegenerateForObserverHeight(impl.lastSkyViewHeightKm, observerHeightKm,
                                                      atmosphere::kObserverHeightRegenerateKm);
    const rs::core::Vec3 moonDirection = moonDirectionToMoon.Normalized();
    const bool needMoonSkyView =
        moonRatio > 0.0f &&
        (atmosphere::ShouldRegenerateSkyView(impl.hasMoonSkyView, impl.lastMoonDirection, moonDirection,
                                             atmosphere::kSkyViewRegenerateThresholdDeg) ||
         atmosphere::ShouldRegenerateForObserverHeight(impl.lastMoonSkyViewHeightKm, observerHeightKm,
                                                       atmosphere::kObserverHeightRegenerateKm));
    if (impl.staticLutsBuilt && !needSkyView && !needMoonSkyView) return false;

    ID3D12GraphicsCommandList* list = commandList.Native();
    ID3D12DescriptorHeap* heaps[] = {impl.srvHeap.Native()};
    list->SetDescriptorHeaps(1, heaps);

    // 透過率 → 多重散乱 → Sky-View の順(後ろの2枚は前の枚を引く)。各パスの終わりに対象をSRV状態へ戻す。
    const std::uint64_t srvTable = impl.transmittance.SrvGpuHandle();
    if (!impl.staticLutsBuilt) {
        DrawAtmosphereLut(commandList, impl.transmittance, impl.rootSigTransmittance.Get(), impl.psoTransmittance.Get(), 0,
                     nullptr, 0);
        DrawAtmosphereLut(commandList, impl.multiScat, impl.rootSigMultiScat.Get(), impl.psoMultiScat.Get(), srvTable,
                     nullptr, 0);
        impl.staticLutsBuilt = true;
    }
    if (needSkyView) {
        const SkyViewShaderConstants constants = {sunDirection.y, observerHeightKm};
        DrawAtmosphereLut(commandList, impl.skyView, impl.rootSigSkyView.Get(), impl.psoSkyView.Get(), srvTable, &constants,
                     kSkyViewConstantDwords);
        impl.lastSunDirection = sunDirection;
        impl.lastSkyViewHeightKm = observerHeightKm;
        impl.hasSkyView = true;
    }
    if (needMoonSkyView) {
        const SkyViewShaderConstants constants = {moonDirection.y, observerHeightKm};
        DrawAtmosphereLut(commandList, impl.moonSkyView, impl.rootSigSkyView.Get(), impl.psoSkyView.Get(), srvTable, &constants,
                     kSkyViewConstantDwords);
        impl.lastMoonDirection = moonDirection;
        impl.lastMoonSkyViewHeightKm = observerHeightKm;
        impl.hasMoonSkyView = true;
    }
    return true;
}

std::uint64_t AtmosphereLuts::SrvTableGpuHandle() const {
    return impl_ ? impl_->transmittance.SrvGpuHandle() : 0;
}

std::uint64_t AtmosphereLuts::MoonSkyViewSrvGpuHandle() const {
    return impl_ ? impl_->moonSkyView.SrvGpuHandle() : 0;
}

std::uint32_t AtmosphereLuts::TransmittanceSrvIndex() const { return impl_ ? impl_->firstSrvIndex : 0; }

const rs::rhi::SrvHeap& AtmosphereLuts::SrvHeapRef() const {
    static const rs::rhi::SrvHeap kEmpty;
    return impl_ ? impl_->srvHeap : kEmpty;
}

} // namespace rs::render
