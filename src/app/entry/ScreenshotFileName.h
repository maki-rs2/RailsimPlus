//
// ScreenshotFileName.h - F12スクリーンショットの保存ファイル名(純関数)
//
// 名前は railsimplus_YYYYMMDD_HHMMSS.png。同名があれば _2, _3 ... を付ける。
// 時刻の取得とファイルの存在確認は呼び出し側が持ち、ここはどちらにも依存しない。
//
#pragma once

#include <ctime>
#include <functional>
#include <string>

namespace rs::app {

// suffixが2以上なら "_<suffix>" を付ける(0・1は付けない)。
std::string FormatScreenshotFileName(const std::tm& localTime, int suffix);

// 存在しない最初のファイル名を返す。existsは名前(フォルダを含まない)を受けて存在を答える。
std::string ChooseScreenshotFileName(const std::tm& localTime,
                                     const std::function<bool(const std::string&)>& exists);

} // namespace rs::app
