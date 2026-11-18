//
// SignalPluginRules.h - 信号プラグイン(station.jsonに`signal`の速度表を持つStation)の選択規則
//
// SceneRendererに依存しない純関数だけを置く(単体テストできる形)。信号を置く操作は
// SignalBlockAction がこの規則を通して行う。
//
#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "../../plugins/schema/sidecar/StationSidecar.h"
#include "../../world/alignment/SpeedSignal.h"

namespace rs::legacy_pi { struct SwitchDef; }

namespace rs::app {

// プラグインのstation.jsonを返す口。実体はSceneRenderer(SceneSignalSidecarSource)、
// テストではスタブを渡す。
class ISignalSidecarSource {
public:
    virtual ~ISignalSidecarSource() = default;
    virtual const rs::legacy_pi::sidecar::StationSidecar& Sidecar(const std::string& folderUtf8) = 0;
    // プラグインのスイッチ定義(DefineSwitch宣言順)。読めなければnullptr。
    virtual const std::vector<rs::legacy_pi::SwitchDef>* SwitchDefs(const std::string& folderUtf8) {
        (void)folderUtf8;
        return nullptr;
    }
};

// 信号プラグインが未選択のときの文言(置く操作の前提)。
inline constexpr const char* kSignalPluginNotSelectedText = "信号プラグインを選んでください";

// 速度表から選べる速度[km/h]の一覧。負の段は除かれ、0を含む整数の昇順。表が無ければ空。
std::vector<int> SignalSpeedChoices(const rs::legacy_pi::sidecar::StationSidecar& sidecar);

// 信号プラグインとして選べるか。選べるなら空文字、選べないなら日本語の理由を返す。
std::string ValidateSignalSidecar(const rs::legacy_pi::sidecar::StationSidecar& sidecar);

// 速度表の全段を信号の段にしたもの(置くときの初期値)。段の順序とsidecarIndexは
// BuildAspectsFromSignalTable/BuildAspectSourceIndicesに従う。表が無ければ空。
std::vector<rs::world::SignalAspectStage> SignalStagesFromSidecar(
    const rs::legacy_pi::sidecar::StationSidecar& sidecar);

// stagesにまだ無い、速度表の速度[km/h]の昇順一覧(段を足す候補)。
std::vector<int> SignalAddableSpeeds(const rs::legacy_pi::sidecar::StationSidecar& sidecar,
                                     const std::vector<rs::world::SignalAspectStage>& stages);

// stagesへ、速度表の速度speedKmhの段を足した段(速度の昇順。足した段のsidecarIndexは表から引く)。
// 表に無い速度・すでにある速度ならnullopt。
std::optional<std::vector<rs::world::SignalAspectStage>> SignalStagesWithAdded(
    const rs::legacy_pi::sidecar::StationSidecar& sidecar,
    const std::vector<rs::world::SignalAspectStage>& stages, int speedKmh);

// stagesから速度speedKmhの段を外した段。先頭の0の段は外せない。無い速度ならnullopt。
std::optional<std::vector<rs::world::SignalAspectStage>> SignalStagesWithRemoved(
    const std::vector<rs::world::SignalAspectStage>& stages, int speedKmh);

// 現示に応じて信号モデルへ書くスイッチ割り当て。displayStageはstagesの添字(nullopt=状態なし)。
// 段kなら stages[k].sidecarIndex の要素の割り当て。状態なし・担当閉塞なし・段が範囲外・
// 要素番号が表の外のときは、消灯の要素(FindDarkStageSourceIndex)の割り当て。
// 消灯の要素が表に無い、または表が使えないときは割り当て無し(呼び出し側は全スイッチを既定値0にする)。
// 赤(段0)を消灯の代わりには出さない。
std::optional<rs::legacy_pi::sidecar::SwitchAssignment> SignalAspectSwitchAssignment(
    const rs::world::SpeedSignal& signal, const rs::legacy_pi::sidecar::StationSidecar& sidecar,
    std::optional<int> displayStage);

// 現示で決まる(エンジンが制御する)スイッチ名の集合。station.jsonのsignal表のどれかの要素が
// 書き込むスイッチ名すべて。表が使えないときは空。エンジン制御かの判定はここ1か所に集約する。
std::set<std::string> SignalEngineControlledSwitchNames(
    const rs::legacy_pi::sidecar::StationSidecar& sidecar);

// switchDefs(DefineSwitch宣言順)の添字ごとに、エンジン制御のスイッチか。同名・空名のスイッチが
// あるので名前ではなく添字で返す。割り当てが実際に書く宣言(同名なら最初の1つ。空名と"_"始まりは書かれない)だけtrue。
std::vector<bool> SignalEngineControlledFlags(const rs::legacy_pi::sidecar::StationSidecar& sidecar,
                                              const std::vector<rs::legacy_pi::SwitchDef>& switchDefs);

// スイッチ行に出す、エンジン制御で編集できない行の文言。
inline constexpr const char* kSignalSwitchLinkedToBlockText = "閉塞に連動中";

// フォルダの末尾の名前(表示用)。空なら空。
std::string SignalPluginDisplayName(const std::string& folderUtf8);

} // namespace rs::app
