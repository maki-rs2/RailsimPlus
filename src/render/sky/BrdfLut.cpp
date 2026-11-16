#include "BrdfLut.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <utility>

#include <spdlog/spdlog.h>

#include "AtmosphereGpuUtil.h"
#include "IblParams.h"
#include "IblShader.h"

using Microsoft::WRL::ComPtr;

namespace rs::render {

namespace {
constexpr DXGI_FORMAT kBrdfLutFormat = DXGI_FORMAT_R16G16_FLOAT;
} // namespace

struct BrdfLut::Impl {
    ComPtr<ID3D12Resource> resource;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    ComPtr<ID3D12RootSignature> rootSig;
    ComPtr<ID3D12PipelineState> pso;
    std::uint64_t srvGpuHandle = 0;
    bool generated = false;
};

BrdfLut::BrdfLut() = default;
BrdfLut::~BrdfLut() = default;

bool BrdfLut::Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap, std::uint32_t srvIndex) {
    impl_.reset();
    if (!device.Valid() || !srvHeap.Valid()) return false;
    ID3D12Device* native = device.Native();
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();

    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = ibl::kIblBrdfLutSize;
    desc.Height = ibl::kIblBrdfLutSize;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = kBrdfLutFormat;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (FAILED(native->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                                               IID_PPV_ARGS(&impl->resource)))) {
        spdlog::error("[brdf_lut] resource");
        return false;
    }
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 1;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    if (FAILED(native->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&impl->rtvHeap)))) return false;
    native->CreateRenderTargetView(impl->resource.Get(), nullptr, impl->rtvHeap->GetCPUDescriptorHandleForHeapStart());

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = kBrdfLutFormat;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    ID3D12DescriptorHeap* srvHeapNative = srvHeap.Native();
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = srvHeapNative->GetCPUDescriptorHandleForHeapStart();
    srvCpu.ptr += static_cast<SIZE_T>(srvIndex) * srvHeap.Stride();
    native->CreateShaderResourceView(impl->resource.Get(), &srvDesc, srvCpu);
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = srvHeapNative->GetGPUDescriptorHandleForHeapStart();
    srvGpu.ptr += static_cast<UINT64>(srvIndex) * srvHeap.Stride();
    impl->srvGpuHandle = srvGpu.ptr;

    impl->rootSig = CreateAtmosphereRootSignature(native, 0, 0);
    const std::string source = BuildIblBrdfShader();
    ComPtr<ID3DBlob> vs = CompileAtmosphereShader(source, "VSMain", "vs_5_0", false);
    ComPtr<ID3DBlob> ps = CompileAtmosphereShader(source, "BrdfLutPS", "ps_5_0", false);
    if (!impl->rootSig || !vs || !ps) return false;
    impl->pso = CreateAtmosphereFullscreenPso(native, impl->rootSig.Get(), vs.Get(), ps.Get(), kBrdfLutFormat);
    if (!impl->pso) {
        spdlog::error("[brdf_lut] PSO");
        return false;
    }
    impl_ = std::move(impl);
    return true;
}

bool BrdfLut::Ready() const { return impl_ != nullptr; }
bool BrdfLut::Generated() const { return impl_ && impl_->generated; }

bool BrdfLut::Generate(const rs::rhi::CommandList& commandList) {
    if (!Ready() || !commandList.Valid() || impl_->generated) return false;
    ID3D12GraphicsCommandList* list = commandList.Native();

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = impl_->resource.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    list->ResourceBarrier(1, &barrier);

    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = impl_->rtvHeap->GetCPUDescriptorHandleForHeapStart();
    list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    const float size = static_cast<float>(ibl::kIblBrdfLutSize);
    const D3D12_VIEWPORT viewport = {0.0f, 0.0f, size, size, 0.0f, 1.0f};
    const D3D12_RECT scissor = {0, 0, static_cast<LONG>(ibl::kIblBrdfLutSize), static_cast<LONG>(ibl::kIblBrdfLutSize)};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
    list->SetGraphicsRootSignature(impl_->rootSig.Get());
    list->SetPipelineState(impl_->pso.Get());
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->IASetVertexBuffers(0, 0, nullptr);
    list->IASetIndexBuffer(nullptr);
    list->DrawInstanced(3, 1, 0, 0);

    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    list->ResourceBarrier(1, &barrier);
    impl_->generated = true;
    return true;
}

std::uint64_t BrdfLut::SrvGpuHandle() const { return impl_ ? impl_->srvGpuHandle : 0; }

} // namespace rs::render
