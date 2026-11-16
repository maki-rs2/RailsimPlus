#include "CameraScreenPlaceholder.h"

namespace rs::app::workspace {
CameraScreenPresentation MakeCameraScreenPlaceholder() {
    // 実際のカメラとは接続しない、画面の全項目を表示するための仮の値。
    CameraScreenPresentation screen;
    screen.fields_ = {{"焦点距離", "", "35", "35", "top-left", "mm"},
                      {"絞り", "F", "5.6", "5.6", "top-right", ""},
                      {"シャッター速度", "", "1/125", "1/125", "bottom-left", "秒"},
                      {"ISO", "", "100", "100", "bottom-center", ""},
                      {"フォーカス距離", "", "10", "10", "bottom-right", "m"}};
    const CameraScreenSteps& steps = CameraScreenSteps::Embedded();
    for (std::size_t i = 0; i < screen.fields_.size(); ++i) {
        CameraScreenField& field = screen.fields_[i];
        field.steps_ = steps.values_[i];
        for (std::size_t j = 0; j < field.steps_.size(); ++j) {
            if (field.steps_[j] == field.text_) field.step_ = static_cast<int>(j);
        }
        field.RefreshDigits();
    }
    screen.bookmarks_.rows_ = {{"仮の駅を望むカメラ"}, {"仮の橋を望むカメラ"}, {"仮の車両を望むカメラ"}};
    // 写真は画面をファインダー枠で切り抜くので、選べるのは縦横比だけ。画素数は画面の大きさで決まる。
    screen.resolutions_ = {{"16:9", 16.0, 9.0}, {"3:2", 3.0, 2.0}, {"4:3", 4.0, 3.0}, {"1:1", 1.0, 1.0}, {"9:16", 9.0, 16.0}};
    screen.SetResolution("0");
    return screen;
}
}
