//
// NumberedName.h - 「接頭辞 + 番号」形式の名前を、空いている最小の番号で作る。
//
#pragma once

#include <string>
#include <string_view>
#include <unordered_set>

namespace rs::world {

// prefix に正の整数を付けた名前(例: 「閉塞 」→「閉塞 1」)のうち、usedNames に無い最小の
// 番号のものを返す。途中が欠けていればそこを埋める(1と3があれば2)。
std::string NextNumberedName(std::string_view prefix,
                             const std::unordered_set<std::string>& usedNames);

}  // namespace rs::world
