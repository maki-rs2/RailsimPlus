#include "InputBindingOverrides.h"

#include <algorithm>
#include <iterator>

namespace rs::app::input {
namespace {

std::vector<InputBinding> RowsOf(const std::vector<InputBinding>& bindings, InputAction action) {
    std::vector<InputBinding> rows;
    std::copy_if(bindings.begin(), bindings.end(), std::back_inserter(rows),
                 [action](const InputBinding& binding) { return binding.action == action; });
    return rows;
}

// actionの行をreplacementに差し替える。元の行の位置に順に置き換え、余った行は元の最後の行の直後に入れる
// (既定と同じ並びを保つ)。
std::vector<InputBinding> ReplaceRows(const std::vector<InputBinding>& bindings, InputAction action,
                                      const std::vector<InputBinding>& replacement) {
    std::vector<InputBinding> result;
    std::size_t used = 0;
    std::optional<std::size_t> after;
    for (const InputBinding& binding : bindings) {
        if (binding.action != action) {
            result.push_back(binding);
            continue;
        }
        if (used < replacement.size()) result.push_back(replacement[used++]);
        after = result.size();
    }
    const std::size_t position = after.value_or(result.size());
    result.insert(result.begin() + static_cast<std::ptrdiff_t>(position),
                  replacement.begin() + static_cast<std::ptrdiff_t>(used), replacement.end());
    return result;
}

// 既定の行から、主キーだけをchordに替えたactionの行を作る。
std::vector<InputBinding> RowsFor(const RebindableAction& entry, const KeyChord& chord) {
    const std::optional<InputBinding> base = FindPrimaryBinding(DefaultInputBindings(), entry.action);
    if (!base.has_value()) return {};
    InputBinding primary = *base;
    primary.key = chord.key;
    primary.ctrl = chord.modifiers.ctrl;
    primary.shift = chord.modifiers.shift;
    primary.alt = chord.modifiers.alt;
    if (chord.key == InputKey::None) {
        // 未割当。修飾は持たず、Ctrlダッシュ行も作らない。
        primary.ctrl = primary.shift = primary.alt = false;
        return {primary};
    }
    std::vector<InputBinding> rows{primary};
    if (entry.ctrlDashFollows) {
        InputBinding dash = primary;
        dash.ctrl = true;
        dash.shift = false;
        dash.alt = false;
        if (!(dash == primary)) rows.push_back(dash);
    }
    return rows;
}

} // namespace

const RebindableAction* FindRebindableAction(InputAction action) {
    const auto found = std::find_if(std::begin(kRebindableActions), std::end(kRebindableActions),
                                    [action](const RebindableAction& entry) { return entry.action == action; });
    return found == std::end(kRebindableActions) ? nullptr : &*found;
}

std::optional<InputBinding> FindPrimaryBinding(const std::vector<InputBinding>& bindings, InputAction action) {
    for (const InputBinding& binding : bindings) {
        if (binding.action == action) return binding;
    }
    return std::nullopt;
}

std::vector<InputBinding> ReassignBinding(const std::vector<InputBinding>& bindings, InputAction action,
                                          const KeyChord& chord) {
    const RebindableAction* entry = FindRebindableAction(action);
    if (entry == nullptr) return bindings;
    return ReplaceRows(bindings, action, RowsFor(*entry, chord));
}

std::vector<InputBinding> ResetBinding(const std::vector<InputBinding>& bindings, InputAction action) {
    if (FindRebindableAction(action) == nullptr) return bindings;
    return ReplaceRows(bindings, action, RowsOf(DefaultInputBindings(), action));
}

std::vector<InputBinding> BuildInputBindings(const std::string& overridesText) {
    std::vector<InputBinding> bindings = DefaultInputBindings();
    const std::vector<InputBinding> saved = ParseInputBindings(overridesText);
    for (const RebindableAction& entry : kRebindableActions) {
        const std::optional<InputBinding> first = FindPrimaryBinding(saved, entry.action);
        if (!first.has_value()) continue;
        bindings = ReplaceRows(bindings, entry.action,
                               RowsFor(entry, {first->key, {first->ctrl, first->shift, first->alt}}));
    }
    return bindings;
}

std::string SerializeInputOverrides(const std::vector<InputBinding>& bindings) {
    const std::vector<InputBinding> defaults = DefaultInputBindings();
    std::vector<InputBinding> changed;
    for (const RebindableAction& entry : kRebindableActions) {
        const std::vector<InputBinding> rows = RowsOf(bindings, entry.action);
        if (rows == RowsOf(defaults, entry.action)) continue;
        changed.insert(changed.end(), rows.begin(), rows.end());
    }
    return SerializeInputBindings(changed);
}

} // namespace rs::app::input
