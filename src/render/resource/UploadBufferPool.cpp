//
// UploadBufferPool.cpp - 小さなUPLOADバッファを共有ヒープからの配置リソースで作る。
//
// CreateCommittedResourceは1個あたり約0.18msかかるが、大きなヒープへの
// CreatePlacedResourceは約0.0045msで済む。そのため64KB以下のUPLOADバッファは
// 64MBのヒープ(チャンク)を共有して切り出す。リソースが破棄されたとき、
// リソースに付けた小さなCOMオブジェクトのデストラクタがブロックを返す。
//
#include "../pipeline/MeshPipelineTypes.h"
#include "UploadHeapBlockSlab.h"

#include <atomic>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include <spdlog/spdlog.h>

namespace rs::render {

namespace {

// リソースへ付けるブロック返却オブジェクトの識別子(このプール専用)。
// {6B1E5C2A-3D4F-4A8B-9C7E-2F1A0B5D8E31}
constexpr GUID kUploadBlockReleaserGuid =
    {0x6b1e5c2a, 0x3d4f, 0x4a8b, {0x9c, 0x7e, 0x2f, 0x1a, 0x0b, 0x5d, 0x8e, 0x31}};

// 1つのデバイスに対するプールの状態。ブロック返却オブジェクトからも
// shared_ptrで参照されるので、リソースより先に消えない。
struct PoolState {
    std::mutex mutex;
    UploadHeapBlockSlab slab;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Heap>> heaps;   // 添字はslabのチャンクと同じ
};

class BlockReleaser final : public IUnknown {
public:
    BlockReleaser(std::shared_ptr<PoolState> state, UploadHeapBlockSlab::Block block)
        : state_(std::move(state)), block_(block) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == __uuidof(IUnknown)) {
            *ppv = static_cast<IUnknown*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = --refCount_;
        if (remaining == 0) delete this;
        return remaining;
    }

private:
    ~BlockReleaser() {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->slab.Free(block_);
        // 空になったチャンクはヒープごと解放する(最後の1チャンクは残す)。
        if (state_->slab.AliveChunkCount() > 1 && state_->slab.ChunkEmpty(block_.chunk)) {
            state_->slab.RemoveChunk(block_.chunk);
            state_->heaps[block_.chunk].Reset();
        }
    }

    std::shared_ptr<PoolState> state_;
    UploadHeapBlockSlab::Block block_;
    std::atomic<ULONG> refCount_{1};
};

std::mutex g_poolsMutex;
std::map<ID3D12Device*, std::shared_ptr<PoolState>>& Pools() {
    static std::map<ID3D12Device*, std::shared_ptr<PoolState>> pools;
    return pools;
}

std::shared_ptr<PoolState> GetPool(ID3D12Device* device) {
    std::lock_guard<std::mutex> lock(g_poolsMutex);
    std::shared_ptr<PoolState>& pool = Pools()[device];
    if (!pool) pool = std::make_shared<PoolState>();
    return pool;
}

D3D12_RESOURCE_DESC BufferDesc(UINT64 byteSize) {
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = byteSize;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return desc;
}

bool CreateCommittedUpload(ID3D12Device* device, UINT64 byteSize,
                           Microsoft::WRL::ComPtr<ID3D12Resource>& out) {
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    const D3D12_RESOURCE_DESC desc = BufferDesc(byteSize);
    Microsoft::WRL::ComPtr<ID3D12Resource> res;
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &desc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&res)))) return false;
    out = std::move(res);
    return true;
}

// ブロックを1つ取って配置リソースを作る。ヒープが作れない・配置に失敗したらfalse。
// 成功時、ブロックはresに付けたBlockReleaserが返す。
bool CreatePlacedUpload(ID3D12Device* device, UINT64 byteSize,
                        Microsoft::WRL::ComPtr<ID3D12Resource>& out) {
    std::shared_ptr<PoolState> state = GetPool(device);
    UploadHeapBlockSlab::Block block;
    Microsoft::WRL::ComPtr<ID3D12Resource> res;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        std::optional<UploadHeapBlockSlab::Block> got = state->slab.Allocate();
        if (!got) {
            D3D12_HEAP_DESC heapDesc = {};
            heapDesc.SizeInBytes = kUploadHeapBlockBytes * kUploadHeapBlocksPerChunk;
            heapDesc.Properties.Type = D3D12_HEAP_TYPE_UPLOAD;
            heapDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
            heapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
            Microsoft::WRL::ComPtr<ID3D12Heap> heap;
            if (FAILED(device->CreateHeap(&heapDesc, IID_PPV_ARGS(&heap)))) return false;
            const uint32_t chunk = state->slab.AddChunk();
            if (state->heaps.size() <= chunk) state->heaps.resize(chunk + 1);
            state->heaps[chunk] = std::move(heap);
            got = state->slab.Allocate();
            if (!got) return false;
        }
        block = *got;
        const D3D12_RESOURCE_DESC desc = BufferDesc(byteSize);
        const HRESULT hr = device->CreatePlacedResource(
            state->heaps[block.chunk].Get(), UINT64(block.index) * kUploadHeapBlockBytes, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&res));
        if (FAILED(hr)) {
            state->slab.Free(block);
            return false;
        }
    }

    // 付け終わるまでは自分でブロックを返せるよう、ロックの外で付ける
    // (失敗時にresを手放してもBlockReleaserが無いので二重に返らない)。
    BlockReleaser* releaser = new BlockReleaser(state, block);
    const HRESULT hr = res->SetPrivateDataInterface(kUploadBlockReleaserGuid, releaser);
    releaser->Release();   // 成功時はリソースが参照を持つ。失敗時はここで破棄され、ブロックが返る。
    if (FAILED(hr)) return false;

    // 配置リソースの中身は前の使用者のままなので、committedと同じく0で初期化する。
    D3D12_RANGE noRead = {0, 0};
    void* mapped = nullptr;
    if (FAILED(res->Map(0, &noRead, &mapped))) {
        spdlog::error("[upload_pool] 配置バッファのMapに失敗しました");
        return false;
    }
    std::memset(mapped, 0, static_cast<size_t>(byteSize));
    res->Unmap(0, nullptr);

    out = std::move(res);
    return true;
}

}  // namespace

bool CreatePooledUploadBuffer(ID3D12Device* device, UINT64 byteSize,
                              Microsoft::WRL::ComPtr<ID3D12Resource>& out) {
    if (byteSize > 0 && byteSize <= kUploadHeapBlockBytes) {
        if (CreatePlacedUpload(device, byteSize, out)) return true;
        spdlog::warn("[upload_pool] 配置リソースの作成に失敗したのでcommittedで作ります size={}", byteSize);
    }
    return CreateCommittedUpload(device, byteSize, out);
}

}  // namespace rs::render
