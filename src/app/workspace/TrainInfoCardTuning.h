#pragma once

namespace rs::app::workspace {

// 編成情報カードの調整値。assets/config/train_info_card_tuning.json から読む。
struct TrainInfoCardTuning {
    // カメラから先頭車までがこの距離[m]より遠い編成はカードを出さない。遠くの編成のカードが
    // 画面に散らばらないための上限で、実機で触って調整する値。
    double maxDistanceM = 500.0;

    static const TrainInfoCardTuning& Embedded();
};

} // namespace rs::app::workspace
