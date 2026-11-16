//
// KeyRepeatStepper.h - キー長押しの連続送りの歩数を数える
//
// 押した瞬間に1歩、PlacementHeightTuningのrepeatDelaySeconds後から
// repeatStepsPerSecondで連続送りする。歩数は経過時間の累計から整数で数えるので、
// フレームの区切り方によらず同じ時間なら同じ歩数になる。
// 高さ調整(PlacementHeightAction)と情景の向き回転(SceneryYawAction)で共用する。
// 編成情報カードの速度の微調整は、速さを指定する版を使う。
//
#pragma once

namespace rs::app {

// 連続送りの速さ。待ち時間(最初の1歩から連続送りに入るまで)と、連続送りの歩数/秒。
struct KeyRepeatTiming {
    double delaySeconds = 0.0;
    double stepsPerSecond = 0.0;
};

class KeyRepeatStepper {
public:
    // 1フレーム分の更新。directionは+1/-1/0(押していない)。符号付きの歩数を返す。
    // 速さはPlacementHeightTuningの値(高さ調整・情景の向き回転と同じ)。
    int Update(int direction, double dtSeconds);
    // 速さを指定する版。
    int Update(int direction, double dtSeconds, const KeyRepeatTiming& timing);

    // 押下状態を捨てる(次に押した瞬間がまた1歩になる)。
    void Reset();

private:
    int direction_ = 0;
    double heldSeconds_ = 0.0;
    long long repeatStepsDone_ = 0;
};

} // namespace rs::app
