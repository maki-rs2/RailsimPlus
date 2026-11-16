#include "ComputeSelfTest.h"

#include <DirectXPackedVector.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <cmath>
#include <cstring>

#include <spdlog/spdlog.h>

#include "../pipeline/EnvFlag.h"
#include "ComputePipeline.h"
#include "VolumeTexture.h"

using Microsoft::WRL::ComPtr;

namespace rs::render {

namespace {

// 幅・高さ・奥行きをそろえず、軸の取り違えも見つかるようにする。
constexpr std::uint32_t kWidth = 7;
constexpr std::uint32_t kHeight = 5;
constexpr std::uint32_t kDepth = 11;
constexpr std::uint32_t kThreadGroupSize = 4;
constexpr int kReadbackDelayFrames = 4; // 写してから取り出すまでのフレーム数(GPUの完了待ちの代わり)
constexpr float kTolerance = 1.0e-3f;

struct SelfTestConstants {
    std::uint32_t inputIdx;
    std::uint32_t outputIdx;
};
constexpr std::uint32_t kConstantDwordCount = sizeof(SelfTestConstants) / sizeof(std::uint32_t);

constexpr const char* kSelfTestShader = R"HLSL(
cbuffer Constants : register(b0) {
    uint gInputIdx;
    uint gOutputIdx;
};

[numthreads(4, 4, 4)]
void CSWrite(uint3 id : SV_DispatchThreadID) {
    RWTexture3D<float4> output = ResourceDescriptorHeap[gOutputIdx];
    uint width, height, depth;
    output.GetDimensions(width, height, depth);
    if (any(id >= uint3(width, height, depth))) return;
    output[id] = float4(id.x, id.y, id.z, 1.0);
}

[numthreads(4, 4, 4)]
void CSDouble(uint3 id : SV_DispatchThreadID) {
    Texture3D<float4> input = ResourceDescriptorHeap[gInputIdx];
    RWTexture3D<float4> output = ResourceDescriptorHeap[gOutputIdx];
    uint width, height, depth;
    output.GetDimensions(width, height, depth);
    if (any(id >= uint3(width, height, depth))) return;
    output[id] = input.Load(int4(id, 0)) * 2.0;
}
)HLSL";

constexpr std::uint32_t GroupCount(std::uint32_t size) { return (size + kThreadGroupSize - 1) / kThreadGroupSize; }

bool NearlyEqual(float a, float b) { return std::fabs(a - b) <= kTolerance; }

} // namespace

struct ComputeSelfTest::Impl {
    rs::rhi::SrvHeap srvHeap;
    ComputePipeline writePipeline;
    ComputePipeline doublePipeline;
    VolumeTexture first;
    VolumeTexture second;
    ComPtr<ID3D12Resource> readback;
    bool recorded = false;
    bool finished = false;
    int framesLeft = 0;

    bool Record(const rs::rhi::CommandList& commandList) {
        ComPtr<ID3D12Device> device;
        if (FAILED(commandList.Native()->GetDevice(IID_PPV_ARGS(&device)))) return false;
        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = second.ReadbackByteSize();
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                                   nullptr, IID_PPV_ARGS(&readback))))
            return false;

        const std::uint32_t groupsX = GroupCount(kWidth);
        const std::uint32_t groupsY = GroupCount(kHeight);
        const std::uint32_t groupsZ = GroupCount(kDepth);

        first.Transition(commandList, VolumeTextureState::UnorderedAccess);
        const SelfTestConstants writeConstants = {0, first.UavIndex()};
        writePipeline.Bind(commandList, srvHeap);
        writePipeline.SetConstants(commandList, &writeConstants);
        writePipeline.Dispatch(commandList, groupsX, groupsY, groupsZ);

        first.Transition(commandList, VolumeTextureState::ShaderResource);
        second.Transition(commandList, VolumeTextureState::UnorderedAccess);
        const SelfTestConstants doubleConstants = {first.SrvIndex(), second.UavIndex()};
        doublePipeline.Bind(commandList, srvHeap);
        doublePipeline.SetConstants(commandList, &doubleConstants);
        doublePipeline.Dispatch(commandList, groupsX, groupsY, groupsZ);
        second.UavBarrier(commandList);

        return second.RecordCopyToBuffer(commandList, readback.Get());
    }

    void Verify() {
        void* mapped = nullptr;
        const D3D12_RANGE readRange = {0, static_cast<SIZE_T>(second.ReadbackByteSize())};
        if (FAILED(readback->Map(0, &readRange, &mapped)) || !mapped) {
            spdlog::error("[compute] 読み戻しに失敗しました");
            return;
        }
        const std::uint32_t rowPitch = second.ReadbackRowPitch();
        std::uint32_t mismatches = 0;
        for (std::uint32_t z = 0; z < kDepth; ++z) {
            for (std::uint32_t y = 0; y < kHeight; ++y) {
                const std::uint8_t* base = static_cast<const std::uint8_t*>(mapped) +
                                           (static_cast<size_t>(z) * kHeight + y) * rowPitch;
                const std::uint16_t* row = reinterpret_cast<const std::uint16_t*>(base);
                for (std::uint32_t x = 0; x < kWidth; ++x) {
                    const std::uint16_t* texel = row + static_cast<size_t>(x) * 4;
                    const bool ok = NearlyEqual(DirectX::PackedVector::XMConvertHalfToFloat(texel[0]), x * 2.0f) &&
                                    NearlyEqual(DirectX::PackedVector::XMConvertHalfToFloat(texel[1]), y * 2.0f) &&
                                    NearlyEqual(DirectX::PackedVector::XMConvertHalfToFloat(texel[2]), z * 2.0f) &&
                                    NearlyEqual(DirectX::PackedVector::XMConvertHalfToFloat(texel[3]), 2.0f);
                    if (!ok) ++mismatches;
                }
            }
        }
        const D3D12_RANGE written = {0, 0};
        readback->Unmap(0, &written);
        const std::uint32_t total = kWidth * kHeight * kDepth;
        if (mismatches == 0) spdlog::info("[compute] 確認OK {}テクセル", total);
        else spdlog::error("[compute] 確認NG {}/{}テクセルが不一致", mismatches, total);
    }
};

ComputeSelfTest::ComputeSelfTest() = default;
ComputeSelfTest::~ComputeSelfTest() = default;

bool ComputeSelfTest::Enabled() {
    static const bool kEnabled = ReadEnvFlag("RS_DEBUG_COMPUTE", false);
    return kEnabled;
}

void ComputeSelfTest::Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap,
                           std::uint32_t firstSrvIndex) {
    if (!Enabled()) return;
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    impl->srvHeap = srvHeap;
    if (!impl->writePipeline.Init(device, kSelfTestShader, L"CSWrite", kConstantDwordCount, "compute") ||
        !impl->doublePipeline.Init(device, kSelfTestShader, L"CSDouble", kConstantDwordCount, "compute") ||
        !impl->first.Init(device, srvHeap, firstSrvIndex) ||
        !impl->second.Init(device, srvHeap, firstSrvIndex + VolumeTexture::kSrvCount) ||
        !impl->first.Resize(kWidth, kHeight, kDepth) || !impl->second.Resize(kWidth, kHeight, kDepth)) {
        spdlog::error("[compute] 確認の準備に失敗しました");
        return;
    }
    impl_ = std::move(impl);
}

void ComputeSelfTest::Step(const rs::rhi::CommandList& commandList) {
    if (!impl_ || !commandList.Valid() || impl_->finished) return;
    Impl& impl = *impl_;
    if (!impl.recorded) {
        impl.recorded = true;
        if (!impl.Record(commandList)) {
            spdlog::error("[compute] 記録に失敗しました");
            impl.finished = true;
            return;
        }
        impl.framesLeft = kReadbackDelayFrames;
        return;
    }
    if (--impl.framesLeft > 0) return;
    impl.Verify();
    impl.finished = true;
}

} // namespace rs::render
