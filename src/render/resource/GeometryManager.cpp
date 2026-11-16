#include "GeometryManager.h"

#include <spdlog/spdlog.h>

#include <algorithm>

using Microsoft::WRL::ComPtr;

namespace rs::render {

namespace {

// 使われなくなったジオメトリを残す量の上限。超えた分は古いものから手放す。
constexpr size_t kRecentGeometryMaxBytes = 256ull * 1024 * 1024;

size_t GeometryBytes(const GeometryResource& resource) {
    return size_t(resource.vertexCount) * resource.vertexStride +
           size_t(resource.indexCount) * sizeof(uint32_t);
}

ComPtr<ID3D12Resource> CreateDefaultBuffer(ID3D12Device* device,
                                           DirectX::ResourceUploadBatch& upload,
                                           const void* data, size_t bytes,
                                           D3D12_RESOURCE_STATES finalState) {
    if (bytes == 0) return nullptr;

    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = UINT64(bytes);
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    // D3D12バッファは初期状態が常にCOMMON(=COPY_DEST相当)に丸められるため、
    // 明示的にCOMMONを渡してデバッグレイヤーの警告(Ignoring InitialState)を避ける。
    ComPtr<ID3D12Resource> buf;
    if (FAILED(device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON,
            nullptr, IID_PPV_ARGS(&buf))))
        return nullptr;

    D3D12_SUBRESOURCE_DATA sub = {};
    sub.pData = data;
    sub.RowPitch = LONG_PTR(bytes);
    sub.SlicePitch = sub.RowPitch;
    upload.Upload(buf.Get(), 0, &sub, 1);
    upload.Transition(buf.Get(), D3D12_RESOURCE_STATE_COPY_DEST, finalState);
    return buf;
}

} // namespace

std::shared_ptr<GeometryResource> GeometryManager::Acquire(
    ID3D12Device* device, DirectX::ResourceUploadBatch& upload,
    const std::string& basePath, const Mesh& mesh) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::shared_ptr<GeometryResource> resident = cache_.Find(basePath);
    if (resident) return resident;

    if (mesh.vertices.empty() || mesh.indices.empty()) return nullptr;

    size_t vbBytes = mesh.vertices.size() * sizeof(MeshVertex);
    size_t ibBytes = mesh.indices.size() * sizeof(uint32_t);

    std::shared_ptr<GeometryResource> resource = std::make_shared<GeometryResource>();
    resource->vertexBuffer = CreateDefaultBuffer(
        device, upload, mesh.vertices.data(), vbBytes,
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    resource->indexBuffer = CreateDefaultBuffer(
        device, upload, mesh.indices.data(), ibBytes, D3D12_RESOURCE_STATE_INDEX_BUFFER);
    if (!resource->vertexBuffer || !resource->indexBuffer) {
        spdlog::warn("[geometry] GPU buffer creation failed: {}", basePath);
        return nullptr;
    }

    resource->vertexStride = UINT(sizeof(MeshVertex));
    resource->vertexCount = UINT(mesh.vertices.size());
    resource->indexCount = UINT(mesh.indices.size());
    cache_.Insert(basePath, resource);
    return resource;
}

void GeometryManager::KeepRecent(const std::shared_ptr<GeometryResource>& resource) {
    if (!resource) return;
    std::lock_guard<std::mutex> lock(mutex_);
    const std::list<std::shared_ptr<GeometryResource>>::iterator found =
        std::find(recent_.begin(), recent_.end(), resource);
    if (found != recent_.end()) {
        recent_.splice(recent_.begin(), recent_, found);
        return;
    }
    recent_.push_front(resource);
    recentBytes_ += GeometryBytes(*resource);
    // 手放しても、使用中のものは使っている側の参照で生き続ける。
    while (recentBytes_ > kRecentGeometryMaxBytes && recent_.size() > 1) {
        recentBytes_ -= GeometryBytes(*recent_.back());
        recent_.pop_back();
    }
}

void GeometryManager::ClearRecent() {
    std::lock_guard<std::mutex> lock(mutex_);
    recent_.clear();
    recentBytes_ = 0;
}

GeometryManager& GetGeometryManager() {
    static GeometryManager sManager;
    return sManager;
}

} // namespace rs::render
