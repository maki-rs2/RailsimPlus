// SrvRangeAllocator.h - 可変長SRVディスクリプタ範囲のサイズクラス
// (スラブ)アロケータ。
//
// SceneRenderer常駐スロット・InstanceBatchManagerの両方が抱えていた「固定個数×
// 固定ストライドでSRVレンジをコンパイル時予約する」問題を解消する。エンティティ
// (常駐スロット1個/インスタンスバッチ1個)は「テクスチャ+transBuf+gPartWorldを
// まとめた1本の連続ブロック」を要求する運用を想定し、ブロック内部の3資源への
// 再分割は呼び出し側(App/FrameRenderer)が行う。このクラスは整数レンジの払い出し
// のみを担当し、D3D12へは一切依存しない(Catch2で直接テスト可能)。
//
// コンパクション(空き領域の詰め直し)は行わない(過剰設計、advisor判断)。同一
// サイズクラス内のブロックは相互に融通できるため、頻繁な確保/解放でも外部断片化で
// 領域が塞がれることはない。
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace rs::render {

class SrvRangeAllocator {
public:
    struct Range {
        uint32_t baseIndex = 0; // 絶対ヒープ添字(firstIndex起点)
        uint32_t capacity = 0;  // 確保された実容量(要求値以上、サイズクラスの粒度に切り上げ)
        bool     ok = false;
    };

    // firstIndex: このアロケータが管理する範囲の先頭ヒープ添字。
    // capacity: 管理する範囲の総スロット数。
    void Init(uint32_t firstIndex, uint32_t capacity);

    // requestedSlots以上のサイズクラスへ切り上げて1ブロックを確保する。
    // 末尾サイズクラスを超える要求、または空き容量不足時は ok=false を返す
    // (呼び出し側は警告ログを出し、そのエンティティの読込を諦めること)。
    Range Allocate(uint32_t requestedSlots);

    // Allocate() が返した Range をそのまま渡して解放する。
    // 同一サイズクラスのフリーリストへ返却するのみ(隣接ブロックとの併合はしない)。
    void Free(const Range& range);

    uint32_t UsedSlots() const { return usedSlots_; }

    // サイズクラス境界(スロット数)。小さいPierから大きい車両まで収まる粒度。
    // クラスを追加/変更する場合はここだけ触ればよい。
    //
    // 266 は SceneRenderer 常駐スロットの固定ストライド(kSceneEntitySrvStride =
    // テクスチャ256 + transBuf 4 + gPartWorld 2 + instanceMatOverride 4)に一致させた
    // 専用クラス。このプールは全スロットが同一のストライドを要求するため、直近上位の
    // 1024へ切り上げると大きく無駄が出て大規模レイアウトでSRV枯渇による非表示が
    // 発生することがある。ぴったりのクラスを設けることでディスクリプタ消費を抑えたまま
    // スロット数を確保する。App.h 側に static_assert があるため、
    // ストライドを変更する際はここも合わせること。
    static constexpr std::array<uint32_t, 7> kSizeClasses = {16, 64, 256, 266,
                                                             1024, 4096, 16384};

private:

    // requestedSlots を収容できる最小のサイズクラスの添字を返す。収まらなければ
    // kSizeClasses.size() を返す(呼び出し側でAllocate失敗として扱う)。
    size_t ClassIndexFor(uint32_t requestedSlots) const;

    uint32_t firstIndex_ = 0;
    uint32_t capacity_ = 0;
    uint32_t usedSlots_ = 0;
    uint32_t bumpNext_ = 0; // 未確保領域の先頭(フリーリストが空ならここから切り出す)
    std::array<std::vector<uint32_t>, kSizeClasses.size()> freeLists_; // クラスごとの解放済みブロックbaseIndex
};

} // namespace rs::render
