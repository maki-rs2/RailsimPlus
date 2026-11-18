//
// SignalBlockAction.h - 信号・閉塞パネルのアプリケーション操作
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "../../editor/tool/SpeedSignalOwnerCandidate.h"
#include "../../world/alignment/WarpLinkTable.h"
#include "SignalAspectSource.h"
#include "SignalPluginRules.h"
#include "SwitchList.h"
#include "../workspace/RailLabelPresentation.h"
#include "../../world/alignment/Guid.h"
#include "../../world/alignment/SpeedSignal.h"

namespace rs::world {
struct TrackGraph;
} // namespace rs::world

namespace rs::editor { class Editor; }

namespace rs::app {

// 置けない理由の日本語の文。Noneは空文字を返す。
const char* SpeedSignalPlacementText(rs::world::SpeedSignalPlacementError error);

// 信号の現示の表示文(例「現示: 段2(45 km/h)」)。ImGui版とRmlUi版で共用する。
// 状態がある(走行中に計算済み)信号は、強制R・在線中・段kのいずれかと速度。
// 状態が無い信号は最上段の速度、stagesが空なら制限なし。
std::string SignalAspectDisplayText(const rs::sim::SignalAspectRuntime& runtime,
                                    const rs::world::SpeedSignal& signal);

// 信号の種別の日本語名(主信号・中継信号・遠方信号・入換信号)。
const char* SpeedSignalKindText(rs::world::SpeedSignalKind kind);

struct SignalBlockStageViewModel {
    int speedKmh = 0;
    int sidecarIndex = 0;
};

struct SignalBlockSignalRowViewModel {
    rs::world::Guid guid = rs::world::NilGuid();
    int direction = +1;  // edgeのs軸に対する向き(+1=順、-1=逆)
    rs::world::SpeedSignalKind kind = rs::world::SpeedSignalKind::Main;
    std::string pluginName;  // 信号プラグインのフォルダ末尾(未設定なら空)
    std::vector<SignalBlockStageViewModel> stages;  // 信号の段(速度の昇順。先頭の0は外せない)
    std::vector<int> addableSpeeds;  // 足せる段の速度[km/h]。プラグインが無い/読めない信号は空
    bool forcedRed = false;          // 手動の強制R(実行時だけの状態)
    bool entryPermitted = false;     // 手動の進入許可(実行時だけの状態)
    std::string aspectText;          // 現示の表示文(SignalAspectDisplayText)
};

struct SignalBlockSelectedBlockViewModel {
    bool visible = false;
    std::size_t edgeCount = 0;
    std::vector<SignalBlockSignalRowViewModel> signals;
};

struct SignalBlockViewModel {
    std::optional<workspace::RailLabelAnchor> candidateAnchor;
    std::vector<workspace::RailLabelCandidate> ownerCandidates;
    rs::editor::SignalBlockArmedAction armedAction = rs::editor::SignalBlockArmedAction::None;
    // 「閉塞にする」を押せるか(どの閉塞も選んでおらず、レールを選んでいる)と、選んでいるレールの本数。
    bool canCreateBlockFromSelection = false;
    size_t selectedEdgeCount = 0;
    SignalBlockSelectedBlockViewModel selectedBlock;
    // 閉塞に属さない表示専用の信号(中継・遠方・入換)。
    std::vector<SignalBlockSignalRowViewModel> displayOnlySignals;
    // 信号の配置待ち中に、ホバー位置へ置けない理由。置ける/対象なしのときは空。
    std::string placeBlockedMessage;
    // 選択中の信号プラグイン(未選択なら空)とその表示名、直前の選択が拒否された理由(無ければ空)。
    std::string signalPluginFolder;
    std::string signalPluginName;
    std::string signalPluginError;
};

// PlaceSpeedSignalAtの結果。pluginNotSelectedなら何も置いていない(errorはNone)。
struct SignalPlaceOutcome {
    bool placed = false;
    bool pluginNotSelected = false;
    rs::world::SpeedSignalPlacementError error = rs::world::SpeedSignalPlacementError::None;
};

std::vector<rs::editor::SpeedSignalOwnerCandidate> SpeedSignalOwnerCandidates(
    const rs::world::TrackGraph& graph, const rs::world::Guid& edge, double s, int direction,
    rs::world::SpeedSignalKind kind, const rs::world::WarpLinkTable* warpLinks = nullptr,
    std::size_t sceneIndex = 0);

class SignalBlockAction {
public:
    SignalBlockAction(rs::editor::Editor& editor, ISignalSidecarSource& sidecarSource,
                      ISignalAspectSource& aspectSource);
    void SelectBlock(rs::world::Guid blockGuid);
    void RenameBlock(rs::world::Guid blockGuid, const std::string& name);
    void RemoveBlock(rs::world::Guid blockGuid);
    // どの閉塞も選んでおらず、エディタでレールを選んでいるときに、選択中のレールを範囲とする閉塞を
    // 作って選ぶ(Undo対象)。作れなければfalse。
    bool CreateBlockFromSelection();
    // CreateBlockFromSelectionで閉塞を作れる状態か(どの閉塞も選んでおらず、レールを選んでいる)。
    bool CanCreateBlockFromSelection() const;
    // 閉塞を作るボタンを画面に置く位置(閉塞に入れる最初のレールの中ほど)。作れないとき・入れるレールが無いときはfalse。
    bool BlockCreateAnchor(rs::world::Guid& outEdge, double& outS) const;
    void Disarm();

    // 信号プラグイン(editor.signalPluginFolderの唯一の書き込み口)を選ぶ。station.jsonの速度表を
    // 検証し、通ったときだけフォルダを入れてエラーを消す。落ちたら理由を日本語で返し、
    // フォルダは変えない(理由はeditor.signalPluginErrorにも残す)。通れば空文字を返す。
    std::string SelectSignalPlugin(const std::string& folderUtf8);

    // 信号の配置待ちを始める。信号プラグインが未選択なら配置待ちにしない。
    void ArmPlaceSpeedSignal();
    // 仮止め中は位置と向きを保ち、検証結果だけ更新する。それ以外はedgeがNilか、
    // 信号プラグインが未選択なら対象なし。向きは、最後に置いた信号・駅舎アンカーからつながりを
    // たどって決め、reverse(Shiftを押している間)がtrueなら逆にする(Editor::ResolvePlaceTravelDirection)。
    void UpdateSpeedSignalHover(const rs::world::Guid& edge, double s, bool reverse,
                                const rs::world::WarpLinkTable* warpLinks = nullptr,
                                std::size_t sceneIndex = 0);
    // クリック位置で担当候補が複数なら仮止めし、それ以外は即配置する。空き地は仮止めを取り消す。
    SignalPlaceOutcome BeginSpeedSignalPlacement(const rs::world::Guid& edge, double s, bool reverse,
                                          const rs::world::WarpLinkTable* warpLinks = nullptr,
                                          std::size_t sceneIndex = 0);
    SignalPlaceOutcome ChooseSpeedSignalOwner(const rs::world::Guid& block,
                                          const rs::world::WarpLinkTable* warpLinks = nullptr,
                                          std::size_t sceneIndex = 0);
    // 指定した担当閉塞で検証し、置けるときだけUndo可能コマンドで追加して配置待ちを解除する。
    SignalPlaceOutcome PlaceSpeedSignalAt(const rs::world::Guid& edge, double s,
                                          const rs::world::Guid& block, int direction = +1,
                                          const rs::world::WarpLinkTable* warpLinks = nullptr,
                                          std::size_t sceneIndex = 0);
    void RemoveSpeedSignal(const rs::world::Guid& signalGuid);
    // 信号の向きを+1(順)か-1(逆)にする(Undo対象)。向きを変えても置ける(同じ向きの信号が閉塞に無い)ときだけ
    // 変え、置けないときはその理由を返す。向きが同じ・信号が無いときは何もせずNone。
    rs::world::SpeedSignalPlacementError SetSpeedSignalDirection(const rs::world::Guid& signalGuid, int direction,
        const rs::world::WarpLinkTable* warpLinks = nullptr, std::size_t sceneIndex = 0);
    // 信号の段を足す・外す(Undo対象)。足せるのはその信号のプラグインの速度表にある、まだ無い速度だけ。
    // 先頭の0の段は外せない。プラグイン未設定・条件外は何もしない。
    void AddSpeedSignalStage(const rs::world::Guid& signalGuid, int speedKmh);
    void RemoveSpeedSignalStage(const rs::world::Guid& signalGuid, int speedKmh);
    // 信号モデルのスイッチ(DefineSwitch宣言順の添字index)を選ぶ(Undo対象)。値は選択肢の範囲に丸める。
    // 値が変わらない・信号やスイッチ定義が無い・現示で決まるスイッチへの書き込みは何もしない。
    void SetSpeedSignalSwitch(const rs::world::Guid& signalGuid, std::size_t index, std::int32_t value);
    // 信号モデルのスイッチ行(DefineSwitch宣言順)。valueは信号のswitchOptions(ユーザーの値)で、
    // 現示で決まるスイッチはlinkedToBlockが立つ。信号やプラグインが無い・定義が読めないときは空。
    // プラグイン設定パネル(鑑賞モードで信号をクリック)の一覧の元で、書き込みはSetSpeedSignalSwitch。
    std::vector<SwitchRowViewModel> SwitchRowsOfSignal(const rs::world::Guid& signalGuid) const;
    // 手動の強制R。実行時だけの状態で、Undo対象にせず保存もしない。
    void SetSpeedSignalForceRed(const rs::world::Guid& signalGuid, bool forced);
    // 手動の進入許可。強制Rと同じく実行時だけの状態で、Undo対象にせず保存もしない。
    void SetSpeedSignalEntryPermit(const rs::world::Guid& signalGuid, bool permitted);
    SignalBlockViewModel ViewModel() const;
    // 信号1本の行(種別・向き・段・足せる速度・手動の強制R・現示の文)。閉塞に属すかによらず作れる。
    SignalBlockSignalRowViewModel SignalRow(const rs::world::SpeedSignal& signal) const;

private:

    rs::editor::Editor& editor_;
    ISignalSidecarSource& sidecarSource_;
    ISignalAspectSource& aspectSource_;
};

} // namespace rs::app
