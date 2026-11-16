#pragma once

#include <nlohmann/json.hpp>

namespace rs::app {

// フィールド名ごとの丸め桁表に従って、JSONツリー内の浮動小数を丸めたコピーを返す。
// 桁表に無いキーの値は丸めない。配列の要素には、その配列を保持しているキーの桁を適用する。
nlohmann::json RoundRslJsonFields(const nlohmann::json& value);

}  // namespace rs::app
