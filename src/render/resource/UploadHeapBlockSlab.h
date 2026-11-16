//
// UploadHeapBlockSlab.h - 固定サイズのブロックをチャンク単位で払い出す純C++の管理表。
//
// 小さなUPLOADバッファを大きなヒープから切り出して作るとき(UploadBufferPool)の、
// 「どのヒープのどの位置が空いているか」だけを持つ。D3D12には依存しない。
// 1チャンク = ヒープ1個に対応し、チャンクの添字は呼び出し側のヒープ配列の添字と揃える。
//
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace rs::render {

// 1ブロックの大きさ。D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT(64KB)と同じ値で、
// 配置リソースのオフセットはこの倍数にする必要がある。
constexpr uint64_t kUploadHeapBlockBytes = 65536;

// 1チャンクあたりのブロック数。64KB x 1024 = 64MB。
constexpr uint32_t kUploadHeapBlocksPerChunk = 1024;

class UploadHeapBlockSlab {
public:
    struct Block {
        uint32_t chunk = 0;
        uint32_t index = 0;
    };

    explicit UploadHeapBlockSlab(uint32_t blocksPerChunk = kUploadHeapBlocksPerChunk);

    // 空きのあるチャンクから1ブロック払い出す。どのチャンクにも空きがなければnullopt。
    std::optional<Block> Allocate();

    // 空のチャンクを1つ足し、その添字を返す。RemoveChunkで空いた添字があれば再利用する。
    uint32_t AddChunk();

    // ブロックを返す。払い出されていないブロックの返却は無視する(二重返却で
    // 同じブロックが2回払い出されないようにするため)。
    void Free(Block block);

    // チャンクが有効で、払い出し中のブロックが1つもなければtrue。
    bool ChunkEmpty(uint32_t chunk) const;

    // 空のチャンクだけ取り除く(添字はずらさず、後でAddChunkが再利用する)。
    // 空でない・無効なチャンクならfalseを返し何もしない。
    bool RemoveChunk(uint32_t chunk);

    // 有効なチャンクの数。
    uint32_t AliveChunkCount() const { return aliveChunks_; }

private:
    struct Chunk {
        bool alive = false;
        uint32_t used = 0;
        std::vector<uint32_t> freeIndices;   // 末尾から払い出す
        std::vector<bool> allocated;         // 添字ごとの払い出し中フラグ
    };

    uint32_t blocksPerChunk_;
    uint32_t aliveChunks_ = 0;
    std::vector<Chunk> chunks_;
};

}  // namespace rs::render
