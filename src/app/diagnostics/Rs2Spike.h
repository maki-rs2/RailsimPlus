//
// Rs2Spike.h - .rs2 読込の常設ヘッドレス診断
//
// 生ダンプ・Rs2Schema件数集計・Rs2Importグラフ統計・trackPlugins/配置物の解決率を出力する。
//
#pragma once

#include <string>

namespace rs::app {

// ヘッドレス: .rs2 の SceneList>Scene 配下 RailConnector/RailWay を数件ダンプし、
// 型付け・取り込み・プラグイン解決の統計を報告する
int RunRs2Spike(const std::string& utf8Path);

} // namespace rs::app
