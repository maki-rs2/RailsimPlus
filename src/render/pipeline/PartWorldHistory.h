//
// PartWorldHistory.h - パーツのワールド行列の「今」と「前に書いた分」を CPU で持つ。
//
// 速度バッファは、前フレームの位置から今の位置への動きを画素ごとに求める。そのため、
// gPartWorld に書くたびに、前回書いた行列(prev)との対応と、動いたかどうかを決める。
// GPU のバッファ(UPLOAD)はフレーム番号で2枚を交互に使い、もう一方の枠は GPU が読んでいる
// 最中に書き換わるので、前フレームの行列は CPU 側のここから取る。
//
// 行列の並びが前回と対応しているかは、呼び出し側が渡す連続キー(continuityKey)と、書いた
// フレーム番号で決める。キーが変わる、行列の数が変わる、履歴が無い(初回・Reset 直後)、
// 前回の書き込みが直前のフレームではない、のどれかなら、対応が崩れたとみなして prev = curr にする
// (その回だけ速度 0)。フレームが空いた場合を崩れとみなすのは、その間に別の経路で描かれて
// いた(同じ車両がバッチとスロットを行き来した、など)かもしれず、前回の行列が古いため。
//
#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

#include "MeshPipelineTypes.h"

namespace rs::render {

class PartWorldHistory {
public:
    // 今回の行列 curr を取り込み、前回の行列との関係(prev・Moving)を更新する。
    void Advance(const std::vector<PartXform>& curr, uint64_t continuityKey, uint32_t frameIndex);

    // 履歴を捨てる。次の Advance は prev = curr になる。
    void Reset() { valid_ = false; }

    // 直近の Advance で取り込んだ行列。
    const std::vector<PartXform>& Curr() const { return curr_; }
    // 前回の行列。対応が崩れたとき(Moving が false のとき)は Curr と同じ値。
    const std::vector<PartXform>& Prev() const { return prev_; }
    // 直近の Advance で、前回の行列と対応づいたか(false なら prev = curr に落とした)。
    bool Continuous() const { return continuous_; }
    // 直近の Advance で、前回と今回のワールド行列が1つでも違ったか。
    bool Moving() const { return moving_; }

private:
    std::vector<PartXform> curr_;
    std::vector<PartXform> prev_;
    uint64_t key_ = 0;
    uint32_t frame_ = 0;
    bool valid_ = false;
    bool continuous_ = false;
    bool moving_ = false;
};

// 連続キーの列を1つのキーにまとめる(並びの指紋)。順序と個数が変われば別の値になる。
// 編成のバッチのように、毎フレーム作り直す並びが前フレームと同じかを比べるのに使う。
constexpr uint64_t kContinuitySeed = 0xcbf29ce484222325ull;
constexpr uint64_t CombineContinuityKey(uint64_t fingerprint, uint64_t key) {
    constexpr uint64_t kPrime = 0x100000001b3ull;
    return (fingerprint ^ key) * kPrime + 0x9e3779b97f4a7c15ull;
}

// 行列の中身から作るキー。動かない物の並び(静的インスタンス)で、物の識別の代わりに使う。
// 並びの指紋に混ぜると、配置が増減・入れ替わったときに指紋が変わる。
inline uint64_t ContinuityKeyOfMatrix(const float (&world)[16]) {
    uint64_t key = kContinuitySeed;
    for (const float value : world) {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        key = CombineContinuityKey(key, bits);
    }
    return key;
}

} // namespace rs::render
