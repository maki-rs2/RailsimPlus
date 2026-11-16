//
// MeshGeometry.h - MeshPipelineの頂点/インデックスバッファとsubmesh配列(データクラスタ)
//
// 「ジオメトリ」責務(vertexBuffer_/indexBuffer_/
// sharedGeometry_/vbView_/ibView_/indexCount_/submeshes_)だけを持つクラス。
// UploadMesh()で一度書き込み、Record*系から毎フレーム読み取られる。
//
#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <ResourceUploadBatch.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../mesh/Mesh.h"
#include "../resource/GeometryResource.h"
#include "../rhi/RetirementQueue.h"

namespace rs::render {

class MeshGeometry {
public:
    // 頂点/インデックスバッファを作成しvbView_/ibView_/indexCount_/sharedGeometry_/
    // vertexBuffer_/indexBuffer_を設定する。失敗時false。
    bool Upload(ID3D12Device* device, DirectX::ResourceUploadBatch& upload,
                const Mesh& mesh,
                std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>& keepAlive,
                const std::string& geometryKey);

    // 書き換えモードの頂点バッファの枚数(フレームの重なりの数)。
    static constexpr uint32_t kDynamicFrames = 2;

    // 書き換えモード(Mesh::dynamicVertexCount > 0 で上げたとき)のスロットだけ働く。
    // frameSlot 枚目のバッファの書き換え範囲へ頂点を書く。count が範囲と合わないとき、
    // 書き換えモードでないときは、何もせず false。
    bool WriteDynamicVertices(uint32_t frameSlot, const MeshVertex* vertices, uint32_t count);
    // 描画に使う頂点バッファを frameSlot 枚目へ切り替える。書き換えモードでなければ何もしない。
    void SelectDynamicFrame(uint32_t frameSlot);

    void Reset();
    void RetireBuffers(rs::rhi::RetirementQueue* queue);
    // 共有ジオメトリを GeometryManager の「最近使ったもの」に入れる。
    void KeepSharedForReuse() const;

    bool HasMesh() const { return indexCount_ > 0; }
    UINT IndexCount() const { return indexCount_; }
    const D3D12_VERTEX_BUFFER_VIEW& VertexBufferView() const { return vbView_; }
    const D3D12_INDEX_BUFFER_VIEW& IndexBufferView() const { return ibView_; }
    std::vector<SubMesh>& Submeshes() { return submeshes_; }
    const std::vector<SubMesh>& Submeshes() const { return submeshes_; }

private:
    // 書き換えモードの状態(ポインタ・範囲・バッファの参照)を捨てる。GPU が使っているかもしれない
    // バッファは、呼び出し側が先に退避キューへ移しておく。
    void ClearDynamicState();

    // 書き換えモード: UPLOAD ヒープの頂点バッファを Map したまま持つ。
    std::shared_ptr<GeometryResource>      dynamicVertexBuffers_[kDynamicFrames];
    uint8_t*                               dynamicVertexMapped_[kDynamicFrames] = {};
    uint32_t                               dynamicVertexFirst_ = 0;
    uint32_t                               dynamicVertexCount_ = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer_;
    std::shared_ptr<GeometryResource>       sharedGeometry_;
    D3D12_VERTEX_BUFFER_VIEW                vbView_ = {};
    D3D12_INDEX_BUFFER_VIEW                 ibView_ = {};
    uint32_t                                indexCount_ = 0;
    std::vector<SubMesh>                    submeshes_;
};

} // namespace rs::render
