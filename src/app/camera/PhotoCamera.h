#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../workspace/CameraScreenSteps.h"

namespace rs::app::workspace { struct CameraScreenField; struct CameraScreenPresentation; }

namespace rs::app {

// 撮影カメラの状態。値の元はカメラ画面の入力欄で、描画側はここから読む。
inline constexpr double kDefaultFocalLengthMm = 35.0;
inline constexpr double kDefaultAperture = 5.6;
inline constexpr double kDefaultShutterSeconds = 1.0 / 125.0;
inline constexpr double kDefaultIso = 100.0;
inline constexpr double kDefaultFocusDistanceM = 10.0;

struct PhotoCamera {
    double focalLengthMm = kDefaultFocalLengthMm;
    double aperture = kDefaultAperture;
    double shutterSeconds = kDefaultShutterSeconds;
    double iso = kDefaultIso;
    bool isoAuto = false; // ISO欄が「AUTO」。iso は基準値のままで、決まった値は PhotoEffectiveIso で求める
    double focusDistanceM = kDefaultFocusDistanceM;
};

// 入力欄の文字列を、項目に応じた値(焦点距離[mm]・絞り・秒・ISO・距離[m])へ変換する。
// シャッター速度は "1/125" と "2" の形、他は数値。正の有限値でなければ nullopt。
std::optional<double> ParsePhotoValue(workspace::CameraScreenParameter parameter, std::string_view text);

// カメラ画面の入力欄から撮影カメラの状態を作る。変換できない欄は既定値になる。
PhotoCamera MakePhotoCamera(const workspace::CameraScreenPresentation& screen);

// 露出と表示に使うISO。手動なら入力欄の値。ISOオートなら autoIso(測光から求めた値。PhotoAutoIso.h の AutoIsoState)で、
// 無ければ入力欄の値(基準のISO)。
double PhotoEffectiveIso(const PhotoCamera& camera, std::optional<double> autoIso);

// 入力欄の段のうち、数値として読める段だけを対象に、value に対数で最も近い段の文字列。無ければ空。
// 先頭の「AUTO」のように数値でない段は飛ばす。
std::string NearestNumericPhotoStep(workspace::CameraScreenParameter parameter, const std::vector<std::string>& steps,
                                    double value);

// ISO・絞り・シャッター秒から決まる明るさの倍率。基準(既定のISO・絞り・シャッター)で 1.0。
// 倍率 = (iso/基準ISO) * (秒/基準秒) * (基準絞り/絞り)^2。結果は 1/4096〜4096 に収める。
// 引数が不正(非有限・0以下)なときは 1.0。ISOオートが同じ式を使えるよう、ISO を引数に取る。
double PhotoExposureMultiplier(double iso, double aperture, double shutterSeconds);

// 35mm判のフレームの長辺と短辺[mm]。
inline constexpr double kPhotoLongSideMm = 36.0;
inline constexpr double kPhotoShortSideMm = 24.0;
// 通常画面(ファインダーが出ていないとき)の視野角を焦点距離へ換算するときのフレームの高さ[mm]。
inline constexpr double kNormalViewFrameHeightMm = kPhotoShortSideMm;

// 撮影するフレーム(センサー面の撮像範囲)の大きさ[mm]。
struct PhotoFrameSize {
    double widthMm = 36.0;
    double heightMm = 24.0;
};

// 出力の縦横比 aspect(幅/高さ)に合わせたフレームの大きさ。35mm判の 36x24 mm に収まる最大の長方形で、
// aspect が 1 未満のときは本体を縦に構えたのと同じく 24x36 mm に収める。
// フレームが縦横比ごとに変わるので、同じ焦点距離でも縦長・正方形では視野角と被写界深度の見え方が変わる。
// aspect が不正(非有限・0以下)なときは 36x24 mm。
PhotoFrameSize PhotoFrameForAspect(double aspect);

// 焦点距離から縦の視野角[rad]を求める。フレームの高さ(PhotoFrameForAspect)を使って 2*atan(高さ/2/焦点距離)。
// 焦点距離か縦横比が不正なときは kViewerFovYRadians を返す。
float FovYFromFocalLength(double focalLengthMm, double aspect);

// フレームの高さ frameHeightMm[mm]と焦点距離から縦の視野角[rad]を求める(FovYFromFocalLength の式の本体)。
// 焦点距離かフレームの高さが不正なときは kViewerFovYRadians を返す。
float FovYFromFrameHeight(double focalLengthMm, double frameHeightMm);

// FovYFromFrameHeight の逆。縦の視野角[rad]とフレームの高さ[mm]から焦点距離[mm]を求める。
// 視野角が 0 以上 π 未満でない、またはフレームの高さが不正なときは kDefaultFocalLengthMm を返す。
double FocalLengthFromFovY(double fovYRadians, double frameHeightMm);

// 入力欄の段(文字列)のうち、value に対数で最も近いものの番号。変換できない段があるか、段が空なら -1。
int NearestPhotoStepIndex(workspace::CameraScreenParameter parameter, const std::vector<std::string>& steps,
                          double value);

// 焦点距離欄を focalMm[mm]へ合わせる。欄の段の最小〜最大に収め、欄の表示は整数 mm に丸める。
// 丸める前の値は欄の exactValue_ が持ち、MakePhotoCamera はそれを読む。書けなかったときは false。
bool SetFocalLengthMm(workspace::CameraScreenField& field, double focalMm);

// 焦点距離欄を、縦の視野角が fovStepRadians × notches だけ変わるように動かす(ホイール用)。
// notches が正で視野角が狭くなる(焦点距離が長くなる)。視野角は高さ frameHeightMm[mm]のフレームで求める。
// 段の最小〜最大で止まる。値が変わったら true。
bool StepFocalLengthByFov(workspace::CameraScreenField& field, double notches, double fovStepRadians,
                          double frameHeightMm);

// ファインダーを出るときの通常画面の視野角[rad]。入るときに入力欄へ書いた焦点距離 writtenFocalMm のままなら
// nullopt(視野角は元のまま)。ユーザーが変えていたら、その焦点距離を通常画面のフレームの高さで換算して
// [minFovYRadians, maxFovYRadians] に収めた値。
std::optional<float> FieldOfViewOnFinderExit(double writtenFocalMm, double currentFocalMm, float minFovYRadians,
                                             float maxFovYRadians);

} // namespace rs::app
