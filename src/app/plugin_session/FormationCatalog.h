#pragma once

#include <optional>
#include "Formation.h"

namespace rs::app {

// 配置前の編成定義と選択を所有する。プラグインの読込状態とは独立に編集できる。
class FormationCatalog {
public:
    // 空の名前は件数から採番する。追加・複製した定義を選択状態にする。
    size_t AddFormation(const std::string& name, rs::world::Guid guid = {});
    // 車両の並び・向き・スイッチ値を複写し、新しいGUIDで元の直後へ挿入する。
    std::optional<size_t> DuplicateFormation(size_t index);
    // 削除後の選択添字は有効範囲へ収める。空なら0にする。
    void RemoveFormation(size_t index);
    void SetFormationName(size_t index, const std::string& name);
    // 名前を付けずに作ったときの名前("編成" + 番号)か。先頭車の名前へ付け替えてよいかの判定に使う。
    static bool IsAutoFormationName(const std::string& name);
    void SelectFormation(size_t index);
    void RemoveFormationCar(size_t defIndex, size_t carIndex);
    // 削除の取り消しでは車両の全情報を戻す。位置が末尾を超えたら末尾へ入れる。
    void RestoreFormationCar(size_t defIndex, size_t position, FormationCar car);
    void ReplaceFormationCar(size_t defIndex, size_t carIndex, FormationCar car);
    // toはfromを抜いた後の並びでの位置。範囲外は変更しない。
    void MoveFormationCarTo(size_t defIndex, size_t from, size_t to);
    void SetFormationCarReversed(size_t defIndex, size_t carIndex, bool reversed);
    void SetFormationCarSwitchOptions(size_t defIndex, size_t carIndex, std::vector<int32_t> switchOptions);
    void MoveFormationCar(size_t defIndex, size_t carIndex, int delta);
    const std::vector<Formation>& Formations() const { return formations_; }
    size_t SelectedFormationIndex() const { return selectedFormationIndex_; }
    const std::vector<FormationCar>& SelectedFormationCars() const;

private:
    std::vector<Formation> formations_;
    size_t selectedFormationIndex_ = 0;
};

} // namespace rs::app
