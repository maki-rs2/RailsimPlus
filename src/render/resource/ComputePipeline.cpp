#include "ComputePipeline.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <spdlog/spdlog.h>

#include "DxcShaderCompiler.h"
#include "BindlessDescriptors.inl"

using Microsoft::WRL::ComPtr;

namespace rs::render {

struct ComputePipeline::Impl {
    ComPtr<ID3D12RootSignature> rootSig;
    ComPtr<ID3D12PipelineState> pso;
    std::uint32_t constantDwordCount = 0;
    UINT rootCbvParam = 0;
    UINT bindlessParam = 0;
    bool useRootCbv = false;
};

ComputePipeline::ComputePipeline() = default;
ComputePipeline::~ComputePipeline() = default;

bool ComputePipeline::Init(const rs::rhi::Device& device, const std::string& source, const wchar_t* entry,
                           std::uint32_t constantDwordCount, const char* logTag, bool useRootCbv) {
    impl_.reset();
    if (!device.Valid()) return false;
    ID3D12Device* native = device.Native();
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    impl->constantDwordCount = constantDwordCount;

    D3D12_ROOT_PARAMETER params[3] = {};
    UINT paramCount = 0;
    if (constantDwordCount > 0) {
        D3D12_ROOT_PARAMETER& constants = params[paramCount++];
        constants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        constants.Constants.ShaderRegister = 0;
        constants.Constants.Num32BitValues = constantDwordCount;
        constants.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    if (useRootCbv) {
        impl->rootCbvParam = paramCount;
        impl->useRootCbv = true;
        D3D12_ROOT_PARAMETER& cbv = params[paramCount++];
        cbv.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        cbv.Descriptor.ShaderRegister = 1;
        cbv.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }

    const BindlessDescriptorRanges ranges = MakeBindlessDescriptorRanges();
    impl->bindlessParam = paramCount;
    params[paramCount++] = MakeBindlessDescriptorTable(ranges);

    // メッシュのルート署名の s1・s2 と同じ設定にして、同じ HLSL の関数をそのまま使えるようにする。
    D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[0].ShaderRegister = 1;
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    samplers[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    samplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    samplers[1].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[1].ShaderRegister = 2;
    samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = paramCount;
    rootDesc.pParameters = paramCount > 0 ? params : nullptr;
    rootDesc.NumStaticSamplers = 2;
    rootDesc.pStaticSamplers = samplers;
    ComPtr<ID3DBlob> signature, error;
    if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error))) {
        if (error) spdlog::error("[{}] rootsig: {}", logTag, static_cast<const char*>(error->GetBufferPointer()));
        return false;
    }
    if (FAILED(native->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                           IID_PPV_ARGS(&impl->rootSig))))
        return false;

    DxcShaderCompiler dxc;
    ComPtr<IDxcBlob> cs;
    if (!dxc.Init() || !dxc.Compile(source, entry, L"cs_6_0", {}, logTag, cs)) return false;
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = impl->rootSig.Get();
    desc.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};
    if (FAILED(native->CreateComputePipelineState(&desc, IID_PPV_ARGS(&impl->pso)))) {
        spdlog::error("[{}] compute PSO", logTag);
        return false;
    }
    impl_ = std::move(impl);
    return true;
}

bool ComputePipeline::Ready() const { return impl_ != nullptr; }

void ComputePipeline::Bind(const rs::rhi::CommandList& commandList, const rs::rhi::SrvHeap& srvHeap) const {
    if (!impl_ || !commandList.Valid() || !srvHeap.Valid()) return;
    ID3D12GraphicsCommandList* list = commandList.Native();
    ID3D12DescriptorHeap* heaps[] = {srvHeap.Native()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(impl_->rootSig.Get());
    SetComputeBindlessTable(list, impl_->bindlessParam, srvHeap.Native());
    list->SetPipelineState(impl_->pso.Get());
}

void ComputePipeline::SetConstants(const rs::rhi::CommandList& commandList, const void* constants) const {
    if (!impl_ || !commandList.Valid() || impl_->constantDwordCount == 0) return;
    commandList.Native()->SetComputeRoot32BitConstants(0, impl_->constantDwordCount, constants, 0);
}

void ComputePipeline::SetRootCbv(const rs::rhi::CommandList& commandList, std::uint64_t gpuAddress) const {
    if (!impl_ || !commandList.Valid() || !impl_->useRootCbv) return;
    commandList.Native()->SetComputeRootConstantBufferView(impl_->rootCbvParam, gpuAddress);
}

void ComputePipeline::Dispatch(const rs::rhi::CommandList& commandList, std::uint32_t groupsX, std::uint32_t groupsY,
                               std::uint32_t groupsZ) const {
    if (!impl_ || !commandList.Valid()) return;
    commandList.Native()->Dispatch(groupsX, groupsY, groupsZ);
}

} // namespace rs::render
