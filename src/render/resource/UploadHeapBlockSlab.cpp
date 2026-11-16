#include "UploadHeapBlockSlab.h"

namespace rs::render {

UploadHeapBlockSlab::UploadHeapBlockSlab(uint32_t blocksPerChunk)
    : blocksPerChunk_(blocksPerChunk) {}

std::optional<UploadHeapBlockSlab::Block> UploadHeapBlockSlab::Allocate() {
    for (uint32_t c = 0; c < chunks_.size(); ++c) {
        Chunk& chunk = chunks_[c];
        if (!chunk.alive || chunk.freeIndices.empty()) continue;
        const uint32_t index = chunk.freeIndices.back();
        chunk.freeIndices.pop_back();
        chunk.allocated[index] = true;
        ++chunk.used;
        return Block{c, index};
    }
    return std::nullopt;
}

uint32_t UploadHeapBlockSlab::AddChunk() {
    uint32_t slot = static_cast<uint32_t>(chunks_.size());
    for (uint32_t c = 0; c < chunks_.size(); ++c) {
        if (!chunks_[c].alive) { slot = c; break; }
    }
    if (slot == chunks_.size()) chunks_.emplace_back();
    Chunk& chunk = chunks_[slot];
    chunk.alive = true;
    chunk.used = 0;
    chunk.allocated.assign(blocksPerChunk_, false);
    chunk.freeIndices.clear();
    chunk.freeIndices.reserve(blocksPerChunk_);
    // 小さい添字から払い出すよう、降順に積む。
    for (uint32_t i = blocksPerChunk_; i > 0; --i) chunk.freeIndices.push_back(i - 1);
    ++aliveChunks_;
    return slot;
}

void UploadHeapBlockSlab::Free(Block block) {
    if (block.chunk >= chunks_.size()) return;
    Chunk& chunk = chunks_[block.chunk];
    if (!chunk.alive || block.index >= blocksPerChunk_ || !chunk.allocated[block.index]) return;
    chunk.allocated[block.index] = false;
    chunk.freeIndices.push_back(block.index);
    --chunk.used;
}

bool UploadHeapBlockSlab::ChunkEmpty(uint32_t chunk) const {
    return chunk < chunks_.size() && chunks_[chunk].alive && chunks_[chunk].used == 0;
}

bool UploadHeapBlockSlab::RemoveChunk(uint32_t chunk) {
    if (!ChunkEmpty(chunk)) return false;
    Chunk& target = chunks_[chunk];
    target.alive = false;
    target.freeIndices.clear();
    target.freeIndices.shrink_to_fit();
    target.allocated.clear();
    target.allocated.shrink_to_fit();
    --aliveChunks_;
    return true;
}

}  // namespace rs::render
