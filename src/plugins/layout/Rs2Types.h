//
// Rs2Types.h - .rs2 レイアウトファイルの生データ構造
//
// PiDocument(AST)をファイル内容に素直に写した型。ブロック名・キー名の解釈は
// Rs2Schema.cpp に集約し、ここでは値だけを保持する。Address は保存時メモリ
// アドレス(16桁hex)を「生文字列のまま」保持する(2^53超の値域があるため
// double を経由しない)。
//
// world::TrackGraph 等の内部表現への変換は Rs2Import が担う(このファイルは
// 変換前の生データのみ)。
//
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "../../core/Math.h"

namespace rs::legacy_layout {

// 16桁hexの生文字列。"0000000000000000" = null。
using Rs2Address = std::string;

inline bool Rs2AddressIsNull(const Rs2Address& addr) {
    if (addr.empty()) return true;
    for (char c : addr)
        if (c != '0') return false;
    return true;
}

// RailConnector.Splitter / RailWay.SplitList.Splitter 共通(1サンプル点)。
struct Rs2Splitter {
    rs::core::Vec3 pos;
    rs::core::Vec3 dir;
    rs::core::Vec3 up;
};

// RailConnector の Link00/01/10/11 の1スロット。
struct Rs2LinkSlot {
    int        endIdx = 0;      // 接続先 RailWay のどちらの端(0/1)が刺さるか
    Rs2Address target;          // 接続先 RailWay の Address(null可)
    bool       Populated() const { return !Rs2AddressIsNull(target); }
    // 接続先AddressがRailWayではなくWarp(WarpList)を指すスロット。
    // ParseRs2Layout(MarkWarpSlots)が全Scene構築後に印を付ける。
    bool       warp = false;
};

// PointInst > PointList: 1編成(またはデフォルト)に対するポイントルーチン列。
struct Rs2PointRoutine {
    Rs2Address       trainGroup;  // 全0 = デフォルトリスト
    bool             useDefault = true;
    // 順序=永続化されたカーソル。ソート/重複排除禁止。0=ランダム、その他は値-1がスロット。
    std::vector<int> points;
};

// RailConnector > PointInst。Point は編成ごとの経路方針であり、現在の開通状態は
// RailConnector.trailPoint 側に保持する。
struct Rs2PointInst {
    bool                         present = false;
    std::string                  name;
    std::vector<Rs2PointRoutine> routines;
};

// = RailConnector(ノード)
struct Rs2RailConnector {
    Rs2Address    address;
    Rs2Splitter   splitter;
    float         cant = 0.0f;
    // RailSim II のRailConnectorは Link00/01 = S0面 / Link10/11 = S1面 の2面構成で、
    // 走行は必ず一方の面から他方の面へ抜ける。
    Rs2LinkSlot   link00, link01, link10, link11;
    // TrailPoint = <S0面の現在スロット>, <S1面の現在スロット>; (0始まり)
    // = ポイントの保存された開通状態。分岐している(スロット2個の)面だけが 0/1 を取り、
    // 他方は常に0。
    std::array<int, 2> trailPoint{0, 0};
    Rs2PointInst  pointInst;
};

// RailWay の両端リンク: Link0/1 = <endIdx>, <branchIdx>, <ConnectorAddress>
struct Rs2WayEndLink {
    int        endIdx = 0;
    int        branchIdx = 0;
    Rs2Address connector;
};

// RailWay.PierList.PierLink = <s>, <Address>;
// edge上の弧長s位置に実際に立つPier(Scene直下PierList側インスタンス)のAddress参照
// (実データで検証済み)。PierPos(スカラー)は生成器の持ち越しカウンタで無関係、無視してよい。
struct Rs2PierLink {
    float      s = 0.0f;
    Rs2Address address;
};

// RailWay.PoleList.PoleLink = <s>, <branchIdx>, <yes/no>, <Address>;
// s/Address はPierLinkと同じ意味。branchIdxは複線一括Pole(TrackNum>1)の
// どの接続点かを指す(Rs2SceneApplier.cppのpoleLateralOffsetと同じ規約)。
// yes/noの意味は実データ解析未確定のため保持のみ(populated)。
struct Rs2PoleLink {
    float      s = 0.0f;
    int        branchIdx = 0;
    bool       populated = true;
    Rs2Address address;
};

// = RailWay(エッジ)
struct Rs2RailWay {
    Rs2Address     address;
    Rs2WayEndLink  link0, link1;
    float          speedLimit = -1.0f;  // -1=無指定
    std::string    railBlock;  // 空=無指定
    std::string    railPlugin, tiePlugin, girderPlugin, pierPlugin, linePlugin, polePlugin;
    std::vector<Rs2Splitter> splitList;
    // 複線一括仕様の橋脚/架線柱/架線を配置する仮想中心線(RailSim II の CRailWay::m_CenterlineOnly)。
    // RailSim II では専用のRailConnectorを持つ通常のRailWayとして構築されるため、
    // nodeA/nodeB解決は通常のRailWayと同じ経路で行える。
    bool           centerlineOnly = false;
    int            trackCount = 0;
    float          trackSpacing = 0.0f;
    // このedge上に存在するPier/Poleへの厳密な参照(s位置+Address)。
    std::vector<Rs2PierLink> pierLinks;
    std::vector<Rs2PoleLink> poleLinks;
};

// StationList > Station > PlatformList > Platform。
// 装飾プラグイン参照やCoord座標リストは対象外(停車判定に使うAddress/Stoppable/OpenDoor/RailListのみ)。
struct Rs2PlatformInst {
    Rs2Address address;
    bool       stoppable = true;         // Stoppable = yes/no;(キー未出現時はyes扱い)
    bool       openDoor[2] = {false, false}; // OpenDoor = yes/no, yes/no;
    std::vector<Rs2Address> railList;    // RailList = <RailWay Address>, ...;(対応番線)
};

// DiaElement{ Action; TimeType; Time = h, m, s; Offset; }。
struct Rs2DiaElement {
    bool   present = false;
    int    action = 0;
    int    timeType = 0;
    int    time[3] = {0, 0, 0};
    double offset = 0.0;  // 正規化値[-1,+1]。メートルではない。
};

// DiaInst > DiaList: 1編成(またはデフォルト)に対するダイヤルーチン列。
struct Rs2DiaRoutine {
    Rs2Address                 trainGroup;  // 全0 = デフォルトリスト
    bool                       useDefault = true;
    // 順序=永続化されたカーソル。ソート/重複排除禁止。
    std::vector<Rs2DiaElement> elements;
};

// Station > DiaInst。
struct Rs2DiaInst {
    bool                       present = false;
    std::string                name;
    std::vector<Rs2DiaRoutine> routines;
};

// Struct/Pole/Pier/Station の配置インスタンス。
// 各種別で位置/姿勢のキー名が異なる
// (Struct/Station=Pos/Dir/Up、Pole=Pos+OrigDir(Upなし)、Pier=JointPos/JointDir/JointUp)。
// Rs2Schema側でこの差異を吸収し、共通形(pos/dir/up)へ正規化して格納する。
// upが元データに無い場合(Pole)は hasUp=false とし、変換側で既定(ワールドup)を補う。
// Line はここに含めない(Link0/Link1によるコネクタ間スパンで構造が全く異なるため、
// 対応しない)。
struct Rs2PlacedInstance {
    std::string    kind;  // "Struct"/"Pole"/"Pier"/"Station"(診断表示用)
    std::string    pluginName;  // フォルダ名そのもの(未解決)
    // 保存時メモリアドレス。kind=="Pole"はLine{Link0/Link1}からの架線スパン参照解決に、
    // kind=="Pier"/"Pole"共通でRailWay埋め込みPierList/PoleList.PierLink/PoleLinkからの
    // edge厳密紐付けに使う。
    Rs2Address     address;
    rs::core::Vec3 pos, dir, up;
    bool           hasUp = true;
    // SwitchOption = <値0>, <値1>, ...; DefineSwitch宣言順(plugin.def.switches[i])に
    // 位置対応する、レイアウト保存時点のスイッチ確定値。
    // SwitchNumは配列長と一致するはずなので保持しない(実データで検証済み)。
    std::vector<int32_t> switchOptions;
    std::vector<Rs2PlatformInst> platforms; // kind=="Station"のときのみ非空(PlatformList > Platform)
    Rs2DiaInst diaInst;  // kind=="Station"のときのみ意味を持つ
};

// WarpList{ Warp{ Address; Link0/Link1 = <face>, <slot>, <RailConnectorAddress>; } }
// = シーン間ワープ。両端はRailConnectorを直接指す
// (RailWayを介さない)。link0/link1のendIdx=接続先RailConnectorの面(0/1)、
// branchIdx=その面内のスロット番号で、Rs2WayEndLinkと同じ3値の形をそのまま流用する。
// 実データでは全件 slot=0(RailSim II 同梱サンプルレイアウトの8件で確認済み)。
struct Rs2Warp {
    Rs2Address    address;
    Rs2WayEndLink link0, link1;  // connector = RailConnector の Address
};

// Scene直下 LineList{Line{...}}(架線スパン1本)。
// 隣接Pole間の1スパンをPole Address同士のLink0/Link1で直接連結する(RailWayの
// RailConnector間Link0/Link1と同じ構造。実データで検証済み)。
// Right/Dirは使わない(架線メッシュはPole側のOrigDirから再構成するため)だが、
// 生データをそのまま保持しておく。
struct Rs2Line {
    Rs2Address     address;
    std::string    linePluginName;
    rs::core::Vec3 right, dir;
    Rs2WayEndLink  link0, link1;  // connector = Pole の Address(RailConnectorではない)
};

// Scene直下 Camera{ Head; Pitch; Dist; FieldOfView; Focus=(x,y,z); }(視点状態)。Head/Pitchはラジアン、Dist/Focusはメートル(実データで
// 確認: Head=-3.110222/Pitch=0.340560/Dist=43.681732/
// FieldOfView=0.785398(=π/4)。値域からラジアンと判断)。
// FieldOfViewはレンダラが固定60°縦視野角のため、取り込むが使わない。
struct Rs2Camera {
    bool present = false;
    float head = 0.0f, pitch = 0.0f, dist = 30.0f, fieldOfView = 0.0f;
    rs::core::Vec3 focus;
};

// = Scene(レイアウトの1レイヤ)
struct Rs2Scene {
    Rs2Address  address;  // Scene自身のAddress(Rs2Layout::currentSceneとの照合用)
    std::string name;
    // Scene直下 EnvPlugin = "名前";(Env/<名前>/ のフォルダ名。空=未指定)。
    std::string envPlugin;
    Rs2Camera   camera;
    std::vector<Rs2RailConnector> railConnectors;
    std::vector<Rs2RailWay>       railWays;
    std::vector<Rs2PlacedInstance> placements;  // Struct/Pole/Pier/Station/Surface 合算(空のことがある)
    std::vector<Rs2Line>          lines;        // 架線スパン(Pole間)
};

// TrainGroupList > TrainGroup > Train(静止配置分のみ)
struct Rs2Train {
    std::string    trainPlugin;
    rs::core::Vec3 pos, dir, up;
    bool           reverse = false;
    // SwitchOption = <値0>, <値1>, ...; DefineSwitch宣言順に位置対応する
    // スイッチ確定値(Rs2PlacedInstance::switchOptionsと同じ意味)。
    // SwitchNumは配列長の検証にのみ使い保持しない。
    std::vector<int32_t> switchOptions;
};

// GroupEnd0/GroupEnd1/Seeker で共通の位置情報(RailSim II の CGroupEndLocator 相当)。
// Location = <side>, <offset>, <RailWayAddress>;
// offsetは RailSim II のトレーサの経路累積値であり、edge内弧長ではないため位置復元には
// 使えない(実データで同一レール上に223mと5024mが現れる)。そのまま保持するだけに留める。
struct Rs2GroupEndLocator {
    bool       present = false;
    Rs2Address address;      // このlocator自身の保存時アドレス
    int        side = 0;
    double     offset = 0.0; // 経路累積値(位置復元には使えない、上記コメント参照)
    Rs2Address railAddress;  // 乗っているRailWayのAddress
};

// Enabled=yes のTrainGroupにのみ保存される走行状態一式。
// 速度(TargetSpeed/CurrentSpeed)は目標・現在の速度として反映する。State=2(停車中)/3(発車直前)は
// 停車した状態で始める(Rs2TrainRunStateRestore)。State=1(減速中)は走行状態として始める。
// DepartureTime/StopTargetは解釈しない。
struct Rs2TrainGroupRunState {
    bool   present = false;
    // 0=通常走行 / 1=停車目標へ減速中 / 2=停車中(ドア扱い・発車待ち) / 3=発車直前(ドア閉待ち)
    int    state = 0;
    double targetSpeed = 0.0;   // 設定速度(km/h相当)
    double currentSpeed = 0.0;  // 現在速度
    double stopTarget = 0.0;    // 停車目標までの距離[m](state==1の時のみ意味を持つ)
    // DepartureTime = <hex16桁>, <hex16桁>;
    // RailSim II では double(絶対日時)だが、保存書式(%p×2)が32bit時代のポインタ幅前提の
    // まま64bitビルドで書かれており、64bitでは16桁の語2つを8バイトdoubleへ
    // 書き込む形になっていて書式と型が整合していない。値の意味は信頼できないため
    // 解釈せず、2語を生のhex文字列のまま保持するだけにする。
    std::string departureTimeRaw[2];
    int    doorWait = 0;           // ドアを閉じる時間(フレーム数。到着時に1回入るだけで、残り時間ではない)
    bool   openDoor[2] = {false, false}; // 進行方向の各側のドア開フラグ
    bool   reverse = false;              // 後退モード
    Rs2GroupEndLocator groupEnd[2];      // GroupEnd0/GroupEnd1
    Rs2GroupEndLocator seeker;           // 先行列車検知用の位置
    std::vector<Rs2Address> pointList;   // PointList = <RailConnector Address>, ...(経路方針対象。出現しないことがある)
    std::vector<Rs2Address> seekList;    // SeekList = <RailConnector Address>, ...(先行検知対象。出現しないことがある)
    // 実行中の1要素のスナップショット。Rs2DiaInstの駅ルーチン列とは別物。
    Rs2DiaElement dia;
    Rs2Address    platform;              // PlatformInst の Address(null許容)
};

struct Rs2TrainGroup {
    Rs2Address            address;
    std::string           name;
    // 軌道に設置済みか(yes=Train/GroupEnd等の実配置データを伴う)。
    bool                  enabled = false;
    std::vector<Rs2Train> trains;
    // Enabled=yes のときだけ埋まる走行状態(present==falseなら未設置)。
    Rs2TrainGroupRunState runState;
};

// legacy .rs2 の Time ブロック生データ。
// 各フィールドの意味は RailSim II のソース(CSaveFile.cpp/CSimulationMode.cpp/
// CEnvPlugin.cpp)に基づく。
struct Rs2Time {
    bool present = false;
    // Date = Year, Month, Day, DayOfWeek(いずれも0始まり)。実際の西暦年は
    // Year+1、実際の月は Month+1、実際の日は Day+1(CSaveFile.cpp
    // "m_Year+1, m_Month+1, m_Day+1"表示ロジックより)。DayOfWeekは
    // 0=日曜〜6=土曜(Win32 SYSTEMTIME::wDayOfWeekと同じ規約をそのまま使用)。
    int year = 0, month = 0, day = 0, dayOfWeek = 0;
    // Time = Hour, Minute, Second, Frame。Frameは1/30秒単位のサブフレーム
    // (0-29、CSaveFile.cpp "m_Frame/30.0"より)。
    int hour = 0, minute = 0, second = 0, frame = 0;
    // 年初(1/1)からの通算日数(CSaveFile.cppコンストラクタで年初起算に初期化され、
    // 以後Dateと連動して増分される値)。GameClockへの変換ではDateを正とし、
    // SumDaysは整合性検証(不一致警告)のみに使う。
    int sumDays = 0;
    // シミュレーション設定ダイアログのラジオボタン選択index(生値)。
    // 実際の倍率への変換は rs::legacy_layout::kTimeScaleTable/kSimSpeedTable
    // (Rs2Import.cpp)を参照。
    int timeScaleIndex = 3;       // 0-9(CSimulationMode::TIME_SCALE_NUM)
    int simulationSpeedIndex = 1; // 0-4(CSimulationMode::SIM_SPEED_NUM、1=等倍)
    // 地球の自転/公転の強制モード(CSimulationMode::m_EarthRotation/m_EarthRevolution)。
    // 0=Auto(実際の時刻/季節を使う)。詳細は rs::world::SolarOverrides 参照。
    int earthRotationMode = 0;   // 0=Auto, 1=常に昼, 2=常に夜
    int earthRevolutionMode = 0; // 0=Auto, 1=春, 2=夏, 3=秋, 4=冬
};

// レイアウト全体
struct Rs2Layout {
    std::string railSimVersion;
    std::string datafileType;
    Rs2Address                 currentScene;  // SceneList.CurrentScene(未出現なら空)
    std::vector<Rs2Scene>      scenes;
    std::vector<Rs2Warp>       warps;         // WarpList.Warp(シーン間ワープ、Scene横断)
    std::vector<Rs2TrainGroup> trainGroups;
    Rs2Time                    time; // Timeブロック(present==falseなら未出現)

    // 未知ブロック/未知キーの件数(前方互換の読み飛ばし。診断表示用)。
    int unknownBlockCount = 0;
};

} // namespace rs::legacy_layout
