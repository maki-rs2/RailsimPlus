#include "VolumetricFogDebug.h"

#include <DirectXPackedVector.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include <spdlog/spdlog.h>

#include "../pipeline/EnvFlag.h"
#include "../resource/VolumeTexture.h"
#include "../sky/HeightFogMath.h"
#include "VolumetricFogMath.h"

using Microsoft::WRL::ComPtr;

namespace rs::render {

namespace {

constexpr int kRecordInterval = 30;    // この間隔のフレームごとに写す
constexpr int kReadbackDelayPolls = 3; // 写してから取り出すまでのフレーム数(GPUの完了待ちの代わり)
constexpr float kMinDensityForLight = 1.0e-7f;
constexpr float kMonotonicTolerance = 1.0e-3f;

ComPtr<ID3D12Resource> CreateReadback(ID3D12Device* device, std::uint64_t size) {
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> buffer;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                               nullptr, IID_PPV_ARGS(&buffer))))
        return nullptr;
    return buffer;
}

// 読み戻したバッファの1テクセルの1成分。
float ReadTexel(const std::uint8_t* base, std::uint32_t rowPitch, std::uint32_t height, std::uint32_t x,
                std::uint32_t y, std::uint32_t z, std::uint32_t channel) {
    const std::uint8_t* row = base + (static_cast<size_t>(z) * height + y) * rowPitch;
    const std::uint16_t* texel = reinterpret_cast<const std::uint16_t*>(row) + static_cast<size_t>(x) * 4;
    return DirectX::PackedVector::XMConvertHalfToFloat(texel[channel]);
}

} // namespace

struct VolumetricFogDebug::Impl {
    ComPtr<ID3D12Resource> scatterReadback;
    ComPtr<ID3D12Resource> integratedReadback;
    VolumetricFogFrameParams params;
    bool historyUsed = false;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 0;
    std::uint32_t scatterPitch = 0;
    std::uint32_t integratedPitch = 0;
    int framesSeen = 0;
    int recordedFrame = 0;
    int pollsLeft = 0;

    void Log() {
        void* scatterMapped = nullptr;
        void* integratedMapped = nullptr;
        if (FAILED(scatterReadback->Map(0, nullptr, &scatterMapped))) {
            spdlog::error("[volumetric_fog] 散乱読み戻しバッファのMapに失敗しました");
            return;
        }
        if (FAILED(integratedReadback->Map(0, nullptr, &integratedMapped))) {
            spdlog::error("[volumetric_fog] 積分読み戻しバッファのMapに失敗しました");
            const D3D12_RANGE noWrite = {0, 0};
            scatterReadback->Unmap(0, &noWrite);
            return;
        }
        const std::uint8_t* scatter = static_cast<const std::uint8_t*>(scatterMapped);
        const std::uint8_t* integrated = static_cast<const std::uint8_t*>(integratedMapped);

        std::uint64_t nonMonotonic = 0;
        float lightMin = std::numeric_limits<float>::max();
        float lightMax = 0.0f;
        float scatterMax = 0.0f;
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                float previous = 1.0f;
                for (std::uint32_t z = 0; z < depth; ++z) {
                    const float transmittance = ReadTexel(integrated, integratedPitch, height, x, y, z, 3);
                    if (transmittance > previous + kMonotonicTolerance) ++nonMonotonic;
                    previous = transmittance;
                    scatterMax = std::max(scatterMax, ReadTexel(integrated, integratedPitch, height, x, y, z, 1));
                    const float density = ReadTexel(scatter, scatterPitch, height, x, y, z, 3);
                    if (density > kMinDensityForLight) {
                        const float light = ReadTexel(scatter, scatterPitch, height, x, y, z, 1) / density;
                        lightMin = std::min(lightMin, light);
                        lightMax = std::max(lightMax, light);
                    }
                }
            }
        }

        // 画面の中央の列を、最後のスライスの中心まで式で積分した値と比べる。
        const std::uint32_t centerX = width / 2;
        const std::uint32_t centerY = height / 2;
        const double unitX = (centerX + 0.5) / width * 2.0 - 1.0;
        const double unitY = 1.0 - (centerY + 0.5) / height * 2.0;
        double ray[3];
        for (int i = 0; i < 3; ++i) {
            ray[i] = params.camera.forward[i] + params.camera.right[i] * unitX * params.camera.tanHalfFovX +
                     params.camera.up[i] * unitY * params.camera.tanHalfFovY;
        }
        const double rayLength = std::sqrt(ray[0] * ray[0] + ray[1] * ray[1] + ray[2] * ray[2]);
        const double farDepth = volumetric_fog::SliceDepth((depth - 0.5) / depth, params.maxDistance);
        const double expected = height_fog::Transmittance(height_fog::OpticalDepth(
            params.fogDensity, params.fogHeightScale, params.fogBaseHeight, params.cameraPos[1],
            params.cameraPos[1] + ray[1] * farDepth, rayLength * farDepth));
        const float observed = ReadTexel(integrated, integratedPitch, height, centerX, centerY, depth - 1, 3);

        spdlog::info("[volumetric_fog] frame={} grid={}x{}x{} 履歴={} 透過率の逆転={} 中央の透過率={:.5f}(式 {:.5f}) "
                     "霧に当たる光={:.4f}..{:.4f} 散乱光の最大={:.5f}",
                     recordedFrame, width, height, depth, historyUsed, nonMonotonic, observed, expected, lightMin, lightMax,
                     scatterMax);
        const D3D12_RANGE written = {0, 0};
        scatterReadback->Unmap(0, &written);
        integratedReadback->Unmap(0, &written);
    }
};

VolumetricFogDebug::VolumetricFogDebug() = default;
VolumetricFogDebug::~VolumetricFogDebug() = default;

bool VolumetricFogDebug::Enabled() {
    static const bool kEnabled = ReadEnvFlag("RS_DEBUG_VOLUMETRIC_FOG", false);
    return kEnabled;
}

void VolumetricFogDebug::Step(const rs::rhi::CommandList& commandList, VolumetricFogPass& pass,
                              const VolumetricFogFrameParams& params) {
    if (!Enabled() || !commandList.Valid()) return;
    if (!impl_) impl_ = std::make_unique<Impl>();
    Impl& impl = *impl_;
    ++impl.framesSeen;
    if (impl.pollsLeft > 0 && --impl.pollsLeft == 0) impl.Log();
    if (impl.pollsLeft > 0 || impl.framesSeen % kRecordInterval != 0) return;

    ComPtr<ID3D12Device> device;
    if (FAILED(commandList.Native()->GetDevice(IID_PPV_ARGS(&device)))) return;
    VolumeTexture& scatter = pass.Scatter();
    VolumeTexture& integrated = pass.Integrated();
    impl.scatterReadback = CreateReadback(device.Get(), scatter.ReadbackByteSize());
    impl.integratedReadback = CreateReadback(device.Get(), integrated.ReadbackByteSize());
    if (!impl.scatterReadback || !impl.integratedReadback) return;
    if (!scatter.RecordCopyToBuffer(commandList, impl.scatterReadback.Get()) ||
        !integrated.RecordCopyToBuffer(commandList, impl.integratedReadback.Get()))
        return;
    impl.params = params;
    impl.historyUsed = pass.HistoryUsed();
    impl.width = integrated.Width();
    impl.height = integrated.Height();
    impl.depth = integrated.Depth();
    impl.scatterPitch = scatter.ReadbackRowPitch();
    impl.integratedPitch = integrated.ReadbackRowPitch();
    impl.recordedFrame = impl.framesSeen;
    impl.pollsLeft = kReadbackDelayPolls;
}

} // namespace rs::render
