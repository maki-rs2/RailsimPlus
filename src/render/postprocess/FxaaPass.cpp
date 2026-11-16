#include "FxaaPass.h"

#include <cstddef>
#include <cstdint>

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <spdlog/spdlog.h>

#include "../resource/ShaderCompileFxc.h"
#include "FxaaShader.h"
#include "PostProcessFormats.h"

using Microsoft::WRL::ComPtr;

namespace rs::render {

namespace {

// ルート署名: [0]=入力のSRV(1枠)、[1]=定数 2 DWORD(1/画面の大きさ)、静的サンプラ s0(リニア・クランプ)。
constexpr UINT kRootParamSrvTable = 0;
constexpr UINT kRootParamConstants = 1;
struct FxaaShaderConstants {
    float invSize[2];
    float outOrigin[2];
    float outSize[2]; // 幅が0以下なら出力先の全体
};
constexpr UINT kConstantDwordCount = sizeof(FxaaShaderConstants) / sizeof(std::uint32_t);
static_assert(sizeof(FxaaShaderConstants) == 24 && offsetof(FxaaShaderConstants, outSize) == 16,
              "FxaaConstants の float2 の並びと一致させる");

} // namespace

struct FxaaPass::Impl {
    ComPtr<ID3D12RootSignature> rootSig;
    ComPtr<ID3D12PipelineState> pso;
};

FxaaPass::FxaaPass() = default;
FxaaPass::~FxaaPass() = default;

bool FxaaPass::Init(const rs::rhi::Device& device) {
    impl_.reset();
    if (!device.Valid()) return false;
    ID3D12Device* native = device.Native();
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();

    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;
    range.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER rootParams[2] = {};
    rootParams[kRootParamSrvTable].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[kRootParamSrvTable].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[kRootParamSrvTable].DescriptorTable.pDescriptorRanges = &range;
    rootParams[kRootParamSrvTable].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[kRootParamConstants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[kRootParamConstants].Constants.ShaderRegister = 0;
    rootParams[kRootParamConstants].Constants.Num32BitValues = kConstantDwordCount;
    rootParams[kRootParamConstants].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = 2;
    rootDesc.pParameters = rootParams;
    rootDesc.NumStaticSamplers = 1;
    rootDesc.pStaticSamplers = &sampler;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> signature, error;
    if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error))) {
        if (error) spdlog::error("[fxaa] rootsig: {}", static_cast<const char*>(error->GetBufferPointer()));
        return false;
    }
    if (FAILED(native->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                           IID_PPV_ARGS(&impl->rootSig))))
        return false;

    UINT flags = 0;
#if defined(_DEBUG)
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    const size_t shaderSize = kFxaaShader.size();
    ComPtr<ID3DBlob> vs = CompileFxcCached(kFxaaShader.data(), shaderSize, "fxaa.hlsl", nullptr, "VSMain", "vs_5_0", flags,
                                           "fxaa");
    ComPtr<ID3DBlob> ps = CompileFxcCached(kFxaaShader.data(), shaderSize, "fxaa.hlsl", nullptr, "PSMain", "ps_5_0", flags,
                                           "fxaa");
    if (!vs || !ps) return false;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
    pd.pRootSignature = impl->rootSig.Get();
    pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.DepthStencilState.DepthEnable = FALSE;
    pd.DepthStencilState.StencilEnable = FALSE;
    pd.SampleMask = UINT_MAX;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = kTonemapOutputFormat;
    pd.SampleDesc.Count = 1;
    if (FAILED(native->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&impl->pso)))) {
        spdlog::error("[fxaa] PSO");
        return false;
    }
    impl_ = std::move(impl);
    return true;
}

bool FxaaPass::Ready() const { return impl_ && impl_->pso; }

void FxaaPass::Record(const rs::rhi::CommandList& commandList, const SceneHdrTarget& source,
                      rs::rhi::CpuDescriptor outputRtv, const OutputRect* outRect) const {
    if (!Ready() || !commandList.Valid() || !source.Ready() || !outputRtv.Valid()) return;
    ID3D12GraphicsCommandList* list = commandList.Native();

    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = {outputRtv.ptr};
    list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    ID3D12DescriptorHeap* heaps[] = {source.SrvHeapRef().Native()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetGraphicsRootSignature(impl_->rootSig.Get());
    list->SetPipelineState(impl_->pso.Get());
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->IASetVertexBuffers(0, 0, nullptr);
    list->IASetIndexBuffer(nullptr);

    D3D12_GPU_DESCRIPTOR_HANDLE table = {source.SrvGpuHandle()};
    list->SetGraphicsRootDescriptorTable(kRootParamSrvTable, table);

    FxaaShaderConstants constants{{1.0f / static_cast<float>(source.Width()), 1.0f / static_cast<float>(source.Height())},
                                  {0.0f, 0.0f},
                                  {0.0f, 0.0f}};
    if (outRect != nullptr) {
        constants.outOrigin[0] = outRect->x;
        constants.outOrigin[1] = outRect->y;
        constants.outSize[0] = outRect->width;
        constants.outSize[1] = outRect->height;
    }
    list->SetGraphicsRoot32BitConstants(kRootParamConstants, kConstantDwordCount, &constants, 0);

    list->DrawInstanced(3, 1, 0, 0);
}

} // namespace rs::render
