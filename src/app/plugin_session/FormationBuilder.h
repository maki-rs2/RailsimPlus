//
// FormationBuilder.h - TrainPlugin名の列からPluginSelectorへFormationを構築する
//
// TrainGroupTemplateApplier(編成テンプレート取り込み)とRs2TrainApplier(.rs2
// TrainGroup取り込み)の両方が使う共通処理。
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rs::app {

class PluginSelector;

// Formation 1両ぶんの指定。
struct FormationCarSpec {
    std::string trainPlugin;  // Train/<trainPlugin> のフォルダ名(未解決)
    bool        reverse = false;
    // 保存済みのスイッチ確定値(DefineSwitch宣言順)。空なら全既定0。
    std::vector<int32_t> switchOptions;
};

// 編成(defIndex)の車両carIndexへ、反転とスイッチ確定値を当てる。checkSwitchCountが真なら、
// 確定値の件数とプラグイン側のDefineSwitch件数の食い違いを warnings へ記録する
// (プラグインを読めなかった車両は件数が分からないので偽にする)。folder は警告の文言に使う。
void ApplyFormationCarSpec(PluginSelector& selector, size_t defIndex, size_t carIndex, const std::string& folder,
                           bool reverse, const std::vector<int32_t>& switchOptions, bool checkSwitchCount,
                           std::vector<std::string>& warnings);

// folder を AddPath で編成(defIndex)の末尾へ1両足し、反転とスイッチ確定値を適用する。
// 保存済みスイッチ確定値の件数がプラグイン側のDefineSwitch件数と食い違う場合は
// warnings へ記録するが、確定値は適用する(位置対応がずれうるため警告のみ)。
// プラグインを読めなかった車両も、保存で参照が消えないよう未解決の車両
// (FormationCar::unresolved)として編成に足す。warnings には理由を記録し、
// 戻り値はfalseにする(Train種別でない場合は編成に足さずfalse)。
// 戻り値は、プラグインを読めて車両を足せたか。
bool AddFormationCar(PluginSelector& selector, size_t defIndex, const std::string& folder,
                     bool reverse, const std::vector<int32_t>& switchOptions,
                     std::vector<std::string>& warnings);

// installRootUtf8 配下 Train/<trainPlugin> を解決し、formationName の Formation を
// 新規追加して各車両を AddPath する。TrainPluginが空の車両は warnings へ記録して
// スキップする。読込に失敗した車両は未解決の車両として編成に残す(AddFormationCar)。
// outDefIndex が非nullptrなら作成した Formation の添字を書き込む。
// 戻り値はプラグインを読めた車両数(未解決の車両は数えない)。
size_t BuildFormationFromPlugins(PluginSelector& selector, const std::string& formationName,
                                   const std::vector<FormationCarSpec>& cars,
                                   const std::string& installRootUtf8,
                                   std::vector<std::string>& warnings, size_t* outDefIndex = nullptr);

} // namespace rs::app
