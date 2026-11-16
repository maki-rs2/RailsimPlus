#include "Overlay2DPass.h"

#include <cstddef>
#include <cstdint>

#include <algorithm>
#include <cstring>

#include <d3dcompiler.h>
#include <spdlog/spdlog.h>

#include "Overlay2DPassSetup.h"

using Microsoft::WRL::ComPtr;

namespace rs::render {

using namespace overlay2d_setup;

namespace {
struct OverlayShaderConstants {
    float screenSize[2];
    float aaSmoothingWidthPx;
    float occludedAlphaScale;
};
constexpr UINT kConstantDwordCount = sizeof(OverlayShaderConstants) / sizeof(std::uint32_t);
static_assert(sizeof(OverlayShaderConstants) == 16 && offsetof(OverlayShaderConstants, aaSmoothingWidthPx) == 8 &&
              offsetof(OverlayShaderConstants, occludedAlphaScale) == 12,
              "OverlayConstants と配置を一致させる");
} // namespace

bool Overlay2DPass::Init(ID3D12Device* device, ID3D12DescriptorHeap* srvHeap, UINT srvStride,
                         UINT glyphSrvIndex, DXGI_FORMAT rtvFormat, const char* fontPath,
                         long fontFaceIndex) {
    if (device == nullptr || srvHeap == nullptr || srvStride == 0 || rtvFormat == DXGI_FORMAT_UNKNOWN) {
        return false;
    }
    settings_ = ParseSettings();
    device_ = device;
    // グリフアトラスの初期化失敗は致命ではない。テキストだけ諦め、図形の描画は続ける。
    glyphAtlas_.Init(device, srvHeap, srvStride, glyphSrvIndex, settings_.textPixelSize,
                     settings_.atlasInitialSize, fontPath, fontFaceIndex);
    if (!CreateRootSignature(device) || !CreatePipelines(device, rtvFormat)) return false;
    ready_ = true;
    return true;
}

bool Overlay2DPass::CreateRootSignature(ID3D12Device* device) {
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER parameters[2] = {};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[0].Constants.ShaderRegister = 0;
    parameters[0].Constants.Num32BitValues = kConstantDwordCount;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    parameters[1].DescriptorTable.pDescriptorRanges = &range;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    D3D12_ROOT_SIGNATURE_DESC description = {};
    description.NumParameters = _countof(parameters);
    description.pParameters = parameters;
    description.NumStaticSamplers = 1;
    description.pStaticSamplers = &sampler;
    description.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> errors;
    if (FAILED(D3D12SerializeRootSignature(&description, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors))) {
        if (errors != nullptr) {
            spdlog::error("[overlay2d] root signature: {}", static_cast<const char*>(errors->GetBufferPointer()));
        }
        return false;
    }
    return SUCCEEDED(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSig_)));
}

bool Overlay2DPass::EnsureUploadBuffer(UINT frameIndex, size_t vertexCount, size_t vertexSize,
                                       std::array<ComPtr<ID3D12Resource>, kUploadRingSize>& buffers,
                                       std::array<size_t, kUploadRingSize>& capacities) {
    const UINT slot = frameIndex % kUploadRingSize;
    const size_t requiredBytes = vertexCount * vertexSize;
    if (buffers[slot] != nullptr && capacities[slot] >= requiredBytes) return true;
    const size_t capacity = std::max(requiredBytes, capacities[slot] * 2);
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC resource = {};
    resource.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource.Width = capacity;
    resource.Height = 1;
    resource.DepthOrArraySize = 1;
    resource.MipLevels = 1;
    resource.SampleDesc.Count = 1;
    resource.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> buffer;
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &resource,
                                                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                IID_PPV_ARGS(&buffer)))) {
        return false;
    }
    buffers[slot] = buffer;
    capacities[slot] = capacity;
    return true;
}

void Overlay2DPass::RecordGeometry(ID3D12GraphicsCommandList* cmdList, ID3D12PipelineState* pso,
                                   float alphaScale, UINT width, UINT height, UINT uploadSlot,
                                   UINT firstVertex, UINT vertexCount) {
    if (vertexCount == 0) return;
    D3D12_VERTEX_BUFFER_VIEW view = {};
    view.BufferLocation = uploadBuffers_[uploadSlot]->GetGPUVirtualAddress() +
                          static_cast<UINT64>(firstVertex) * sizeof(OverlayVertex);
    view.SizeInBytes = vertexCount * sizeof(OverlayVertex);
    view.StrideInBytes = sizeof(OverlayVertex);
    const OverlayShaderConstants constants{{static_cast<float>(width), static_cast<float>(height)},
                                            settings_.aaSmoothingWidthPx, alphaScale};
    cmdList->SetPipelineState(pso);
    cmdList->SetGraphicsRoot32BitConstants(0, kConstantDwordCount, &constants, 0);
    cmdList->IASetVertexBuffers(0, 1, &view);
    cmdList->DrawInstanced(vertexCount, 1, 0, 0);
}

void Overlay2DPass::RecordText(ID3D12GraphicsCommandList* cmdList, ID3D12PipelineState* pso,
                               float alphaScale, UINT width, UINT height, UINT uploadSlot,
                               UINT firstVertex, UINT vertexCount) {
    if (vertexCount == 0) return;
    D3D12_VERTEX_BUFFER_VIEW view = {};
    view.BufferLocation = textUploadBuffers_[uploadSlot]->GetGPUVirtualAddress() +
                          static_cast<UINT64>(firstVertex) * sizeof(OverlayTextVertex);
    view.SizeInBytes = vertexCount * sizeof(OverlayTextVertex);
    view.StrideInBytes = sizeof(OverlayTextVertex);
    const OverlayShaderConstants constants{{static_cast<float>(width), static_cast<float>(height)},
                                            settings_.aaSmoothingWidthPx, alphaScale};
    cmdList->SetPipelineState(pso);
    cmdList->SetGraphicsRoot32BitConstants(0, kConstantDwordCount, &constants, 0);
    cmdList->SetGraphicsRootDescriptorTable(1, glyphAtlas_.GpuHandle());
    cmdList->IASetVertexBuffers(0, 1, &view);
    cmdList->DrawInstanced(vertexCount, 1, 0, 0);
}

void Overlay2DPass::Record(ID3D12GraphicsCommandList* cmdList, D3D12_CPU_DESCRIPTOR_HANDLE rtv,
                           D3D12_CPU_DESCRIPTOR_HANDLE readOnlyDsv, const rs::core::DebugDrawList& list,
                           const rs::core::Mat4& viewProj, UINT width, UINT height, UINT frameIndex) {
    if (!ready_ || cmdList == nullptr || width == 0 || height == 0) return;
    OverlayGeometryConfig config;
    config.width = width;
    config.height = height;
    config.aaSmoothingWidthPx = settings_.aaSmoothingWidthPx;
    config.dashDutyCycle = settings_.dashDuty;
    config.attenuationReferenceDistance = settings_.attenuationReferenceDistance;
    config.attenuationMinScale = settings_.attenuationMinScale;
    OverlayGeometryBuilder::Build(list, viewProj, config, &glyphAtlas_, geometry_);
    // 頂点生成中に初出のグリフがラスタライズされるため、描画コマンドより前にアトラスを更新する。
    glyphAtlas_.UploadDirty(cmdList, frameIndex);

    const size_t shapeDepthCount = geometry_.depthTested.size();
    const size_t shapeTopCount = geometry_.alwaysOnTop.size();
    const size_t shapeCount = shapeDepthCount + shapeTopCount;
    const size_t textDepthCount = geometry_.textDepthTested.size();
    const size_t textTopCount = geometry_.textAlwaysOnTop.size();
    const size_t textCount = textDepthCount + textTopCount;
    if (shapeCount > UINT_MAX || textCount > UINT_MAX) return;
    if (shapeCount == 0 && textCount == 0) return;
    const UINT uploadSlot = frameIndex % kUploadRingSize;
    if (shapeCount != 0 &&
        !EnsureUploadBuffer(frameIndex, shapeCount, sizeof(OverlayVertex), uploadBuffers_, uploadCapacities_)) {
        return;
    }
    if (textCount != 0 &&
        !EnsureUploadBuffer(frameIndex, textCount, sizeof(OverlayTextVertex), textUploadBuffers_,
                            textUploadCapacities_)) {
        return;
    }

    // 深度テスト分と最前面分を1本のバッファへ連結し、描画側は開始頂点で切り分ける。
    void* mapped = nullptr;
    const D3D12_RANGE readRange = {0, 0};
    if (shapeCount != 0) {
        if (FAILED(uploadBuffers_[uploadSlot]->Map(0, &readRange, &mapped))) {
            spdlog::error("[overlay2d] 頂点バッファのMapに失敗しました");
            return;
        }
        std::memcpy(mapped, geometry_.depthTested.data(), shapeDepthCount * sizeof(OverlayVertex));
        std::memcpy(static_cast<OverlayVertex*>(mapped) + shapeDepthCount, geometry_.alwaysOnTop.data(),
                    shapeTopCount * sizeof(OverlayVertex));
        uploadBuffers_[uploadSlot]->Unmap(0, nullptr);
    }
    if (textCount != 0) {
        if (FAILED(textUploadBuffers_[uploadSlot]->Map(0, &readRange, &mapped))) {
            spdlog::error("[overlay2d] 文字頂点バッファのMapに失敗しました");
            return;
        }
        std::memcpy(mapped, geometry_.textDepthTested.data(), textDepthCount * sizeof(OverlayTextVertex));
        std::memcpy(static_cast<OverlayTextVertex*>(mapped) + textDepthCount, geometry_.textAlwaysOnTop.data(),
                    textTopCount * sizeof(OverlayTextVertex));
        textUploadBuffers_[uploadSlot]->Unmap(0, nullptr);
    }

    cmdList->OMSetRenderTargets(1, &rtv, FALSE, readOnlyDsv.ptr != 0 ? &readOnlyDsv : nullptr);
    cmdList->SetGraphicsRootSignature(rootSig_.Get());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const UINT shapeDepth = static_cast<UINT>(shapeDepthCount);
    RecordGeometry(cmdList, visiblePso_.Get(), 1.0f, width, height, uploadSlot, 0, shapeDepth);
    RecordGeometry(cmdList, occludedPso_.Get(), settings_.occludedAlphaScale, width, height, uploadSlot, 0,
                   shapeDepth);
    RecordGeometry(cmdList, alwaysOnTopPso_.Get(), 1.0f, width, height, uploadSlot, shapeDepth,
                   static_cast<UINT>(shapeTopCount));
    if (textCount == 0 || !glyphAtlas_.Ready()) return;
    const UINT textDepth = static_cast<UINT>(textDepthCount);
    RecordText(cmdList, textVisiblePso_.Get(), 1.0f, width, height, uploadSlot, 0, textDepth);
    RecordText(cmdList, textOccludedPso_.Get(), settings_.occludedAlphaScale, width, height, uploadSlot, 0,
               textDepth);
    RecordText(cmdList, textAlwaysOnTopPso_.Get(), 1.0f, width, height, uploadSlot, textDepth,
               static_cast<UINT>(textTopCount));
}
} // namespace rs::render
