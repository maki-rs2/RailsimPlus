//
// SrvHeapReserver.h - SRVヒープの固定範囲を、名前つきで先頭から順に予約する払い出し表
//
// 部品ごとの範囲の先頭を「前の部品の末尾」から手で連鎖させず、予約した順に払い出す。
// 予約はコンパイル時にも実行時にも使える(ヒープの総数はデバイスを作る前に要るため、
// SrvLayout はコンパイル時に組み立てる)。
// SrvRangeAllocator(render層)は実行時に可変長のブロックを出し入れするプール用で、
// こちらは起動時に一度だけ決まる固定範囲の割り付けだけを受け持つので、役割が重ならない。
//
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace rs::app {

struct SrvReservation {
    const char* name = "";
    std::uint32_t first = 0;
    std::uint32_t count = 0;
};

class SrvHeapReserver {
public:
    static constexpr std::size_t kMaxReservations = 48;

    // count 個の連続した範囲を予約し、先頭の番号を返す。count が 0 でも予約表には残る。
    // 予約表が満杯のときは範囲を払い出さず、Overflowed() が真になる(コンパイル時に検出する)。
    constexpr std::uint32_t Reserve(const char* name, std::uint32_t count) {
        if (size_ >= kMaxReservations) {
            overflowed_ = true;
            return next_;
        }
        const std::uint32_t first = next_;
        entries_[size_++] = SrvReservation{name, first, count};
        next_ += count;
        return first;
    }

    constexpr std::uint32_t Total() const { return next_; }
    constexpr std::size_t Size() const { return size_; }
    constexpr bool Overflowed() const { return overflowed_; }
    constexpr const SrvReservation& At(std::size_t index) const { return entries_[index]; }

private:
    std::array<SrvReservation, kMaxReservations> entries_{};
    std::size_t size_ = 0;
    std::uint32_t next_ = 0;
    bool overflowed_ = false;
};

} // namespace rs::app
