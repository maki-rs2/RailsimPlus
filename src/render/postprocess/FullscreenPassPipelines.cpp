#include "FullscreenPassPipelines.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "../resource/DxcShaderCompiler.h"
#include "PostProcessFormats.h"

using Microsoft::WRL::ComPtr;

namespace rs::render {

namespace {

bool ToDxgiFormat(HdrTargetFormat format, DXGI_FORMAT& out) {
    switch (format) {
    case HdrTargetFormat::Half16: out = kSceneHdrFormat; return true;
    case HdrTargetFormat::Rg16Float: out = DXGI_FORMAT_R16G16_FLOAT; return true;
    case HdrTargetFormat::R32Float: out = DXGI_FORMAT_R32_FLOAT; return true;
    default: return false;
    }
}

} // namespace

struct FullscreenPassPipelines::Impl {
    ID3D12Device* device = nullptr;
    rs::rhi::SrvHeap srvHeap;
    std::string source;
    std::string tag;
    std::uint32_t constantDwordCount = 0;
    DxcShaderCompiler dxc;
    ComPtr<IDxcBlob> vs;
    ComPtr<ID3D12RootSignature> rootSig;
    std::vector<ComPtr<ID3D12PipelineState>> psos;
};

FullscreenPassPipelines::FullscreenPassPipelines() = default;
FullscreenPassPipelines::~FullscreenPassPipelines() = default;

bool FullscreenPassPipelines::Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap,
                                   std::string_view shaderSource, const char* tag,
                                   std::uint32_t constantDwordCount) {
    impl_.reset();
    if (!device.Valid() || !srvHeap.Valid()) return false;
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    impl->device = device.Native();
    impl->srvHeap = srvHeap;
    impl->source = std::string(shaderSource);
    impl->tag = tag;
    impl->constantDwordCount = constantDwordCount;

    // ルート署名: 32bit 定数 b0 だけ。SRV は ResourceDescriptorHeap[] から番号で引き、画素は Load で読む。
    D3D12_ROOT_PARAMETER param = {};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    param.Constants.ShaderRegister = 0;
    param.Constants.Num32BitValues = constantDwordCount;
    param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = 1;
    rootDesc.pParameters = &param;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
    ComPtr<ID3DBlob> signature, error;
    if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error))) {
        if (error) spdlog::error("[{}] rootsig: {}", impl->tag, static_cast<const char*>(error->GetBufferPointer()));
        return false;
    }
    if (FAILED(impl->device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                                 IID_PPV_ARGS(&impl->rootSig))))
        return false;

    if (!impl->dxc.Init() || !impl->dxc.Compile(impl->source, L"VSMain", L"vs_6_6", {}, impl->tag.c_str(), impl->vs))
        return false;
    impl_ = std::move(impl);
    return true;
}

bool FullscreenPassPipelines::AddPixelShader(const wchar_t* entry, HdrTargetFormat format) {
    if (!impl_) return false;
    Impl& impl = *impl_;
    DXGI_FORMAT dxgiFormat = DXGI_FORMAT_UNKNOWN;
    if (!ToDxgiFormat(format, dxgiFormat)) return false;
    ComPtr<IDxcBlob> ps;
    if (!impl.dxc.Compile(impl.source, entry, L"ps_6_6", {}, impl.tag.c_str(), ps)) return false;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
    pd.pRootSignature = impl.rootSig.Get();
    pd.VS = {impl.vs->GetBufferPointer(), impl.vs->GetBufferSize()};
    pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.DepthStencilState.DepthEnable = FALSE;
    pd.SampleMask = UINT_MAX;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = dxgiFormat;
    pd.SampleDesc.Count = 1;
    ComPtr<ID3D12PipelineState> pso;
    if (FAILED(impl.device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso)))) {
        spdlog::error("[{}] PSO", impl.tag);
        return false;
    }
    impl.psos.push_back(std::move(pso));
    return true;
}

void FullscreenPassPipelines::Bind(const rs::rhi::CommandList& commandList, const void* constants) {
    if (!impl_) return;
    ID3D12GraphicsCommandList* list = commandList.Native();
    ID3D12DescriptorHeap* heaps[] = {impl_->srvHeap.Native()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetGraphicsRootSignature(impl_->rootSig.Get());
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->IASetVertexBuffers(0, 0, nullptr);
    list->IASetIndexBuffer(nullptr);
    list->SetGraphicsRoot32BitConstants(0, impl_->constantDwordCount, constants, 0);
}

void FullscreenPassPipelines::Draw(const rs::rhi::CommandList& commandList, SceneHdrTarget& target,
                                   std::size_t pixelShaderIndex) {
    if (!impl_ || pixelShaderIndex >= impl_->psos.size()) return;
    ID3D12GraphicsCommandList* list = commandList.Native();
    target.BeginScene(commandList, rs::rhi::CpuDescriptor{});
    const D3D12_VIEWPORT viewport = {0.0f, 0.0f, static_cast<float>(target.Width()), static_cast<float>(target.Height()),
                                     0.0f, 1.0f};
    const D3D12_RECT scissor = {0, 0, static_cast<LONG>(target.Width()), static_cast<LONG>(target.Height())};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
    list->SetPipelineState(impl_->psos[pixelShaderIndex].Get());
    list->DrawInstanced(3, 1, 0, 0);
}

} // namespace rs::render
