#pragma once

// LoadPluginFolder()の実装を複数TUへ分割するための内部専用ヘッダ。
// PluginLoader.h(広く波及する公開ヘッダ)には入れず、PluginLoader.cpp /
// PluginLoadProfile.cpp / PluginPartModels.cpp の3TUだけがincludeする。

#include <chrono>
#include <map>
#include <set>
#include <string>
#include <utility>

#include "PluginLoader.h"

namespace rs::legacy_plugin {

using Clock = std::chrono::steady_clock;

inline double ProfMs(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

// [profile] 計測専用。フェーズ別所要をlp.logへ。
struct LoadProfile {
    double parseMs = 0.0;    // 定義txtのパース
    double assimpMs = 0.0;   // 主要パーツループの.xパース累計
    double pbrMs = 0.0;      // 同ループのApplyPbr累計
    int    assimpCount = 0;
    double variantMs = 0.0;  // ChangeModel候補(LoadModelVariant)の読込累計
    int    variantCount = 0;
    double variantXMs = 0.0;   // 候補読込のうち .x ロード分
    double variantPbrMs = 0.0; // 同 ApplyPbr 分
    // 候補の重複度。ApplyPbrはpart依存なので、無償で削れるのは同一パーツ内の
    // 重複(dedupable)だけ。パーツ横断のuniqは削減幅の上限にすぎない。
    std::set<std::string> variantPaths;
    int variantDedupable = 0;
    // [profile] ChangeModel全候補を常駐させた場合の上限見積り用。
    // パス(重複排除済み)ごとの頂点数/インデックス数。
    std::map<std::string, std::pair<std::size_t, std::size_t>> variantPathStats;
    double animPathMs = 0.0; // アニメ用テクスチャパス解決
    double assemblyMs = 0.0; // 全パーツ合成メッシュの構築
    double appendMs = 0.0;   // レール断面/Interval/Pier の追加読込

    // LoadPartModels() 内のChangeModel候補読込に伴う計測簿記(重複度・
    // variantPathStats)を1パーツ分まとめて記録する。variantMs自体は呼び出し
    // 側(計測区間の始点が呼び出し側のtVariant0にあるため)で加算する。
    void RecordVariant(const rs::legacy_pi::ModelPart& part, const PartDynamicModel& dyn);
};

// [profile] フェーズ別内訳を出力(計測専用。環境変数 RS_PROFILE_LOAD 指定時のみ)。
void LogLoadProfile(LoadedPlugin& lp, const LoadProfile& prof, double totalMs);

// 各パーツのモデルを読み PBR を適用
void LoadPartModels(LoadedPlugin& lp, const std::wstring& baseDir, LoadProfile& prof);

} // namespace rs::legacy_plugin
