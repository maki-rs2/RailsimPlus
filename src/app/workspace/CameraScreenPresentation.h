#pragma once

#include <optional>
#include <string_view>
#include <utility>

#include "CameraFinder.h"
#include "CameraScreenSteps.h"
#include "ScenePanelPresentation.h"

namespace rs::app::workspace {
struct CameraScreenSegment {
    int index_ = 0;
    bool on_ = false;
};
struct CameraScreenDigit {
    bool narrow_ = false;
    std::vector<CameraScreenSegment> segments_;
};
struct CameraScreenField {
    std::string label_;
    std::string prefix_;
    std::string text_;
    std::string draft_;
    std::string placement_;
    std::string unit_; // 入力欄の右に出す単位(無ければ空)
    std::vector<std::string> steps_;
    int step_ = 0;
    std::vector<CameraScreenDigit> digits_;
    // 自動(text_ が AUTO)のときに7セグへ出す、決まった値の文字列。空なら基準の値を出す。
    std::string autoDisplay_;
    bool IsAuto() const { return text_ == kCameraScreenAutoText; }
    // 自動の表示値を変える。変わったときだけ数字を作り直してtrue。
    bool SetAutoDisplay(std::string_view value);
    void RefreshDigits();
    bool Wheel(double delta);
    // 入力した文字列を値にする。段の中のどの値よりも長いとき(表示の幅に収まらないとき)は変えずにfalse。
    bool SetText(std::string_view value);
};
struct CameraScreenResolution {
    std::string label_;
    double width_ = 0.0;
    double height_ = 0.0;
};
struct CameraScreenPresentation {
    // ファインダーに重ねる三分割の線と水平線を出すか(ユーザー設定の値を写す)。
    bool finderGrid_ = true;
    bool finderHorizon_ = true;
    double rollDegrees_ = 0.0;
    // 傾きのキーを押している最中か。水平線はこの間だけ出す。
    bool rolling_ = false;
    bool active_ = false;
    std::vector<CameraScreenField> fields_;
    // 一覧の名前編集は、シーン一覧と共通のUI状態を使う。
    ScenePanelPresentation bookmarks_;
    std::vector<CameraScreenResolution> resolutions_;
    int resolution_ = -1;
    CameraFinderRect finder_;
    std::vector<CameraFinderRect> masks_;
    void RequestPhoto();
    bool BeginPhotoCapture(bool otherCaptureRequested);
    void CompletePhoto() { photoRequested_ = photoReady_ = photoCapturing_ = false; }
    // フォーカスを合わせた点の印(画面のピクセル座標で、印の中心)。ファインダーが変わったら消す。
    bool focusShown_ = false;
    double focusX_ = 0.0;
    double focusY_ = 0.0;
    // クリックでフォーカス: ファインダーの中の点(左上を0、右下を1とする割合)の奥行きを読む要求。描画側が1回だけ取り出す。
    std::optional<std::pair<double, double>> focusProbe_;
    bool SetResolution(std::string_view index);
    // 画面の点(x, y)がファインダーの中なら印をそこへ置いてtrue。外なら何もしない。
    bool FocusAt(double x, double y);
    void Layout(double screenWidth, double screenHeight, const CameraFinderRect& available);
private:
    bool photoRequested_ = false;
    bool photoReady_ = false;
    bool photoCapturing_ = false;
};
}
