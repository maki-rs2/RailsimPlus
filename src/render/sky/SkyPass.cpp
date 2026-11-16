#include "SkyPass.h"

#include "../../platform/EnvironmentVariable.h"
#include <d3d12.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <numbers>
#include <vector>

#include <spdlog/spdlog.h>

#include "../postprocess/PostProcessFormats.h"
#include "../resource/RenderTuning.h"
#include "AtmosphereGpuUtil.h"
#include "SkyShader.h"

using Microsoft::WRL::ComPtr;

namespace rs::render {

namespace {

constexpr UINT kRootParamSrvTable = 0;
constexpr UINT kRootParamConstants = 1;
constexpr UINT kRootParamMoonSkyView = 2;
constexpr UINT kRootParamExposure = 3;
// t0..t2=大気LUT、t3=環境ソースキューブ(useEnvCube=1のときだけ引く)。
constexpr UINT kSkySrvCount = AtmosphereLuts::kSrvCount + 1;
// t4=月用のSky-View LUT(AtmosphereLuts::MoonSkyViewSrvGpuHandle)。大気LUTとは離れた枠なので別テーブル。
constexpr UINT kMoonSkyViewSrvCount = 1;
// t5=暗順応の1x1(IblGenerator::ExposureSrvGpuHandle。useAdaptation=1のときだけ引く)。別の枠なので別テーブル。
constexpr UINT kExposureSrvCount = 1;
constexpr UINT kConstantDwordCount = sizeof(SkyPassParams) / sizeof(float);
static_assert(kConstantDwordCount == 32, "HLSLのSkyConstantsと同じ32 DWORD");

// 円盤の縁をなめらかに落とす幅(角半径に対する比。内側のこの割合までは中心と同じ明るさ)。
constexpr float kSunDiskInnerRadiusRatio = 0.85f;

// 開発用: 環境変数 RS_DEBUG_SKY_FROM_ENVCUBE=1 のときだけ、背景を環境ソースキューブから引く。
// 起動後の最初の呼び出しで1回だけ読む(設定画面・JSONには出さない)。
bool DebugSkyFromEnvCubeEnabled() {
    static const bool kEnabled = []() {
        const char* value = rs::platform::ReadEnvironmentVariable("RS_DEBUG_SKY_FROM_ENVCUBE");
        return value != nullptr && std::string(value) == "1";
    }();
    return kEnabled;
}

} // namespace

struct SkyPass::Impl {
    ComPtr<ID3D12RootSignature> rootSig;
    ComPtr<ID3D12PipelineState> pso;
};

SkyPassParams MakeSkyPassParams(const rs::core::Mat4& view, float fovYRadians, float aspectRatio,
                                const rs::core::Vec3& sunDirectionToSun, float dayAlpha,
                                const RenderTuning& tuning) {
    SkyPassParams params;
    // row-vector 慣習の LookAt: 列がカメラのx/y/z軸(右・上・前)。
    for (int i = 0; i < 3; ++i) {
        params.cameraRight[i] = view.m[i * 4 + 0];
        params.cameraUp[i] = view.m[i * 4 + 1];
        params.cameraForward[i] = view.m[i * 4 + 2];
    }
    params.tanHalfFovY = std::tan(fovYRadians * 0.5f);
    params.aspect = aspectRatio;
    params.nightBlue = 1.0f - std::clamp(dayAlpha, 0.0f, 1.0f);
    const rs::core::Vec3 sun = sunDirectionToSun.Normalized();
    params.sunToSun[0] = sun.x;
    params.sunToSun[1] = sun.y;
    params.sunToSun[2] = sun.z;
    params.skyRadianceScale = tuning.skyRadianceScale;
    params.useEnvCube = DebugSkyFromEnvCubeEnabled() ? 1.0f : 0.0f;

    const float halfAngleRad = tuning.SunAngularRadiusRad();
    params.sunDiskCosOuter = std::cos(halfAngleRad);
    params.sunDiskCosInner = std::cos(halfAngleRad * kSunDiskInnerRadiusRatio);
    params.sunDiskIntensity = tuning.sunDiskIntensity;
    return params;
}

SkyPass::SkyPass() = default;
SkyPass::~SkyPass() = default;

bool SkyPass::Init(const rs::rhi::Device& device) {
    impl_.reset();
    if (!device.Valid()) return false;
    ID3D12Device* native = device.Native();
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();

    // t0..t3 = 大気LUT+環境ソースキューブ、b0 = SkyPassParams(ルート定数)、s0 = リニアクランプ。
    // t4 = 月用のSky-View LUT、t5 = 暗順応の1x1(それぞれ別テーブル)。
    const std::vector<UINT> extraTables = {kMoonSkyViewSrvCount, kExposureSrvCount};
    impl->rootSig = CreateAtmosphereRootSignatureTables(native, kSkySrvCount, kConstantDwordCount, extraTables);
    if (!impl->rootSig) return false;
    static_assert(kRootParamSrvTable == 0 && kRootParamConstants == 1 && kRootParamMoonSkyView == 2 &&
                      kRootParamExposure == 3,
                  "CreateAtmosphereRootSignatureの並び(SRVテーブル→ルート定数→追加のSRVテーブル)と一致させる");

    const std::string source = BuildSkyShader();
    ComPtr<ID3DBlob> vs = CompileAtmosphereShader(source, "VSMain", "vs_5_0", false);
    ComPtr<ID3DBlob> ps = CompileAtmosphereShader(source, "PSMain", "ps_5_0", false);
    if (!vs || !ps) return false;

    // 深度テスト・書き込みともにOFF(不透明パス/WBOITより前に全画面描画する前提)。
    impl->pso = CreateAtmosphereFullscreenPso(native, impl->rootSig.Get(), vs.Get(), ps.Get(), kSceneHdrFormat);
    if (!impl->pso) {
        spdlog::error("[sky] PSO");
        return false;
    }
    impl_ = std::move(impl);
    return true;
}

bool SkyPass::Ready() const { return impl_ != nullptr; }

void SkyPass::Record(const rs::rhi::CommandList& commandList, const AtmosphereLuts& luts,
                     const SkyPassParams& params, std::uint64_t exposureSrvGpuHandle) const {
    if (!Ready() || !commandList.Valid() || !luts.Ready()) return;
    ID3D12GraphicsCommandList* list = commandList.Native();

    ID3D12DescriptorHeap* heaps[] = {luts.SrvHeapRef().Native()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetGraphicsRootSignature(impl_->rootSig.Get());
    list->SetPipelineState(impl_->pso.Get());
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->IASetVertexBuffers(0, 0, nullptr);
    list->IASetIndexBuffer(nullptr);

    const D3D12_GPU_DESCRIPTOR_HANDLE table = {luts.SrvTableGpuHandle()};
    list->SetGraphicsRootDescriptorTable(kRootParamSrvTable, table);
    list->SetGraphicsRoot32BitConstants(kRootParamConstants, kConstantDwordCount, &params, 0);

    // 追加テーブルは未使用でも必ず埋める(未設定のルートパラメータはデバッグレイヤーが咎める)。
    // 月のLUTは未生成でも有効なSRV(moonRatio=0なら引かれない)。
    list->SetGraphicsRootDescriptorTable(kRootParamMoonSkyView,
                                         D3D12_GPU_DESCRIPTOR_HANDLE{luts.MoonSkyViewSrvGpuHandle()});

    // 暗順応の1x1。無いときは、同じヒープ内の透過率LUT(大気LUTの先頭。同じ2Dテクスチャ)で代用する。
    const std::uint64_t exposureHandle = exposureSrvGpuHandle != 0 ? exposureSrvGpuHandle : luts.SrvTableGpuHandle();
    list->SetGraphicsRootDescriptorTable(kRootParamExposure, D3D12_GPU_DESCRIPTOR_HANDLE{exposureHandle});

    list->DrawInstanced(3, 1, 0, 0);
}

} // namespace rs::render
