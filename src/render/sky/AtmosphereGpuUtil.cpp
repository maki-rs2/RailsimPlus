#include "AtmosphereGpuUtil.h"

#include <vector>

#include <spdlog/spdlog.h>

#include "../resource/ShaderCompileFxc.h"

using Microsoft::WRL::ComPtr;

namespace rs::render {

ComPtr<ID3D12RootSignature> CreateAtmosphereRootSignature(ID3D12Device* device, UINT srvCount,
                                                          UINT constantDwords, UINT extraSrvCount,
                                                          bool allowInputLayout) {
    std::vector<UINT> extraTables;
    if (extraSrvCount > 0) extraTables.push_back(extraSrvCount);
    return CreateAtmosphereRootSignatureTables(device, srvCount, constantDwords, extraTables, allowInputLayout);
}

ComPtr<ID3D12RootSignature> CreateAtmosphereRootSignatureTables(ID3D12Device* device, UINT srvCount,
                                                                UINT constantDwords,
                                                                const std::vector<UINT>& extraTableSrvCounts,
                                                                bool allowInputLayout) {
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = srvCount;
    range.BaseShaderRegister = 0;
    range.OffsetInDescriptorsFromTableStart = 0;

    // 追加テーブルごとの範囲。レジスタは、先のテーブルの直後から連続して割り当てる。
    std::vector<D3D12_DESCRIPTOR_RANGE> extraRanges(extraTableSrvCounts.size());
    UINT nextRegister = srvCount;
    for (size_t i = 0; i < extraTableSrvCounts.size(); ++i) {
        extraRanges[i] = {};
        extraRanges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        extraRanges[i].NumDescriptors = extraTableSrvCounts[i];
        extraRanges[i].BaseShaderRegister = nextRegister;
        extraRanges[i].OffsetInDescriptorsFromTableStart = 0;
        nextRegister += extraTableSrvCounts[i];
    }

    std::vector<D3D12_ROOT_PARAMETER> params;
    if (srvCount > 0) {
        D3D12_ROOT_PARAMETER param = {};
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        param.DescriptorTable.NumDescriptorRanges = 1;
        param.DescriptorTable.pDescriptorRanges = &range;
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params.push_back(param);
    }
    if (constantDwords > 0) {
        D3D12_ROOT_PARAMETER param = {};
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        param.Constants.ShaderRegister = 0;
        param.Constants.RegisterSpace = 0;
        param.Constants.Num32BitValues = constantDwords;
        param.ShaderVisibility = allowInputLayout ? D3D12_SHADER_VISIBILITY_ALL : D3D12_SHADER_VISIBILITY_PIXEL;
        params.push_back(param);
    }
    for (size_t i = 0; i < extraRanges.size(); ++i) {
        D3D12_ROOT_PARAMETER param = {};
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        param.DescriptorTable.NumDescriptorRanges = 1;
        param.DescriptorTable.pDescriptorRanges = &extraRanges[i];
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params.push_back(param);
    }

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = static_cast<UINT>(params.size());
    desc.pParameters = params.empty() ? nullptr : params.data();
    desc.NumStaticSamplers = 1;
    desc.pStaticSamplers = &sampler;
    desc.Flags = allowInputLayout ? D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
                                  : D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> signature, error;
    if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error))) {
        if (error) spdlog::error("[atmosphere] rootsig: {}", static_cast<const char*>(error->GetBufferPointer()));
        return nullptr;
    }
    ComPtr<ID3D12RootSignature> rootSig;
    if (FAILED(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                           IID_PPV_ARGS(&rootSig))))
        return nullptr;
    return rootSig;
}

ComPtr<ID3DBlob> CompileAtmosphereShader(const std::string& source, const char* entry, const char* target,
                                         bool useMultiScat) {
    UINT flags = 0;
#if defined(_DEBUG)
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    const D3D_SHADER_MACRO defines[] = {{"RS_USE_MULTISCAT", useMultiScat ? "1" : "0"}, {nullptr, nullptr}};
    return CompileFxcCached(source.c_str(), source.size(), "atmosphere.hlsl", defines, entry, target, flags,
                            "atmosphere");
}

ComPtr<ID3D12PipelineState> CreateAtmosphereFullscreenPso(ID3D12Device* device, ID3D12RootSignature* rootSig,
                                                          ID3DBlob* vs, ID3DBlob* ps, DXGI_FORMAT rtvFormat) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
    pd.pRootSignature = rootSig;
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
    pd.RTVFormats[0] = rtvFormat;
    pd.SampleDesc.Count = 1;
    ComPtr<ID3D12PipelineState> pso;
    if (FAILED(device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso)))) return nullptr;
    return pso;
}

void DrawAtmosphereLut(const rs::rhi::CommandList& commandList, SceneHdrTarget& target, void* rootSigNative,
                       void* psoNative, std::uint64_t tableGpuHandle, const void* constants,
                       std::uint32_t constantDwords) {
    ID3D12GraphicsCommandList* list = commandList.Native();
    target.BeginScene(commandList, rs::rhi::CpuDescriptor{});
    const D3D12_VIEWPORT viewport = {0.0f, 0.0f, static_cast<float>(target.Width()),
                                     static_cast<float>(target.Height()), 0.0f, 1.0f};
    const D3D12_RECT scissor = {0, 0, static_cast<LONG>(target.Width()), static_cast<LONG>(target.Height())};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
    list->SetGraphicsRootSignature(static_cast<ID3D12RootSignature*>(rootSigNative));
    list->SetPipelineState(static_cast<ID3D12PipelineState*>(psoNative));
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->IASetVertexBuffers(0, 0, nullptr);
    list->IASetIndexBuffer(nullptr);
    UINT rootIndex = 0;
    if (tableGpuHandle != 0) {
        const D3D12_GPU_DESCRIPTOR_HANDLE table = {tableGpuHandle};
        list->SetGraphicsRootDescriptorTable(rootIndex++, table);
    }
    if (constantDwords > 0) list->SetGraphicsRoot32BitConstants(rootIndex, constantDwords, constants, 0);
    list->DrawInstanced(3, 1, 0, 0);
    target.Transition(commandList, HdrTargetState::ShaderResource);
}

} // namespace rs::render
