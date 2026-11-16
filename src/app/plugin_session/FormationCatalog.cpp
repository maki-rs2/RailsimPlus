#include "FormationCatalog.h"

#include <algorithm>
#include <string_view>
#include <utility>

namespace rs::app {

size_t FormationCatalog::AddFormation(const std::string& name, rs::world::Guid guid) {
    Formation def;
    def.guid = rs::world::GuidIsNil(guid) ? rs::world::GenerateGuid() : guid;
    def.name = name.empty() ? ("編成" + std::to_string(formations_.size() + 1)) : name;
    formations_.push_back(std::move(def));
    selectedFormationIndex_ = formations_.size() - 1;
    return selectedFormationIndex_;
}

bool FormationCatalog::IsAutoFormationName(const std::string& name) {
    constexpr std::string_view kPrefix = "編成";
    if (name.size() <= kPrefix.size() || !name.starts_with(kPrefix)) return false;
    return std::all_of(name.begin() + static_cast<ptrdiff_t>(kPrefix.size()), name.end(),
                       [](char c) { return c >= '0' && c <= '9'; });
}

std::optional<size_t> FormationCatalog::DuplicateFormation(size_t index) {
    if (index >= formations_.size()) return std::nullopt;
    Formation copy = formations_[index];
    copy.guid = rs::world::GenerateGuid();
    copy.name += " のコピー";
    const size_t copyIndex = index + 1;
    formations_.insert(formations_.begin() + static_cast<ptrdiff_t>(copyIndex), std::move(copy));
    selectedFormationIndex_ = copyIndex;
    return copyIndex;
}

void FormationCatalog::RemoveFormation(size_t index) {
    if (index >= formations_.size()) return;
    formations_.erase(formations_.begin() + static_cast<ptrdiff_t>(index));
    if (formations_.empty()) {
        selectedFormationIndex_ = 0;
    } else if (selectedFormationIndex_ >= formations_.size()) {
        selectedFormationIndex_ = formations_.size() - 1;
    }
}

void FormationCatalog::SetFormationName(size_t index, const std::string& name) {
    if (index < formations_.size()) formations_[index].name = name;
}

void FormationCatalog::SelectFormation(size_t index) {
    if (index < formations_.size()) selectedFormationIndex_ = index;
}

void FormationCatalog::RemoveFormationCar(size_t defIndex, size_t carIndex) {
    if (defIndex >= formations_.size()) return;
    std::vector<FormationCar>& cars = formations_[defIndex].cars;
    if (carIndex < cars.size()) cars.erase(cars.begin() + static_cast<ptrdiff_t>(carIndex));
}

void FormationCatalog::RestoreFormationCar(size_t defIndex, size_t position, FormationCar car) {
    if (defIndex >= formations_.size()) return;
    std::vector<FormationCar>& cars = formations_[defIndex].cars;
    cars.insert(cars.begin() + static_cast<ptrdiff_t>(std::min(position, cars.size())), std::move(car));
}

void FormationCatalog::ReplaceFormationCar(size_t defIndex, size_t carIndex, FormationCar car) {
    if (defIndex >= formations_.size()) return;
    std::vector<FormationCar>& cars = formations_[defIndex].cars;
    if (carIndex < cars.size()) cars[carIndex] = std::move(car);
}

void FormationCatalog::MoveFormationCarTo(size_t defIndex, size_t from, size_t to) {
    if (defIndex >= formations_.size()) return;
    std::vector<FormationCar>& cars = formations_[defIndex].cars;
    if (from >= cars.size() || to >= cars.size() || from == to) return;
    FormationCar moved = std::move(cars[from]);
    cars.erase(cars.begin() + static_cast<ptrdiff_t>(from));
    cars.insert(cars.begin() + static_cast<ptrdiff_t>(to), std::move(moved));
}

void FormationCatalog::SetFormationCarReversed(size_t defIndex, size_t carIndex, bool reversed) {
    if (defIndex >= formations_.size()) return;
    std::vector<FormationCar>& cars = formations_[defIndex].cars;
    if (carIndex < cars.size()) cars[carIndex].reversed = reversed;
}

void FormationCatalog::SetFormationCarSwitchOptions(size_t defIndex, size_t carIndex,
                                              std::vector<int32_t> switchOptions) {
    if (defIndex >= formations_.size()) return;
    std::vector<FormationCar>& cars = formations_[defIndex].cars;
    if (carIndex < cars.size()) cars[carIndex].switchOptions = std::move(switchOptions);
}

void FormationCatalog::MoveFormationCar(size_t defIndex, size_t carIndex, int delta) {
    if (defIndex >= formations_.size()) return;
    if (delta != -1 && delta != 1) return;
    std::vector<FormationCar>& cars = formations_[defIndex].cars;
    if (carIndex >= cars.size()) return;
    const ptrdiff_t targetSigned = static_cast<ptrdiff_t>(carIndex) + delta;
    if (targetSigned < 0 || targetSigned >= static_cast<ptrdiff_t>(cars.size())) return;
    const size_t target = static_cast<size_t>(targetSigned);
    std::swap(cars[carIndex], cars[target]);
}

const std::vector<FormationCar>& FormationCatalog::SelectedFormationCars() const {
    static const std::vector<FormationCar> kEmpty;
    if (selectedFormationIndex_ >= formations_.size()) return kEmpty;
    return formations_[selectedFormationIndex_].cars;
}

} // namespace rs::app
