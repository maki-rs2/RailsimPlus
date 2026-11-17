//
// SwitchList.h - スイッチ一覧(DefineSwitchの行)の共通部品
//
// 単体表示のスイッチと配置個体のスイッチで、行の型・値の丸め・書き込み先の
// インターフェースを共有する。違いは値の書き込み先(SwitchValueWriter)だけ。
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace rs::legacy_pi { struct SwitchDef; }


namespace rs::app {

struct SwitchRowViewModel {
    std::string name;
    std::vector<std::string> entries;
    std::int32_t value = 0;
    // 閉塞の現示で決まるスイッチ(信号モデルのみ)。編集させず、値も出さない。
    bool linkedToBlock = false;
};

// 一覧での行の出し方。選択肢の無いスイッチは RailSim II では値を持たない空のスイッチで、プラグイン作者は
// 一覧の見出しや区切りの空きとして使うことが多い。そのため値の入力は出さず、名前があれば見出し、
// 名前が空白だけなら空きにする。
enum class SwitchRowKind { Choice, Header, Spacer };

// 行の出し方を決める(ImGui版・RmlUi版の両方がこれを使う)。
SwitchRowKind GetSwitchRowKind(const SwitchRowViewModel& row);

// 選択肢があれば [0, entries.size()-1] に丸める。選択肢が空ならそのまま返す。
std::int32_t ClampSwitchValue(const rs::legacy_pi::SwitchDef& def, std::int32_t value);

// defsの各行からViewModelを作る。valueはcurrentValueの戻り値をClampSwitchValueしたもの。
std::vector<SwitchRowViewModel> BuildSwitchRows(
    const std::vector<rs::legacy_pi::SwitchDef>& defs,
    const std::function<std::int32_t(std::size_t index, const rs::legacy_pi::SwitchDef& def)>&
        currentValue);

// スイッチ値の書き込み先。行は宣言順の添字で指す(空名・同名のスイッチがあるため、名前では受けない)。
class SwitchValueWriter {
public:
    virtual ~SwitchValueWriter() = default;
    virtual void SetSwitch(std::size_t index, std::int32_t value) = 0;
};

} // namespace rs::app
