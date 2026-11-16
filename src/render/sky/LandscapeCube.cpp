#include "LandscapeCube.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <chrono>
#include <cstring>
#include <exception>
#include <future>
#include <string>

#include <ResourceUploadBatch.h>
#include <spdlog/spdlog.h>

#include "../../platform/file/AssetPath.h"
#include "../resource/RenderTuning.h"
#include "LandscapeCubeImpl.h"
#include "LandscapeMesh.h"

namespace rs::render {

using namespace landscape_cube;

namespace {

constexpr const char* kLandscapeImagePath = "assets/sky/Landscape.png";

D3D12_RESOURCE_DESC BufferDesc(UINT64 bytes) {
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return desc;
}

// CPU書き込みのアップロードヒープのバッファを作ってデータを写す(頂点・インデックス。小さく、作り直さない)。
bool CreateUploadBuffer(ID3D12Device* device, const void* data, UINT64 bytes, ComPtr<ID3D12Resource>& out) {
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    const D3D12_RESOURCE_DESC desc = BufferDesc(bytes);
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&out))))
        return false;
    void* mapped = nullptr;
    if (FAILED(out->Map(0, nullptr, &mapped))) {
        spdlog::error("[landscape] アップロードバッファのMapに失敗しました");
        return false;
    }
    std::memcpy(mapped, data, static_cast<size_t>(bytes));
    out->Unmap(0, nullptr);
    return true;
}

} // namespace

bool LandscapeCube::Impl::UploadImage(const rs::render::ImageData& pixels, ID3D12CommandQueue* queue) {
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = pixels.width;
    desc.Height = pixels.height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    desc.SampleDesc.Count = 1;
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                               nullptr, IID_PPV_ARGS(&image))))
        return false;
    try {
        DirectX::ResourceUploadBatch upload(device.Get());
        upload.Begin();
        D3D12_SUBRESOURCE_DATA sub = {};
        sub.pData = pixels.pixels.data();
        sub.RowPitch = static_cast<LONG_PTR>(pixels.width) * 4;
        sub.SlicePitch = sub.RowPitch * static_cast<LONG_PTR>(pixels.height);
        upload.Upload(image.Get(), 0, &sub, 1);
        upload.Transition(image.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        std::future<void> finished = upload.End(queue);
        finished.get();
    } catch (const std::exception& e) {
        spdlog::warn("[landscape] 画像のアップロードに失敗しました: {}", e.what());
        return false;
    }
    return true;
}

bool LandscapeCube::Impl::CreateCube(const rs::rhi::SrvHeap& sharedHeap, std::uint32_t srvIndex) {
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = kFaceSize;
    desc.Height = kFaceSize;
    desc.DepthOrArraySize = kFaceCount;
    desc.MipLevels = kMipCount;
    desc.Format = kSceneHdrFormat;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE clear = {};
    clear.Format = kSceneHdrFormat; // 透明(0,0,0,0)
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    // 生成直後はSRV状態(CubeTarget と同じ)。
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear,
                                               IID_PPV_ARGS(&cube))))
        return false;

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
    rtvDesc.NumDescriptors = kFaceCount * kMipCount;
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
    srvDesc.NumDescriptors = kPrivateSrvCount;
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&rtvHeap))) ||
        FAILED(device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&privateSrvHeap))))
        return false;
    rtvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    privateSrvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    for (std::uint32_t mip = 0; mip < kMipCount; ++mip) {
        for (std::uint32_t face = 0; face < kFaceCount; ++face) {
            D3D12_RENDER_TARGET_VIEW_DESC view = {};
            view.Format = kSceneHdrFormat;
            view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
            view.Texture2DArray.MipSlice = mip;
            view.Texture2DArray.FirstArraySlice = face;
            view.Texture2DArray.ArraySize = 1;
            device->CreateRenderTargetView(cube.Get(), &view, RtvHandle(face, mip));
        }
    }

    // 自前ヒープ: 0=元画像。
    D3D12_SHADER_RESOURCE_VIEW_DESC imageView = {};
    imageView.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    imageView.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    imageView.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    imageView.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(image.Get(), &imageView, PrivateSrvCpu(0));
    // 自前ヒープ: m=1以上は、ミップ m-1 の6面(ミップ m の生成の入力)。
    for (std::uint32_t m = 1; m < kMipCount; ++m) {
        D3D12_SHADER_RESOURCE_VIEW_DESC mipView = {};
        mipView.Format = kSceneHdrFormat;
        mipView.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        mipView.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        mipView.Texture2DArray.MostDetailedMip = m - 1;
        mipView.Texture2DArray.MipLevels = 1;
        mipView.Texture2DArray.FirstArraySlice = 0;
        mipView.Texture2DArray.ArraySize = kFaceCount;
        device->CreateShaderResourceView(cube.Get(), &mipView, PrivateSrvCpu(m));
    }

    // 共有ヒープ: 完成したキューブ(全ミップ)。
    D3D12_SHADER_RESOURCE_VIEW_DESC cubeView = {};
    cubeView.Format = kSceneHdrFormat;
    cubeView.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    cubeView.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    cubeView.TextureCube.MostDetailedMip = 0;
    cubeView.TextureCube.MipLevels = kMipCount;
    ID3D12DescriptorHeap* shared = sharedHeap.Native();
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = shared->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += static_cast<SIZE_T>(srvIndex) * sharedHeap.Stride();
    device->CreateShaderResourceView(cube.Get(), &cubeView, cpu);
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = shared->GetGPUDescriptorHandleForHeapStart();
    gpu.ptr += static_cast<UINT64>(srvIndex) * sharedHeap.Stride();
    sharedSrvGpuHandle = gpu.ptr;
    return true;
}

bool LandscapeCube::Impl::CreateMesh() {
    const LandscapeMeshData mesh = BuildLandscapeBandMesh();
    const UINT64 vertexBytes = mesh.vertices.size() * sizeof(LandscapeVertex);
    const UINT64 indexBytes = mesh.indices.size() * sizeof(std::uint32_t);
    if (!CreateUploadBuffer(device.Get(), mesh.vertices.data(), vertexBytes, vertexBuffer) ||
        !CreateUploadBuffer(device.Get(), mesh.indices.data(), indexBytes, indexBuffer))
        return false;
    vertexView = {vertexBuffer->GetGPUVirtualAddress(), static_cast<UINT>(vertexBytes), sizeof(LandscapeVertex)};
    indexView = {indexBuffer->GetGPUVirtualAddress(), static_cast<UINT>(indexBytes), DXGI_FORMAT_R32_UINT};
    indexCount = static_cast<UINT>(mesh.indices.size());
    return true;
}

LandscapeCube::LandscapeCube() = default;
LandscapeCube::~LandscapeCube() = default;

bool LandscapeCube::Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap,
                         const rs::rhi::CommandQueue& queue, std::uint32_t srvIndex) {
    impl_.reset();
    if (!device.Valid() || !srvHeap.Valid() || !queue.Valid()) return false;
    using Clock = std::chrono::steady_clock;
    const Clock::time_point t0 = Clock::now();

    const std::string path = rs::platform_file::ResolveAssetPath(kLandscapeImagePath);
    if (path.empty()) {
        spdlog::warn("[landscape] {} が見つかりません。遠景なしで動きます", kLandscapeImagePath);
        return false;
    }
    const rs::render::ImageData pixels = rs::render::LoadImageRGBA(path);
    if (!pixels.ok || pixels.width == 0 || pixels.height == 0) {
        spdlog::warn("[landscape] {} を読めません。遠景なしで動きます", path);
        return false;
    }
    const Clock::time_point t1 = Clock::now();

    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    impl->device = device.Native();
    const struct {
        const char* name;
        bool ok;
    } steps[] = {
        {"画像のアップロード", impl->UploadImage(pixels, queue.Native())},
        {"キューブ・ディスクリプタ", impl->CreateCube(srvHeap, srvIndex)},
        {"メッシュ", impl->CreateMesh()},
        {"ルート署名・PSO", impl->CreatePipelines()},
    };
    for (const auto& step : steps) {
        if (!step.ok) {
            spdlog::warn("[landscape] {}に失敗しました。遠景なしで動きます", step.name);
            return false;
        }
    }
    const Clock::time_point t2 = Clock::now();
    spdlog::info("[landscape] image={}x{} decode={:.1f}ms create={:.1f}ms", pixels.width, pixels.height,
                 std::chrono::duration<double, std::milli>(t1 - t0).count(),
                 std::chrono::duration<double, std::milli>(t2 - t1).count());
    impl->contrastStats = ComputeLandscapeContrastStats(pixels.pixels.data(), pixels.width, pixels.height,
                                                        RenderTuning::Embedded().landscapeContrast);
    spdlog::info("[landscape] contrast pivot={:.3f} meanScale={:.3f}", impl->contrastStats.pivotLuminance,
                 impl->contrastStats.meanScale);
    impl_ = std::move(impl);
    return true;
}

bool LandscapeCube::Available() const { return impl_ != nullptr; }
bool LandscapeCube::Ready() const { return impl_ && impl_->baked; }

bool LandscapeCube::Bake(const rs::rhi::CommandList& commandList) {
    if (!impl_ || impl_->baked || !commandList.Valid()) return false;
    impl_->RecordBake(commandList.Native());
    impl_->baked = true;
    spdlog::debug("[landscape] 遠景キューブの焼き込みを記録しました ({}x{}x6 mip{})", kFaceSize, kFaceSize, kMipCount);
    return true;
}

std::uint64_t LandscapeCube::SrvGpuHandle() const { return impl_ ? impl_->sharedSrvGpuHandle : 0; }

LandscapeContrastStats LandscapeCube::ContrastStats() const {
    return impl_ ? impl_->contrastStats : LandscapeContrastStats{};
}

} // namespace rs::render
