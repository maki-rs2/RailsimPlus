#include "EnvCubeSet.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <exception>
#include <future>
#include <string>
#include <vector>

#include <ResourceUploadBatch.h>

using Microsoft::WRL::ComPtr;

namespace rs::render {

namespace {

constexpr std::uint32_t kCubeFaceCount = 6;

bool ToDxgiFormat(EnvTexelFormat format, DXGI_FORMAT& out) {
    switch (format) {
    case EnvTexelFormat::Rgba8Srgb: out = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; return true;
    case EnvTexelFormat::Rgba16Float: out = DXGI_FORMAT_R16G16B16A16_FLOAT; return true;
    case EnvTexelFormat::Rgba32Float: out = DXGI_FORMAT_R32G32B32A32_FLOAT; return true;
    }
    return false;
}

// 読み済みデータが、GPUへ上げられる形か。失敗理由を返す(問題なければ空)。
std::string ValidateImage(const EnvTextureData& data, bool expectCube, const char* label) {
    if (!data.ok) return std::string(label) + ": " + (data.error.empty() ? "not loaded" : data.error);
    if (data.isCube != expectCube) {
        return std::string(label) + (expectCube ? ": not a cube map" : ": cube map where a 2D texture is expected");
    }
    const std::uint32_t expectedArray = expectCube ? kCubeFaceCount : 1u;
    if (data.width == 0 || data.height == 0 || data.mipLevels == 0 || data.arraySize != expectedArray) {
        return std::string(label) + ": invalid size or array count";
    }
    if (data.subresources.size() != static_cast<std::size_t>(data.arraySize) * data.mipLevels) {
        return std::string(label) + ": subresource count mismatch";
    }
    for (const EnvTextureSubresource& sub : data.subresources) {
        if (sub.rowPitch == 0 || sub.slicePitch == 0 || sub.offset + sub.slicePitch > data.bytes.size()) {
            return std::string(label) + ": subresource out of range";
        }
    }
    DXGI_FORMAT unused = DXGI_FORMAT_UNKNOWN;
    if (!ToDxgiFormat(data.format, unused)) return std::string(label) + ": unsupported format";
    return {};
}

// 1枚をGPUテクスチャにして、アップロードを upload へ積み、SRVを書く。
bool CreateAndQueue(ID3D12Device* device, DirectX::ResourceUploadBatch& upload, const EnvTextureData& data,
                    bool isCube, D3D12_CPU_DESCRIPTOR_HANDLE srvCpu, ComPtr<ID3D12Resource>& out) {
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    if (!ToDxgiFormat(data.format, format)) return false;

    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = data.width;
    desc.Height = data.height;
    desc.DepthOrArraySize = static_cast<UINT16>(data.arraySize);
    desc.MipLevels = static_cast<UINT16>(data.mipLevels);
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                               nullptr, IID_PPV_ARGS(&out)))) {
        return false;
    }

    std::vector<D3D12_SUBRESOURCE_DATA> subresources(data.subresources.size());
    for (std::size_t i = 0; i < subresources.size(); ++i) {
        const EnvTextureSubresource& sub = data.subresources[i];
        subresources[i].pData = data.bytes.data() + sub.offset;
        subresources[i].RowPitch = static_cast<LONG_PTR>(sub.rowPitch);
        subresources[i].SlicePitch = static_cast<LONG_PTR>(sub.slicePitch);
    }
    upload.Upload(out.Get(), 0, subresources.data(), static_cast<UINT>(subresources.size()));
    upload.Transition(out.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = format;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if (isCube) {
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srv.TextureCube.MostDetailedMip = 0;
        srv.TextureCube.MipLevels = data.mipLevels;
    } else {
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MostDetailedMip = 0;
        srv.Texture2D.MipLevels = data.mipLevels;
    }
    device->CreateShaderResourceView(out.Get(), &srv, srvCpu);
    return true;
}

} // namespace

struct EnvCubeSet::Impl {
    ID3D12Device* device = nullptr;
    ID3D12DescriptorHeap* srvHeap = nullptr;
    unsigned int srvStride = 0;
    ID3D12CommandQueue* queue = nullptr;
    std::uint32_t firstSrvIndex = 0;

    ComPtr<ID3D12Resource> textures[kSrvCount];
    std::uint32_t specularMips = 0;
    bool ready = false;
    std::uint64_t contentSerial = 0;
    std::string lastError;
};

EnvCubeSet::EnvCubeSet() = default;
EnvCubeSet::~EnvCubeSet() = default;

bool EnvCubeSet::Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap,
                      const rs::rhi::CommandQueue& queue, std::uint32_t firstSrvIndex) {
    impl_.reset();
    if (!device.Valid() || !srvHeap.Valid() || !queue.Valid()) return false;
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    impl->device = device.Native();
    impl->srvHeap = srvHeap.Native();
    impl->srvStride = srvHeap.Stride();
    impl->queue = queue.Native();
    impl->firstSrvIndex = firstSrvIndex;
    impl_ = std::move(impl);
    return true;
}

void EnvCubeSet::Clear() {
    if (!impl_) return;
    for (ComPtr<ID3D12Resource>& texture : impl_->textures) texture.Reset();
    impl_->specularMips = 0;
    impl_->ready = false;
    ++impl_->contentSerial;
}

bool EnvCubeSet::Upload(const EnvCubeImages& images) {
    if (!impl_) return false;
    Clear();
    impl_->lastError.clear();

    std::string error = ValidateImage(images.diffuse, true, "diffuse");
    if (error.empty()) error = ValidateImage(images.specular, true, "specular");
    if (error.empty()) error = ValidateImage(images.brdfLut, false, "brdfLut");
    if (!error.empty()) {
        impl_->lastError = error;
        return false;
    }

    const EnvTextureData* const sources[kSrvCount] = {&images.diffuse, &images.specular, &images.brdfLut};
    try {
        DirectX::ResourceUploadBatch upload(impl_->device);
        upload.Begin();
        D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = impl_->srvHeap->GetCPUDescriptorHandleForHeapStart();
        srvCpu.ptr += static_cast<SIZE_T>(impl_->firstSrvIndex) * impl_->srvStride;
        bool created = true;
        for (std::uint32_t i = 0; i < kSrvCount && created; ++i) {
            const bool isCube = i != kSrvCount - 1;
            created = CreateAndQueue(impl_->device, upload, *sources[i], isCube, srvCpu, impl_->textures[i]);
            srvCpu.ptr += impl_->srvStride;
        }
        // 失敗しても積んだ分は流してから資源を捨てる(アップロード中の資源を解放しない)。
        std::future<void> finished = upload.End(impl_->queue);
        finished.get();
        if (!created) {
            impl_->lastError = "failed to create a GPU texture";
            Clear();
            return false;
        }
    } catch (const std::exception& e) {
        impl_->lastError = std::string("upload failed: ") + e.what();
        Clear();
        return false;
    }
    impl_->specularMips = images.specular.mipLevels;
    impl_->ready = true;
    ++impl_->contentSerial;
    return true;
}

bool EnvCubeSet::Ready() const { return impl_ && impl_->ready; }
std::uint32_t EnvCubeSet::SpecularMipCount() const { return impl_ ? impl_->specularMips : 0; }

std::uint32_t EnvCubeSet::DiffuseSrvIndex() const { return impl_ ? impl_->firstSrvIndex + 0 : 0; }
std::uint32_t EnvCubeSet::SpecularSrvIndex() const { return impl_ ? impl_->firstSrvIndex + 1 : 0; }
std::uint32_t EnvCubeSet::BrdfLutSrvIndex() const { return impl_ ? impl_->firstSrvIndex + 2 : 0; }

std::uint64_t EnvCubeSet::SpecularSrvGpuHandle() const {
    if (!impl_) return 0;
    return impl_->srvHeap->GetGPUDescriptorHandleForHeapStart().ptr +
           static_cast<std::uint64_t>(impl_->firstSrvIndex + 1) * impl_->srvStride;
}

std::uint64_t EnvCubeSet::ContentSerial() const { return impl_ ? impl_->contentSerial : 0; }

const char* EnvCubeSet::LastError() const { return impl_ ? impl_->lastError.c_str() : ""; }

} // namespace rs::render
