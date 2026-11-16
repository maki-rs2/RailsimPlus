// MeshDynamicBuffers.cpp - gPartWorld/InstanceMatOverride/DynamicMaterialSlot
// バッファの確保・拡張・更新(実体はMeshPipeline.cppから移動)。
#include "MeshDynamicBuffers.h"

#include "../resource/TextureUpload.h"

#include <algorithm>
#include <cstring>

#include <spdlog/spdlog.h>

namespace rs::render {

bool MeshDynamicBuffers::Init(ID3D12Device* device, ID3D12DescriptorHeap* srvHeap, UINT srvSize,
                              UINT firstPartWorldDescriptorIndex,
                              UINT firstInstanceMatOverrideDescriptorIndex, UINT maxParts) {
    device_ = device;
    srvHeap_ = srvHeap;
    srvSize_ = srvSize;
    firstPartWorldIndex_ = firstPartWorldDescriptorIndex;
    firstInstanceMatOverrideIndex_ = firstInstanceMatOverrideDescriptorIndex;

    // パーツ単位ワールド行列（gPartWorld）用バッファをフレームごとに確保。
    // 以降は UpdatePartWorlds() が内容だけ書き換える。容量が足りなくなった場合は
    // EnsurePartWorldCapacity() が作り直す(インスタンス描画の配置数増加)。
    for (UINT f = 0; f < kFrames; ++f) {
        if (!CreatePartWorldBuffer(f, maxParts == 0 ? 1 : maxParts)) return false;
        if (!CreateInstanceMatOverrideBuffers(device, f, 1)) return false;
    }

    // 恒等行列1個で初期化しておく（未初期化の gPartWorld[0] 参照による
    // GPU ハングを防ぐ）。パーツ行列を与えない描画(エディタの押し出しメッシュ等)はこの値のまま描く。
    UpdatePartWorlds({}, 0);
    UpdatePartWorlds({}, 1);
    return true;
}

bool MeshDynamicBuffers::CreateInstanceMatOverrideBuffers(ID3D12Device* device, UINT f, UINT count) {
    // 新規リソースはローカルで組み立て、Mapまで成功して初めて既存メンバへ
    // 反映する(失敗時に既存バッファ・容量を壊さないため)。
    Microsoft::WRL::ComPtr<ID3D12Resource> newBuf;
    if (!CreatePooledUploadBuffer(device, UINT64(sizeof(InstanceMatOverride)) * count, newBuf)) return false;
    D3D12_RANGE noRead = {0, 0};
    InstanceMatOverride* newMapped = nullptr;
    if (FAILED(newBuf->Map(0, &noRead, reinterpret_cast<void**>(&newMapped)))) {
        spdlog::error("[mesh] インスタンス材質バッファのMapに失敗しました frame={}", f);
        return false;
    }

    // instanceMatOverrideBuf_[f]自体は先に組み立て済みだが、まだメンバへ
    // 反映していない状態でDynamicMaterialSlotBufferの生成を試みる。こちらが
    // 失敗した場合は新規リソースを破棄し、既存メンバには一切触れない
    // (部分適用を避ける)。
    if (!CreateDynamicMaterialSlotBuffer(f)) {
        newBuf->Unmap(0, nullptr);
        return false;
    }

    if (instanceMatOverrideMapped_[f]) {
        instanceMatOverrideBuf_[f]->Unmap(0, nullptr);
        instanceMatOverrideMapped_[f] = nullptr;
    }
    RetireBuffer(retirementQueue_, instanceMatOverrideBuf_[f]);
    instanceMatOverrideBuf_[f] = newBuf;
    instanceMatOverrideMapped_[f] = newMapped;
    WriteStructuredBufferSrv(device, srvHeap_, srvSize_, instanceMatOverrideBuf_[f].Get(), count,
                             sizeof(InstanceMatOverride), firstInstanceMatOverrideIndex_ + f * 2);
    instanceMatOverrideCapacity_[f] = count;
    return true;
}

bool MeshDynamicBuffers::CreateDynamicMaterialSlotBuffer(UINT f) {
    const UINT count = std::max(1u, UINT(dynamicMaterialSlots_.size()));
    Microsoft::WRL::ComPtr<ID3D12Resource> newBuf;
    if (!CreatePooledUploadBuffer(device_, UINT64(sizeof(uint32_t)) * count, newBuf)) return false;
    D3D12_RANGE noRead = {0, 0};
    uint32_t* newMapped = nullptr;
    if (FAILED(newBuf->Map(0, &noRead, reinterpret_cast<void**>(&newMapped)))) {
        spdlog::error("[mesh] 動的材質スロットバッファのMapに失敗しました frame={}", f);
        return false;
    }
    std::fill_n(newMapped, count, kInvalidDynamicMaterialSlot);
    if (!dynamicMaterialSlots_.empty()) {
        std::memcpy(newMapped, dynamicMaterialSlots_.data(),
                    dynamicMaterialSlots_.size() * sizeof(uint32_t));
    }

    if (dynamicMaterialSlotMapped_[f]) {
        dynamicMaterialSlotBuf_[f]->Unmap(0, nullptr);
        dynamicMaterialSlotMapped_[f] = nullptr;
    }
    RetireBuffer(retirementQueue_, dynamicMaterialSlotBuf_[f]);
    dynamicMaterialSlotBuf_[f] = newBuf;
    dynamicMaterialSlotMapped_[f] = newMapped;
    WriteStructuredBufferSrv(device_, srvHeap_, srvSize_, dynamicMaterialSlotBuf_[f].Get(), count,
                             sizeof(uint32_t), firstInstanceMatOverrideIndex_ + f * 2 + 1);
    return true;
}

bool MeshDynamicBuffers::EnsureInstanceMatOverrideCapacity(ID3D12Device* device, UINT requiredOverrides, UINT frameIndex) {
    const UINT f = frameIndex % kFrames;
    if (requiredOverrides <= instanceMatOverrideCapacity_[f]) return true;
    const UINT capacity = std::max(requiredOverrides, instanceMatOverrideCapacity_[f] * 2);
    return CreateInstanceMatOverrideBuffers(device, f, capacity);
}

void MeshDynamicBuffers::UpdateInstanceMatOverrides(const std::vector<InstanceMatOverride>& overrides, UINT frameIndex) {
    const UINT f = frameIndex % kFrames;
    if (!instanceMatOverrideMapped_[f] || overrides.empty()) return;
    const UINT count = std::min(UINT(overrides.size()), instanceMatOverrideCapacity_[f]);
    std::memcpy(instanceMatOverrideMapped_[f], overrides.data(), size_t(count) * sizeof(InstanceMatOverride));
}

void MeshDynamicBuffers::ConfigureDynamicMaterialSlots(const std::vector<uint32_t>& materialIndices,
                                                       size_t materialCount) {
    uint32_t maxMaterialIndex = 0;
    for (const uint32_t materialIndex : materialIndices) maxMaterialIndex = std::max(maxMaterialIndex, materialIndex);
    const size_t slotCount = std::max(materialCount,
                                      materialIndices.empty() ? size_t(1) : size_t(maxMaterialIndex) + 1);
    dynamicMaterialSlots_.assign(slotCount, kInvalidDynamicMaterialSlot);
    for (uint32_t slot = 0; slot < materialIndices.size(); ++slot)
        dynamicMaterialSlots_[materialIndices[slot]] = slot;
    // gInstanceMatOverrides側のper-instanceストライド(D)。ルックアップテーブル
    // dynamicMaterialSlots_のサイズ(M、全マテリアル数まで拡張されうる)とは別に
    // 保持する(上のDynamicMaterialSlotCount()コメント参照)。
    dynamicMaterialSlotCount_ = static_cast<UINT>(materialIndices.size());
    hasInstanceMatOverrides_ = !materialIndices.empty();
    for (UINT f = 0; f < kFrames; ++f) {
        if (!CreateDynamicMaterialSlotBuffer(f)) {
            spdlog::warn("[mesh] gDynamicMaterialSlot buffer rebuild failed (frame {})", f);
        }
    }
}

void MeshDynamicBuffers::BindInstanceMatOverrideTable(ID3D12GraphicsCommandList* cmdList, UINT f) const {
    D3D12_GPU_DESCRIPTOR_HANDLE handle = srvHeap_->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += SIZE_T(firstInstanceMatOverrideIndex_ + f * 2) * srvSize_;
    cmdList->SetGraphicsRootDescriptorTable(3, handle);
}

bool MeshDynamicBuffers::CreatePartWorldBuffer(UINT f, UINT parts, bool withPrev) {
    const UINT elements = withPrev ? parts * 2 : parts;
    Microsoft::WRL::ComPtr<ID3D12Resource> newBuf;
    if (!CreatePooledUploadBuffer(device_, UINT64(sizeof(PartXform)) * elements, newBuf)) {
        // 生成失敗時は既存のバッファ・容量値には触れない(呼び出し側が
        // 既存容量のまま継続できるようにする)。
        return false;
    }
    D3D12_RANGE noRead = {0, 0};
    uint8_t* newMapped = nullptr;
    if (FAILED(newBuf->Map(0, &noRead, reinterpret_cast<void**>(&newMapped)))) {
        spdlog::error("[mesh] パーツ行列バッファのMapに失敗しました frame={}", f);
        return false;
    }

    if (partWorldMapped_[f]) {
        partWorldBuf_[f]->Unmap(0, nullptr);
        partWorldMapped_[f] = nullptr;
    }
    RetireBuffer(retirementQueue_, partWorldBuf_[f]);
    partWorldBuf_[f] = newBuf;
    partWorldMapped_[f] = newMapped;

    WriteStructuredBufferSrv(device_, srvHeap_, srvSize_, partWorldBuf_[f].Get(), elements,
                             sizeof(PartXform), firstPartWorldIndex_ + f);

    partWorldCapacity_[f] = parts;
    partWorldHasPrev_[f] = withPrev;
    partWorldPrevOffset_[f] = 0;
    return true;
}

bool MeshDynamicBuffers::EnsurePartWorldCapacity(ID3D12Device* /*device*/, UINT requiredParts,
                                                 UINT frameIndex) {
    const UINT f = frameIndex % kFrames;
    if (requiredParts <= partWorldCapacity_[f]) return true;
    // 拡張は倍々で行い、大量配置のバッチでも再作成回数を抑える。
    const UINT grown = partWorldCapacity_[f] == 0 ? requiredParts : partWorldCapacity_[f] * 2;
    const UINT newCapacity = std::max(requiredParts, grown);
    if (!CreatePartWorldBuffer(f, newCapacity, partWorldHasPrev_[f])) {
        spdlog::warn("[mesh] gPartWorld buffer growth failed (requested {} parts)",
                     newCapacity);
        return false;
    }
    return true;
}

void MeshDynamicBuffers::UpdatePartWorlds(const std::vector<PartXform>& xforms,
                                          UINT frameIndex, uint64_t continuityKey) {
    UINT f = frameIndex % kFrames;
    if (xforms.empty()) {
        PartXform identity{};
        for (int i = 0; i < 16; ++i) {
            identity.world[i] = (i % 5 == 0) ? 1.0f : 0.0f;
            identity.normalMat[i] = identity.world[i];
        }
        history_.Advance(std::vector<PartXform>(1, identity), continuityKey, frameIndex);
    } else {
        history_.Advance(xforms, continuityKey, frameIndex);
    }
    if (!partWorldMapped_[f]) return;
    UINT count = UINT(history_.Curr().size());
    if (partWorldCapacity_[f] > 0 && count > partWorldCapacity_[f])
        count = partWorldCapacity_[f];
    // 動いているパーツがあるときだけ、前フレーム分を書く。初めてなら、この枠のバッファを前フレーム分つきで作り直す
    // (この枠のGPU実行は完了済みという、EnsurePartWorldCapacity と同じ前提)。作り直せなければ前フレーム分なし(速度0)。
    bool writePrev = history_.Moving() && device_ != nullptr;
    if (writePrev && !partWorldHasPrev_[f]) {
        writePrev = CreatePartWorldBuffer(f, std::max(partWorldCapacity_[f], count), true);
    }
    std::memcpy(partWorldMapped_[f], history_.Curr().data(),
                size_t(count) * sizeof(PartXform));
    partWorldPrevOffset_[f] = 0;
    if (writePrev) {
        std::memcpy(partWorldMapped_[f] + size_t(partWorldCapacity_[f]) * sizeof(PartXform),
                    history_.Prev().data(), size_t(count) * sizeof(PartXform));
        partWorldPrevOffset_[f] = partWorldCapacity_[f];
    }
}

} // namespace rs::render
