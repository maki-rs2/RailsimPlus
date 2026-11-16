#include "VelocityDebug.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <spdlog/spdlog.h>

#include "../pipeline/EnvFlag.h"

using Microsoft::WRL::ComPtr;

namespace rs::render {

namespace {

constexpr std::uint32_t kBytesPerPixel = 4; // R16G16_FLOAT
constexpr int kRecordInterval = 5;          // この間隔のフレームごとに写す(奇数にして、リングの両方の枠を見る)
constexpr int kReadbackDelayPolls = 3;      // 写してから取り出すまでのフレーム数(GPUの完了待ちの代わり)
constexpr float kNonzeroThreshold = 1.0e-6f;

float HalfToFloat(std::uint16_t h) {
    const std::uint32_t sign = (h >> 15) & 1u;
    const std::uint32_t exponent = (h >> 10) & 0x1Fu;
    const std::uint32_t mantissa = h & 0x3FFu;
    float value;
    if (exponent == 0) value = std::ldexp(static_cast<float>(mantissa), -24);
    else if (exponent == 31) value = mantissa ? NAN : INFINITY;
    else value = std::ldexp(static_cast<float>(mantissa | 0x400u), static_cast<int>(exponent) - 25);
    return sign ? -value : value;
}

} // namespace

struct VelocityDebug::Impl {
    ComPtr<ID3D12Resource> readback;
    std::uint32_t rowPitch = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    int framesSeen = 0;
    int recordedFrame = 0;
    int pollsLeft = 0;

    void Log() {
        void* mapped = nullptr;
        const D3D12_RANGE readRange = {0, static_cast<SIZE_T>(rowPitch) * height};
        if (FAILED(readback->Map(0, &readRange, &mapped)) || !mapped) {
            spdlog::error("[velocity_debug] 読み戻しバッファのMapに失敗しました");
            return;
        }
        std::uint64_t nonzero = 0;
        float maxMagnitude = 0.0f;
        std::uint32_t minX = width, minY = height, maxX = 0, maxY = 0;
        for (std::uint32_t y = 0; y < height; ++y) {
            const std::uint16_t* row = reinterpret_cast<const std::uint16_t*>(static_cast<const std::uint8_t*>(mapped) + static_cast<size_t>(y) * rowPitch);
            for (std::uint32_t x = 0; x < width; ++x) {
                const float vx = HalfToFloat(row[size_t{x} * 2]);
                const float vy = HalfToFloat(row[size_t{x} * 2 + 1]);
                const float magnitude = std::sqrt(vx * vx + vy * vy);
                if (!(magnitude > kNonzeroThreshold)) continue;
                ++nonzero;
                maxMagnitude = std::max(maxMagnitude, magnitude);
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
            }
        }
        if (nonzero == 0) {
            spdlog::info("[velocity] frame={} nonzero=0", recordedFrame);
        } else {
            spdlog::info("[velocity] frame={} nonzero={} max={:.5f} bbox=({},{})-({},{})", recordedFrame, nonzero,
                         maxMagnitude, minX, minY, maxX, maxY);
        }
        const D3D12_RANGE written = {0, 0};
        readback->Unmap(0, &written);
    }
};

VelocityDebug::VelocityDebug() = default;
VelocityDebug::~VelocityDebug() = default;

bool VelocityDebug::Enabled() {
    static const bool kEnabled = ReadEnvFlag("RS_DEBUG_VELOCITY", false);
    return kEnabled;
}

void VelocityDebug::Step(const rs::rhi::CommandList& commandList, SceneHdrTarget& velocity) {
    if (!Enabled() || !commandList.Valid() || !velocity.Ready()) return;
    ComPtr<ID3D12Device> device;
    if (FAILED(commandList.Native()->GetDevice(IID_PPV_ARGS(&device)))) return;
    if (!impl_) impl_ = std::make_unique<Impl>();
    Impl& impl = *impl_;
    ++impl.framesSeen;
    if (impl.pollsLeft > 0 && --impl.pollsLeft == 0) impl.Log();
    if (impl.pollsLeft > 0 || impl.framesSeen % kRecordInterval != 0) return;

    const std::uint32_t width = velocity.Width();
    const std::uint32_t height = velocity.Height();
    impl.rowPitch = (width * kBytesPerPixel + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) /
                    D3D12_TEXTURE_DATA_PITCH_ALIGNMENT * D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = static_cast<UINT64>(impl.rowPitch) * height;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    impl.readback.Reset();
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                        IID_PPV_ARGS(&impl.readback))))
        return;
    if (!velocity.RecordCopyToBuffer(commandList, impl.readback.Get(), impl.rowPitch)) return;
    impl.width = width;
    impl.height = height;
    impl.recordedFrame = impl.framesSeen;
    impl.pollsLeft = kReadbackDelayPolls;
}

} // namespace rs::render
