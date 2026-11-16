#include "TonemapPass.h"

#include <cstddef>
#include <cstdint>

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <mutex>

#include <spdlog/spdlog.h>

#include "../resource/ShaderCompileFxc.h"
#include "PostProcessFormats.h"
#include "TonemapShader.h"

using Microsoft::WRL::ComPtr;

namespace rs::render {

namespace {

// ルート定数: exposure・mode・入力の大きさ・出力矩形の8 DWORD。
constexpr UINT kRootParamSrvTable = 0;
constexpr UINT kRootParamConstants = 1;
struct TonemapShaderConstants {
    float exposure;
    std::uint32_t mode;
    float srcSize[2];
    float outRect[4]; // 幅が0以下なら出力先の全体
};
constexpr UINT kConstantDwordCount = sizeof(TonemapShaderConstants) / sizeof(std::uint32_t);
static_assert(sizeof(TonemapShaderConstants) == 32 && offsetof(TonemapShaderConstants, outRect) == 16,
              "TonemapConstants の配置(float4 は16バイト境界)と一致させる");

} // namespace

struct TonemapPass::Impl {
    ComPtr<ID3D12RootSignature> rootSig;
    ComPtr<ID3D12PipelineState> pso;
    // 設定変更はメインスレッド、読み取りはサムネイルのワーカーからも来る。
    mutable std::mutex paramsMutex;
    TonemapParams params;
};

TonemapPass::TonemapPass() = default;
TonemapPass::~TonemapPass() = default;

bool TonemapPass::Init(const rs::rhi::Device& device, const TonemapParams& params) {
    impl_.reset();
    if (!device.Valid()) return false;
    ID3D12Device* native = device.Native();
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    impl->params = params;

    // t0 = HDRシーン(Loadで読むのでサンプラ不要)、b0 = 露出+モード。
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
    rootParams[kRootParamConstants].Constants.RegisterSpace = 0;
    rootParams[kRootParamConstants].Constants.Num32BitValues = kConstantDwordCount;
    rootParams[kRootParamConstants].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = 2;
    rootDesc.pParameters = rootParams;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> signature, error;
    if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error))) {
        if (error) spdlog::error("[tonemap] rootsig: {}", static_cast<const char*>(error->GetBufferPointer()));
        return false;
    }
    if (FAILED(native->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                           IID_PPV_ARGS(&impl->rootSig))))
        return false;

    UINT flags = 0;
#if defined(_DEBUG)
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    const size_t shaderSize = kTonemapShader.size();
    ComPtr<ID3DBlob> vs = CompileFxcCached(kTonemapShader.data(), shaderSize, "tonemap.hlsl", nullptr, "VSMain",
                                           "vs_5_0", flags, "tonemap");
    ComPtr<ID3DBlob> ps = CompileFxcCached(kTonemapShader.data(), shaderSize, "tonemap.hlsl", nullptr, "PSMain",
                                           "ps_5_0", flags, "tonemap");
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
        spdlog::error("[tonemap] PSO");
        return false;
    }
    impl_ = std::move(impl);
    return true;
}

bool TonemapPass::Ready() const { return impl_ && impl_->pso; }

void TonemapPass::SetParams(const TonemapParams& params) {
    if (!impl_) return;
    const std::scoped_lock lock(impl_->paramsMutex);
    impl_->params = params;
}

TonemapParams TonemapPass::Params() const {
    if (!impl_) return TonemapParams{};
    const std::scoped_lock lock(impl_->paramsMutex);
    return impl_->params;
}

void TonemapPass::Record(const rs::rhi::CommandList& commandList, const SceneHdrTarget& source,
                         rs::rhi::CpuDescriptor outputRtv) const {
    Record(commandList, source, outputRtv, Params());
}

void TonemapPass::Record(const rs::rhi::CommandList& commandList, const SceneHdrTarget& source,
                         rs::rhi::CpuDescriptor outputRtv, const TonemapParams& params,
                         const OutputRect* outRect) const {
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

    TonemapShaderConstants constants{params.exposure, static_cast<std::uint32_t>(params.mode),
                                     {static_cast<float>(source.Width()), static_cast<float>(source.Height())},
                                     {0.0f, 0.0f, 0.0f, 0.0f}};
    if (outRect != nullptr) {
        constants.outRect[0] = outRect->x;
        constants.outRect[1] = outRect->y;
        constants.outRect[2] = outRect->width;
        constants.outRect[3] = outRect->height;
    }
    list->SetGraphicsRoot32BitConstants(kRootParamConstants, kConstantDwordCount, &constants, 0);

    list->DrawInstanced(3, 1, 0, 0);
}

} // namespace rs::render
