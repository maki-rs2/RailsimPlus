#include "MeshGeometry.h"

#include <cstring>

#include <spdlog/spdlog.h>

#include "../resource/GeometryManager.h"

namespace rs::render {

bool MeshGeometry::Upload(
    ID3D12Device* device, DirectX::ResourceUploadBatch& upload, const Mesh& mesh,
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>& keepAlive,
    const std::string& geometryKey) {
    // mappedOut を渡すと、Unmap せずに書き込み先ポインタを返す(書き換えモードの頂点バッファ用)。
    auto makeUpload = [&](const void* data, size_t bytes,
                          uint8_t** mappedOut = nullptr) -> Microsoft::WRL::ComPtr<ID3D12Resource> {
        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = bytes;
        rd.Height = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Microsoft::WRL::ComPtr<ID3D12Resource> res;
        if (FAILED(device->CreateCommittedResource(
                &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr, IID_PPV_ARGS(&res))))
            return nullptr;
        void* p = nullptr;
        D3D12_RANGE noRead = {0, 0};
        if (FAILED(res->Map(0, &noRead, &p))) {
            spdlog::error("[mesh] ジオメトリバッファのMapに失敗しました");
            return nullptr;
        }
        std::memcpy(p, data, bytes);
        if (mappedOut) {
            *mappedOut = static_cast<uint8_t*>(p);
        } else {
            res->Unmap(0, nullptr);
        }
        return res;
    };

    size_t vbBytes = mesh.vertices.size() * sizeof(MeshVertex);
    size_t ibBytes = mesh.indices.size() * sizeof(uint32_t);
    sharedGeometry_.reset();
    vertexBuffer_.Reset();
    indexBuffer_.Reset();
    ClearDynamicState();
    // 書き換えモード。範囲が頂点配列の末尾に収まらないメッシュは、書き換えない通常のメッシュとして扱う。
    const bool dynamicMode =
        mesh.dynamicVertexCount > 0 &&
        size_t(mesh.dynamicVertexFirst) + mesh.dynamicVertexCount == mesh.vertices.size();
    if (mesh.dynamicVertexCount > 0 && !dynamicMode) {
        spdlog::warn("[mesh] 書き換える頂点の範囲が末尾に収まらないため、書き換えモードにしない: "
                     "first={} count={} 頂点数={}",
                     mesh.dynamicVertexFirst, mesh.dynamicVertexCount, mesh.vertices.size());
    }
    if (dynamicMode) {
        // 共有ジオメトリには書かない(同じ形を使う他のスロットが壊れるため)。頂点は枠ごとに1枚持つ。
        for (uint32_t i = 0; i < kDynamicFrames; ++i) {
            dynamicVertexBuffers_[i] = std::make_shared<GeometryResource>();
            dynamicVertexBuffers_[i]->vertexBuffer =
                makeUpload(mesh.vertices.data(), vbBytes, &dynamicVertexMapped_[i]);
            if (!dynamicVertexBuffers_[i]->vertexBuffer) {
                ClearDynamicState();
                return false;
            }
        }
        indexBuffer_ = makeUpload(mesh.indices.data(), ibBytes);
        if (!indexBuffer_) {
            ClearDynamicState();
            return false;
        }
        dynamicVertexFirst_ = mesh.dynamicVertexFirst;
        dynamicVertexCount_ = mesh.dynamicVertexCount;
        vbView_.BufferLocation = dynamicVertexBuffers_[0]->vertexBuffer->GetGPUVirtualAddress();
        ibView_.BufferLocation = indexBuffer_->GetGPUVirtualAddress();
    } else if (!geometryKey.empty()) {
        sharedGeometry_ = GetGeometryManager().Acquire(device, upload, geometryKey, mesh);
        if (!sharedGeometry_) return false;
        vbView_.BufferLocation = sharedGeometry_->vertexBuffer->GetGPUVirtualAddress();
        ibView_.BufferLocation = sharedGeometry_->indexBuffer->GetGPUVirtualAddress();
    } else {
        vertexBuffer_ = makeUpload(mesh.vertices.data(), vbBytes);
        indexBuffer_ = makeUpload(mesh.indices.data(), ibBytes);
        if (!vertexBuffer_ || !indexBuffer_) return false;
        vbView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
        ibView_.BufferLocation = indexBuffer_->GetGPUVirtualAddress();
    }
    // 共有バッファを使い回したときは、バッファの実際の大きさでビューを作る。同じキーで
    // 大きさの違うメッシュが来ても、バッファの終端を越えて読まないようにするため。
    UINT viewIndexCount = UINT(mesh.indices.size());
    if (sharedGeometry_) {
        const bool sizeMismatch = sharedGeometry_->vertexCount != mesh.vertices.size() ||
                                  sharedGeometry_->indexCount != mesh.indices.size();
        if (sizeMismatch) {
            spdlog::warn("[mesh] geometryKey={} の共有バッファとメッシュの大きさが違う: "
                         "バッファ 頂点{}/索引{}、メッシュ 頂点{}/索引{}。バッファの大きさで描く",
                         geometryKey, sharedGeometry_->vertexCount, sharedGeometry_->indexCount,
                         mesh.vertices.size(), mesh.indices.size());
        }
        vbBytes = size_t(sharedGeometry_->vertexCount) * sizeof(MeshVertex);
        ibBytes = size_t(sharedGeometry_->indexCount) * sizeof(uint32_t);
        viewIndexCount = sharedGeometry_->indexCount;
    }
    vbView_.StrideInBytes = sizeof(MeshVertex);
    vbView_.SizeInBytes = UINT(vbBytes);
    ibView_.Format = DXGI_FORMAT_R32_UINT;
    ibView_.SizeInBytes = UINT(ibBytes);
    indexCount_ = viewIndexCount;

    // sharedGeometry_ 使用時は GeometryManager 側が寿命を管理するため keepAlive 不要
    // (vertexBuffer_/indexBuffer_ は非共有時のみ生成されるので、この2行は自然に空振りする)。
    if (vertexBuffer_) keepAlive.push_back(vertexBuffer_);
    if (indexBuffer_) keepAlive.push_back(indexBuffer_);
    return true;
}

void MeshGeometry::KeepSharedForReuse() const {
    GetGeometryManager().KeepRecent(sharedGeometry_);
}

void MeshGeometry::ClearDynamicState() {
    for (uint32_t i = 0; i < kDynamicFrames; ++i) {
        dynamicVertexMapped_[i] = nullptr;
        dynamicVertexBuffers_[i].reset();
    }
    dynamicVertexFirst_ = 0;
    dynamicVertexCount_ = 0;
}

bool MeshGeometry::WriteDynamicVertices(uint32_t frameSlot, const MeshVertex* vertices,
                                        uint32_t count) {
    if (dynamicVertexCount_ == 0 || frameSlot >= kDynamicFrames || !vertices ||
        count != dynamicVertexCount_ || !dynamicVertexMapped_[frameSlot])
        return false;
    std::memcpy(dynamicVertexMapped_[frameSlot] + size_t(dynamicVertexFirst_) * sizeof(MeshVertex),
                vertices, size_t(count) * sizeof(MeshVertex));
    return true;
}

void MeshGeometry::SelectDynamicFrame(uint32_t frameSlot) {
    if (dynamicVertexCount_ == 0 || frameSlot >= kDynamicFrames || !dynamicVertexBuffers_[frameSlot])
        return;
    vbView_.BufferLocation = dynamicVertexBuffers_[frameSlot]->vertexBuffer->GetGPUVirtualAddress();
}

void MeshGeometry::RetireBuffers(rs::rhi::RetirementQueue* queue) {
    KeepSharedForReuse();
    if (queue && sharedGeometry_) queue->Retire(std::move(sharedGeometry_));
    sharedGeometry_.reset();
    // 書き換えモードの頂点バッファは GPU がまだ使っているかもしれないので、退避キューへ渡す。
    for (uint32_t i = 0; i < kDynamicFrames; ++i) {
        dynamicVertexMapped_[i] = nullptr;
        if (!dynamicVertexBuffers_[i]) continue;
        if (queue) queue->Retire(std::move(dynamicVertexBuffers_[i]));
    }
    ClearDynamicState();
    if (vertexBuffer_ || indexBuffer_) {
        std::shared_ptr<GeometryResource> own = std::make_shared<GeometryResource>();
        own->vertexBuffer = std::move(vertexBuffer_);
        own->indexBuffer = std::move(indexBuffer_);
        if (queue) queue->Retire(std::move(own));
    }
}

void MeshGeometry::Reset() {
    vertexBuffer_.Reset();
    indexBuffer_.Reset();
    sharedGeometry_.reset();
    ClearDynamicState();
    submeshes_.clear();
    indexCount_ = 0;
}

} // namespace rs::render
