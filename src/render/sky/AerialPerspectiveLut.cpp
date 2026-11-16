#include "AerialPerspectiveLut.h"

#include <cstddef>
#include <cstdint>

#include <d3d12.h>
#include <wrl/client.h>

#include <string>

#include <spdlog/spdlog.h>

#include "../postprocess/PostProcessFormats.h"
#include "../postprocess/SceneHdrTarget.h"
#include "../resource/RenderTuning.h"
#include "AerialPerspectiveDebug.h"
#include "AerialPerspectiveShader.h"
#include "AtmosphereGpuUtil.h"
#include "AtmosphereShader.h"

using Microsoft::WRL::ComPtr;

namespace rs::render {

namespace {

// LUTの形式。入れ物のSceneHdrTargetの既定の形式(Half16)と同じ(PSOのRTV形式を合わせるため)。
constexpr DXGI_FORMAT kAerialLutFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
static_assert(kAerialLutFormat == kSceneHdrFormat,
              "LUTの入れ物にSceneHdrTargetを再利用しているため、形式を一致させること");

// 透過率LUT(t0)と多重散乱LUT(t1)を引く。AtmosphereLuts は3枚を連続した枠に持つので、先頭から2枠のテーブル。
constexpr UINT kAerialSrvCount = 2;

static_assert(sizeof(atmosphere::AerialCameraFrame) == 12 * sizeof(float), "HLSLの AerialConstants の前半と同じ12 DWORD");
struct AerialShaderConstants {
    atmosphere::AerialCameraFrame camera;
    float sunToSun[3];
    float padding;
};
constexpr UINT kAerialPassConstantDwords = sizeof(AerialShaderConstants) / sizeof(std::uint32_t);
static_assert(sizeof(AerialShaderConstants) == 64 && offsetof(AerialShaderConstants, sunToSun) == 48 &&
              offsetof(atmosphere::AerialCameraFrame, tanHalfFovX) == 12 &&
              offsetof(atmosphere::AerialCameraFrame, up) == 16 &&
              offsetof(atmosphere::AerialCameraFrame, tanHalfFovY) == 28 &&
              offsetof(atmosphere::AerialCameraFrame, forward) == 32 &&
              offsetof(atmosphere::AerialCameraFrame, padding) == 44,
              "AerialConstants のカメラ・太陽方向と配置を一致させる");

}  // namespace

struct AerialPerspectiveLut::Impl {
    rs::rhi::Device device{nullptr}; // 所有しない(読み戻しの確認が資源を作るために持つ)
    SceneHdrTarget lut;
    ComPtr<ID3D12RootSignature> rootSig;
    ComPtr<ID3D12PipelineState> pso;
    std::uint32_t srvIndex = 0;
    int sliceCount = 0;
    int tileSize = 0;
    float maxDistanceKm = 0.0f;
    float aerosolOpticalDepth = 0.0f;
    bool enabled = true;
    AerialPerspectiveDebug debug;
};

AerialPerspectiveLut::AerialPerspectiveLut() = default;
AerialPerspectiveLut::~AerialPerspectiveLut() = default;

bool AerialPerspectiveLut::Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap,
                                std::uint32_t srvIndex) {
    impl_.reset();
    if (!device.Valid() || !srvHeap.Valid()) return false;
    const RenderTuning& tuning = RenderTuning::Embedded();
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    impl->device = device;
    impl->srvIndex = srvIndex;
    impl->sliceCount = tuning.aerialPerspectiveSliceCount;
    impl->tileSize = tuning.aerialPerspectiveTileSize;
    impl->maxDistanceKm = tuning.aerialPerspectiveMaxDistanceKm;
    impl->aerosolOpticalDepth = tuning.atmosphereAerosolOpticalDepth;

    // クリア値は使わない(毎回全面を描く)。
    const float clearColor[3] = {0.0f, 0.0f, 0.0f};
    if (!impl->lut.Init(device, srvHeap, srvIndex, clearColor, HdrTargetFormat::Half16) ||
        !impl->lut.Resize(static_cast<std::uint32_t>(impl->sliceCount * impl->tileSize),
                          static_cast<std::uint32_t>(impl->tileSize))) {
        spdlog::error("[aerial_perspective] LUT の作成に失敗しました");
        return false;
    }

    ID3D12Device* native = device.Native();
    impl->rootSig = CreateAtmosphereRootSignature(native, kAerialSrvCount, kAerialPassConstantDwords);
    if (!impl->rootSig) return false;
    const std::string source = BuildAtmosphereCommonShader() + BuildAerialPerspectiveConstantsShader(tuning) +
                               kAerialPerspectivePassShader;
    ComPtr<ID3DBlob> vs = CompileAtmosphereShader(source, "VSMain", "vs_5_0", true);
    ComPtr<ID3DBlob> ps = CompileAtmosphereShader(source, "AerialPS", "ps_5_0", true);
    if (!vs || !ps) return false;
    impl->pso = CreateAtmosphereFullscreenPso(native, impl->rootSig.Get(), vs.Get(), ps.Get(), kAerialLutFormat);
    if (!impl->pso) {
        spdlog::error("[aerial_perspective] PSO");
        return false;
    }
    impl_ = std::move(impl);
    return true;
}

bool AerialPerspectiveLut::Ready() const { return impl_ != nullptr; }

void AerialPerspectiveLut::SetEnabled(bool enabled) {
    if (impl_) impl_->enabled = enabled;
}

bool AerialPerspectiveLut::Enabled() const { return impl_ && impl_->enabled; }

bool AerialPerspectiveLut::Update(const rs::rhi::CommandList& commandList, const AtmosphereLuts& luts,
                                  const rs::core::Vec3& sunDirectionToSun,
                                  const atmosphere::AerialCameraFrame& camera, float observerHeightKm) {
    if (!Enabled() || !commandList.Valid() || !luts.Ready()) return false;
    Impl& impl = *impl_;
    ID3D12GraphicsCommandList* list = commandList.Native();
    ID3D12DescriptorHeap* heaps[] = {luts.SrvHeapRef().Native()};
    list->SetDescriptorHeaps(1, heaps);

    const rs::core::Vec3 sun = sunDirectionToSun.Normalized();
    AerialShaderConstants constants{camera, {sun.x, sun.y, sun.z}, 0.0f};
    constants.camera.padding = observerHeightKm;
    DrawAtmosphereLut(commandList, impl.lut, impl.rootSig.Get(), impl.pso.Get(), luts.SrvTableGpuHandle(), &constants,
                      kAerialPassConstantDwords);
    impl.debug.Step(impl.device, commandList, impl.lut,
                    AerialDebugLayout{impl.sliceCount, impl.tileSize, impl.maxDistanceKm, impl.aerosolOpticalDepth,
                                      observerHeightKm},
                    camera, sun);
    return true;
}

std::uint64_t AerialPerspectiveLut::SrvGpuHandle() const { return impl_ ? impl_->lut.SrvGpuHandle() : 0; }
std::uint32_t AerialPerspectiveLut::SrvIndex() const { return impl_ ? impl_->srvIndex : 0; }
int AerialPerspectiveLut::SliceCount() const { return impl_ ? impl_->sliceCount : 0; }
int AerialPerspectiveLut::TileSize() const { return impl_ ? impl_->tileSize : 0; }
float AerialPerspectiveLut::MaxDistanceKm() const { return impl_ ? impl_->maxDistanceKm : 0.0f; }

} // namespace rs::render
