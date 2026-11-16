#include "FormationBuilder.h"

#include "PluginSelector.h"
#include "TrainGroupTemplateParse.h"
#include "../../plugins/layout/Rs2Import.h"

namespace rs::app {

void ApplyFormationCarSpec(PluginSelector& selector, size_t defIndex, size_t carIndex, const std::string& folder,
                           bool reverse, const std::vector<int32_t>& switchOptions, bool checkSwitchCount,
                           std::vector<std::string>& warnings) {
    if (reverse) {
        selector.SetFormationCarReversed(defIndex, carIndex, true);
    }
    if (switchOptions.empty()) return;
    selector.SetFormationCarSwitchOptions(defIndex, carIndex, switchOptions);
    if (!checkSwitchCount) return;
    // 保存済みスイッチ確定値の件数とプラグイン側のDefineSwitch件数が
    // 食い違うと、位置対応がずれて意図しない見た目になる。件数は読込時に
    // FormationCarへキャッシュ済みなので、ここで再読込はしない。
    const size_t pluginSwitchCount = selector.Formations()[defIndex].cars[carIndex].pluginSwitchCount;
    if (pluginSwitchCount != switchOptions.size()) {
        warnings.push_back(
            "スイッチ確定値の件数が一致しません(保存=" +
            std::to_string(switchOptions.size()) +
            " / プラグイン=" + std::to_string(pluginSwitchCount) + "): " + folder);
    }
}

bool AddFormationCar(PluginSelector& selector, size_t defIndex, const std::string& folder,
                     bool reverse, const std::vector<int32_t>& switchOptions,
                     std::vector<std::string>& warnings) {
    const size_t carsBefore = selector.SelectedFormationCars().size();
    std::string err;
    bool resolved = true;
    if (!selector.AddPath(folder, err)) {
        // 読めなかった車両も編成には残す。落とすと、次の保存で車両の参照が消える。
        warnings.push_back("読込失敗: " + folder + " (" + err + ")");
        if (defIndex >= selector.Formations().size()) return false;
        selector.AddUnresolvedFormationCar(defIndex, folder);
        resolved = false;
    } else if (selector.SelectedFormationCars().size() <= carsBefore) {
        // AddPathはTrain種別以外のプラグインでも「読み込み自体は成功」としてtrueを
        // 返す(該当プラグインを単一スロット等へ格納するため)が、その場合は編成の
        // 車両として追加されない。carsが実際に増えていなければ「TrainPlugin指定が
        // Train種別以外を指している」ケースなので、添字計算をせずスキップする
        // (未増加のままsize()-1を計算するとsize()==0の場合にSIZE_MAXへ反転し
        // vector添字が範囲外になる)。
        warnings.push_back("TrainPluginがTrain種別ではありません: " + folder);
        return false;
    }
    const size_t carIndex = selector.Formations()[defIndex].cars.size() - 1;
    ApplyFormationCarSpec(selector, defIndex, carIndex, folder, reverse, switchOptions, resolved, warnings);
    return resolved;
}

size_t BuildFormationFromPlugins(PluginSelector& selector, const std::string& formationName,
                                   const std::vector<FormationCarSpec>& cars,
                                   const std::string& installRootUtf8,
                                   std::vector<std::string>& warnings, size_t* outDefIndex) {
    const size_t defIndex = selector.AddFormation(formationName);
    if (outDefIndex) *outDefIndex = defIndex;

    size_t addedCount = 0;
    for (const FormationCarSpec& car : cars) {
        const std::string folder = ResolveTrainCarFolder(installRootUtf8, car);
        if (folder.empty()) {
            warnings.push_back("TrainPluginが空です");
            continue;
        }
        if (!AddFormationCar(selector, defIndex, folder, car.reverse, car.switchOptions, warnings)) {
            continue;
        }
        ++addedCount;
    }
    return addedCount;
}

} // namespace rs::app
