//
// CompatScan.h - 手持ち資産の一括パース(compat_test)の実行部
//
// ルート配下の全プラグインフォルダと全 .rs2 を既存のローダ/スキーマで読み、
// CompatRecord に落として CompatReport で集計・判定する(ウィンドウ/デバイス不要)。
//
#pragma once

#include <string>

namespace rs::app {

// 集計を標準出力へ出す。REQUIREMENTS §3 の基準を満たさない種別があれば 1、
// 満たす(または資産なし)なら 0 を返す。
int RunCompatTest(const std::string& rootUtf8);

}  // namespace rs::app
