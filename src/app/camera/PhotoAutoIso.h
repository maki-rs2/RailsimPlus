#pragma once

#include <cstddef>
#include <optional>
#include <span>

namespace rs::app {

// ISO オート(撮影カメラの ISO 欄が AUTO のとき)。測光した画面の明るさから、画面が目標の明るさになる ISO を求める。

// ISO オートが選ぶ ISO の範囲(入力欄の手動の段の 50・12800 は使わない)。
inline constexpr double kAutoIsoMin = 100.0;
inline constexpr double kAutoIsoMax = 6400.0;

// 測光した対数輝度(log2)の格子から、中央重みの平均で画面の輝度を求める。格子は行ごとに左から並び、
// 幅 width・高さ height(要素数は width*height)。重みは、格子の中心からの距離(各軸 -1〜1 に正規化)のガウスで、
// 中央が重い。数が合わない・空・非有限の値を含むときは nullopt。
std::optional<double> MeteredLuminance(std::span<const float> logLuminance, std::size_t width, std::size_t height);

// ISO を求める調整値と固定の倍率(RenderTuning の値とトーンマップの露出から、呼ぶ側が詰める)。
struct AutoIsoParams {
    double targetBrightness = 0.2;   // 画面の目標の明るさ(測光した輝度 x トーンマップの露出 x 撮影カメラの倍率)
    double tonemapExposure = 1.0;    // トーンマップの固定の露出倍率(撮影カメラの倍率を掛ける前)
    double followSeconds = 0.5;      // ISO が目標へ追いつく時定数[秒]。0 以下なら即座に合わせる
};

// 画面の明るさ(luminance x tonemapExposure x 撮影カメラの倍率)が targetBrightness になる ISO。
// 撮影カメラの倍率は PhotoExposureMultiplier と同じ式で、絞りとシャッターを固定して ISO だけ動かす。
// 結果は [kAutoIsoMin, kAutoIsoMax] に収める。luminance が 0 以下なら暗いものとして上限。
// 引数が非有限、またはトーンマップの露出・目標が 0 以下のときは kDefaultIso。
double AutoIsoForLuminance(double luminance, double aperture, double shutterSeconds, const AutoIsoParams& params);

// 決まった ISO を、対数(段)の空間で目標へなめらかに追わせる。最後に測った輝度を覚えているので、
// 読み戻しの合間に絞りやシャッターを変えても、目標はそのつど求め直す。
class AutoIsoState {
public:
    AutoIsoState() { Reset(); }

    // 測った輝度と追従の状態を捨てる(次に測るまで kDefaultIso に戻る)。
    void Reset();

    // dtSeconds 進める。measuredLuminance があれば、最後に測った輝度として覚える。
    // まだ一度も測っていない間は kDefaultIso。返すのは今の ISO(kAutoIsoMin〜kAutoIsoMax)。
    double Update(double dtSeconds, std::optional<double> measuredLuminance, double aperture, double shutterSeconds,
                  const AutoIsoParams& params);

    // 今の ISO(Update の返り値と同じ)。
    double Iso() const;
    // 今の輝度から求めた、追従前の目標の ISO(ログ用)。測っていなければ nullopt。
    std::optional<double> TargetIso(double aperture, double shutterSeconds, const AutoIsoParams& params) const;

private:
    std::optional<double> luminance_;
    double log2Iso_ = 0.0; // 今の ISO の log2(Reset が kDefaultIso の値にする)
};

} // namespace rs::app
