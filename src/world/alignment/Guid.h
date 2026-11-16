//
// Guid.h - 128bit安定ID。TrackGraph.h/PointRoutine.hの双方から
// 参照されるため、循環includeを避けるためTrackGraph.hから切り出した単独ヘッダ。
//
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <random>
#include <string>

namespace rs::world {

// ---- GUID（128bit 安定ID） ----------------------------------
using Guid = std::array<uint8_t, 16>;

inline bool GuidIsNil(const Guid& g) {
    for (uint8_t b : g)
        if (b != 0) return false;
    return true;
}

inline Guid NilGuid() { return Guid{}; }

// 敷設時にランダム128bitを発行する。衝突は無視できる確率。
inline Guid GenerateGuid() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist(
        0, std::numeric_limits<uint64_t>::max());
    Guid g{};
    uint64_t hi = dist(rng);
    uint64_t lo = dist(rng);
    for (int i = 0; i < 8; ++i) g[i] = static_cast<uint8_t>(hi >> (8 * i));
    for (int i = 0; i < 8; ++i) g[8 + i] = static_cast<uint8_t>(lo >> (8 * i));
    return g;
}

// seed文字列から毎回同じ128bitを導出する。同じseedからは常に同じGuidが得られる
// (乱数を使わないFNV-1a派生。RailSim II 互換ではなく本プロジェクト独自の安定id用途)。
// .rs2取り込みのように元データがGUIDを持たない配置へ、住所等から再現可能なGuidを
// 割り当てたい場合に使う(乱数発行のGenerateGuid()と使い分ける)。
inline Guid GuidFromSeed(const std::string& seed) {
    // FNV-1a(64bit)を、末尾に区切りバイトを挟んで2回かけ2つの64bit値を得る。
    constexpr uint64_t kFnvOffsetBasis = 1469598103934665603ull;
    constexpr uint64_t kFnvPrime = 1099511628211ull;
    const auto fnv1a64 = [kFnvPrime](uint64_t basis, const std::string& data, uint8_t salt) {
        uint64_t h = basis;
        h ^= salt;
        h *= kFnvPrime;
        for (const char c : data) {
            h ^= static_cast<uint8_t>(c);
            h *= kFnvPrime;
        }
        return h;
    };
    const uint64_t hi = fnv1a64(kFnvOffsetBasis, seed, 0x01);
    const uint64_t lo = fnv1a64(kFnvOffsetBasis, seed, 0x02);
    Guid g{};
    for (int i = 0; i < 8; ++i) g[i] = static_cast<uint8_t>(hi >> (8 * i));
    for (int i = 0; i < 8; ++i) g[8 + i] = static_cast<uint8_t>(lo >> (8 * i));
    return g;
}

struct GuidHash {
    size_t operator()(const Guid& g) const noexcept {
        // 先頭8バイトと末尾8バイトをFNV風に混ぜたハッシュ。
        uint64_t h = 1469598103934665603ull;
        for (uint8_t b : g) {
            h ^= b;
            h *= 1099511628211ull;
        }
        return static_cast<size_t>(h);
    }
};

inline bool operator==(const Guid& a, const Guid& b) {
    return std::equal(a.begin(), a.end(), b.begin());
}
inline bool operator!=(const Guid& a, const Guid& b) { return !(a == b); }

} // namespace rs::world
