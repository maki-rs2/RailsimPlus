//
// BrowserFrameBudget.h - プラグインブラウザがメインスレッドで1フレームに使ってよい時間の定数と、測る部品
//
// ワーカー結果の受け取りは件数ではなく時間で区切る
// (3D描画が重くなっても受け取りが進み、1フレームにかける時間は一定以内に収まる)。
// どの段階も、予算を超えていても1フレームに最低1件は進める(止まらないように)。
//
#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>

namespace rs::app::plugin_browser {

// 走査結果のマージと画像結果の受け取りで、1フレームに使ってよい合計時間(ms)。
inline constexpr double kBrowserReceiveBudgetMs = 1.0;
// RmlUiのテクスチャ作成(新しく見せる画像)で、1フレームに使ってよい時間(ms)。
// こちらはRmlUiの描画中に起きて途中で区切れないので、前のフレームの実測から枚数を決める。
inline constexpr double kBrowserTextureUploadBudgetMs = 1.0;
// 1フレームに新しく見せる画像の上限(平均が極端に速くても、この枚数で止める)。
inline constexpr std::size_t kMaxNewImagesPerFrame = 8;
// まだ1枚も測れていないときの枚数。
inline constexpr std::size_t kInitialNewImagesPerFrame = 2;

// 経過時間が予算に達したか。時刻を引数で受ける純関数(テスト用)。
inline bool IsOverBudget(double elapsedMs, double budgetMs) {
    return elapsedMs >= budgetMs;
}

// 前のフレームで測ったテクスチャ作成の合計時間と枚数から、次のフレームに新しく見せてよい枚数を決める。
// 測れていない(枚数0)ときはkInitialNewImagesPerFrame。それ以外は「1枚あたりの平均時間」で予算を
// 割った枚数を、[1, kMaxNewImagesPerFrame]に収める。
inline std::size_t NewImagesForNextFrame(double measuredTotalMs, std::size_t measuredCount) {
    if (measuredCount == 0) return kInitialNewImagesPerFrame;
    const double averageMs = measuredTotalMs / static_cast<double>(measuredCount);
    if (averageMs <= 0.0) return kMaxNewImagesPerFrame;
    const double fit = kBrowserTextureUploadBudgetMs / averageMs;
    if (fit >= static_cast<double>(kMaxNewImagesPerFrame)) return kMaxNewImagesPerFrame;
    return std::max<std::size_t>(1, static_cast<std::size_t>(fit));
}

// 作った時刻からの経過時間を測る。1フレームの受け取りの頭で作り、走査と画像で共有する。
class FrameBudget {
public:
    explicit FrameBudget(double budgetMs = kBrowserReceiveBudgetMs)
        : budgetMs_(budgetMs), start_(std::chrono::steady_clock::now()) {}

    double ElapsedMs() const {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count();
    }
    bool Exceeded() const { return IsOverBudget(ElapsedMs(), budgetMs_); }

private:
    double budgetMs_;
    std::chrono::steady_clock::time_point start_;
};

} // namespace rs::app::plugin_browser
