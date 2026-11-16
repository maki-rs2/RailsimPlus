#include "AerialPerspectiveDebug.h"

#include "../../platform/EnvironmentVariable.h"
#include <d3d12.h>
#include <wrl/client.h>

#include <DirectXPackedVector.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <numbers>
#include <vector>

#include <spdlog/spdlog.h>

#include "AtmosphereParams.h"

using Microsoft::WRL::ComPtr;

namespace rs::render {

namespace {

// LUTを読み戻すフレーム(LUTを描いた回の数)と、取り出すまでに待つ回数(記録したコマンドが終わるまでの余裕)。
constexpr int kRecordFrame = 6;
constexpr int kReadbackDelayPolls = 4;
// RGBA16F の1画素のバイト数。
constexpr std::uint32_t kBytesPerPixel = 8;

struct SamplePixel {
    int x = 0;
    int y = 0;
    double elevationDeg = 0.0;
};

struct Rgba {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;
};

}  // namespace

struct AerialPerspectiveDebug::Impl {
    ComPtr<ID3D12Resource> readback;
    std::uint32_t rowPitch = 0;
    int framesSeen = 0;
    int pollsLeft = 0;
    AerialDebugLayout layout;
    atmosphere::AerialCameraFrame camera;
    rs::core::Vec3 sunDirectionToSun;

    Rgba Read(const std::uint8_t* mapped, int slice, int x, int y) const;
    SamplePixel FindPixel(bool horizontal) const;
    void Log() const;
};

Rgba AerialPerspectiveDebug::Impl::Read(const std::uint8_t* mapped, int slice, int x, int y) const {
    const std::uint8_t* row = mapped + static_cast<std::size_t>(y) * rowPitch;
    const DirectX::PackedVector::HALF* texel = reinterpret_cast<const DirectX::PackedVector::HALF*>(
        row + static_cast<std::size_t>(slice * layout.tileSize + x) * kBytesPerPixel);
    return {DirectX::PackedVector::XMConvertHalfToFloat(texel[0]),
            DirectX::PackedVector::XMConvertHalfToFloat(texel[1]),
            DirectX::PackedVector::XMConvertHalfToFloat(texel[2]),
            DirectX::PackedVector::XMConvertHalfToFloat(texel[3])};
}

// タイルの中から、視線が水平に近い画素(horizontal=true)か、最も上を向く画素を選ぶ。
SamplePixel AerialPerspectiveDebug::Impl::FindPixel(bool horizontal) const {
    SamplePixel best;
    double bestScore = -1.0e30;
    for (int y = 0; y < layout.tileSize; ++y) {
        for (int x = 0; x < layout.tileSize; ++x) {
            const std::array<double, 3> dir = atmosphere::AerialViewDirection(
                camera, atmosphere::AerialUnitFromTexelIndex(x, layout.tileSize),
                atmosphere::AerialUnitFromTexelIndex(y, layout.tileSize));
            // 水平は、地面で打ち切られない(水平以上の)向きのうち最も低い画素を優先する。無ければ最も水平に近い下向き。
            const double score = horizontal ? (dir[1] >= 0.0 ? -dir[1] : -1000.0 + dir[1]) : dir[1];
            if (score > bestScore) {
                bestScore = score;
                best = {x, y, std::asin(std::clamp(dir[1], -1.0, 1.0)) * 180.0 / std::numbers::pi};
            }
        }
    }
    return best;
}

void AerialPerspectiveDebug::Impl::Log() const {
    std::uint8_t* mapped = nullptr;
    const D3D12_RANGE range = {0, static_cast<SIZE_T>(rowPitch) * layout.tileSize};
    if (FAILED(readback->Map(0, &range, reinterpret_cast<void**>(&mapped)))) {
        spdlog::error("[aerial_perspective] 読み戻しバッファのMapに失敗しました");
        return;
    }

    const rs::core::Vec3 sun = sunDirectionToSun.Normalized();
    spdlog::info("[aplut] sun elevation={:.1f}deg slices={} tile={} maxDistance={}km", atmosphere::DirectionElevationDeg(sun),
                 layout.sliceCount, layout.tileSize, layout.maxDistanceKm);
    const SamplePixel horizontal = FindPixel(true);
    const SamplePixel upward = FindPixel(false);
    const std::array<const char*, 2> names = {"horizontal", "upward"};
    const std::array<SamplePixel, 2> pixels = {horizontal, upward};
    const std::array<int, 4> slices = {0, layout.sliceCount / 4, layout.sliceCount / 2, layout.sliceCount - 1};
    for (int slice : slices) {
        const double unit = atmosphere::AerialUnitFromTexelIndex(slice, layout.sliceCount);
        const double distanceKm = atmosphere::AerialDistanceKmFromUnit(unit, layout.maxDistanceKm);
        for (int i = 0; i < 2; ++i) {
            const Rgba v = Read(mapped, slice, pixels[i].x, pixels[i].y);
            spdlog::info("[aplut] slice={:2d} dist={:7.3f}km {:10s} elev={:6.2f}deg rgb=({:.5f},{:.5f},{:.5f}) a={:.5f}", slice,
                         distanceKm, names[i], pixels[i].elevationDeg, v.r, v.g, v.b, v.a);
        }
    }

    // 最大距離の a を、CPUの基準実装(RGBの平均)と比べる。水平は地面で打ち切られない向きを選んでいるので比べられる。
    const int last = layout.sliceCount - 1;
    for (int i = 0; i < 2; ++i) {
        const Rgba farValue = Read(mapped, last, pixels[i].x, pixels[i].y);
        const std::array<double, 3> dir = atmosphere::AerialViewDirection(
            camera, atmosphere::AerialUnitFromTexelIndex(pixels[i].x, layout.tileSize),
            atmosphere::AerialUnitFromTexelIndex(pixels[i].y, layout.tileSize));
        const std::array<double, 3> cpu = atmosphere::AerialTransmittance(
            atmosphere::kBottomRadiusKm + layout.observerHeightKm, dir[1], layout.maxDistanceKm,
            layout.aerosolOpticalDepth);
        const double cpuMean = (cpu[0] + cpu[1] + cpu[2]) / 3.0;
        spdlog::info("[aplut] {} far: gpu a={:.5f} cpu mean={:.5f} relative error={:.2f}% b/r of rgb={:.3f}", names[i], farValue.a,
                     cpuMean, std::abs(farValue.a - cpuMean) / cpuMean * 100.0, farValue.r > 0.0f ? farValue.b / farValue.r : 0.0f);
    }

    const D3D12_RANGE written = {0, 0};
    readback->Unmap(0, &written);
}

AerialPerspectiveDebug::AerialPerspectiveDebug() = default;
AerialPerspectiveDebug::~AerialPerspectiveDebug() = default;

bool AerialPerspectiveDebug::Enabled() {
    static const bool kEnabled = []() {
        const char* value = rs::platform::ReadEnvironmentVariable("RS_DEBUG_APLUT");
        return value != nullptr && value[0] == '1';
    }();
    return kEnabled;
}

void AerialPerspectiveDebug::Step(const rs::rhi::Device& device, const rs::rhi::CommandList& commandList,
                                  SceneHdrTarget& lut, const AerialDebugLayout& layout,
                                  const atmosphere::AerialCameraFrame& camera,
                                  const rs::core::Vec3& sunDirectionToSun) {
    if (!Enabled() || !device.Valid() || !commandList.Valid()) return;
    if (!impl_) impl_ = std::make_unique<Impl>();
    Impl& impl = *impl_;
    ++impl.framesSeen;

    if (impl.pollsLeft > 0 && --impl.pollsLeft == 0) impl.Log();
    if (impl.framesSeen != kRecordFrame) return;

    // 行の間隔はコピーの配置の単位(256バイト)の倍数にそろえる。
    const std::uint32_t rowBytes = static_cast<std::uint32_t>(layout.sliceCount * layout.tileSize) * kBytesPerPixel;
    impl.rowPitch = (rowBytes + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) / D3D12_TEXTURE_DATA_PITCH_ALIGNMENT *
                    D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = static_cast<UINT64>(impl.rowPitch) * layout.tileSize;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    impl.readback.Reset();
    if (FAILED(device.Native()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                        IID_PPV_ARGS(&impl.readback))))
        return;
    if (!lut.RecordCopyToBuffer(commandList, impl.readback.Get(), impl.rowPitch)) return;
    impl.layout = layout;
    impl.camera = camera;
    impl.sunDirectionToSun = sunDirectionToSun;
    impl.pollsLeft = kReadbackDelayPolls;
}

}  // namespace rs::render
