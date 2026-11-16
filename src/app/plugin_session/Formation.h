#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "../../world/alignment/Guid.h"

namespace rs::app {

// 編成の1両。reversedは向き反転フラグ。frontLimit/tailLimitはAddPath時に
// def.trainInfoからキャッシュした連結位置[m](PlaceTrainRepeatingが車両ごとの
// 実長で間隔を計算するために使う)。
struct FormationCar {
    std::string folderUtf8;
    bool reversed = false;
    std::string displayName;
    double frontLimit = 0.0;
    double tailLimit = 0.0;
    // AddPath時にdef.trainInfoからキャッシュしたDoorClosingTime[実秒]。
    double doorClosingTime = 0.0;
    // 保存済みのスイッチ確定値(DefineSwitch宣言順、SceneEntity::switchOptionsと
    // 同じ意味)。空なら全既定0として扱う。
    std::vector<int32_t> switchOptions;
    // AddPath時にキャッシュしたプラグイン側のDefineSwitch宣言数。switchOptionsとの
    // 件数照合(位置対応ずれの検出)に使う。
    size_t pluginSwitchCount = 0;
    // プラグインを読めなかった車両(フォルダが無い・定義が壊れている)。保存で参照が消えないよう
    // 編成には残すが、連結位置などの寸法は持たないため、配置・走行・描画の対象にはしない。
    bool unresolved = false;
};

// 配置前の編成候補(1編成=車両の並び)。placedな編成インスタンス(SceneEntity::
// trainId)とは別概念なので"Group"ではなく"Def"と呼ぶ。
struct Formation {
    rs::world::Guid guid;
    std::string name;                 // 表示名(既定は "編成1", "編成2", ...)
    std::vector<FormationCar> cars;
};

} // namespace rs::app
