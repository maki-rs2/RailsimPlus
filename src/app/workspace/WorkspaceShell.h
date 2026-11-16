//
// WorkspaceShell.h - 通常画面(固定ワークスペース)の画面状態
//
// RmlUi・ImGui・Win32のいずれにも依存しない純C++の状態機械。画面遷移・左右タブの
// 開閉・レールモードの敷設/編集タブ・重ね表示(主メニュー/ゲーム設定/
// プラグイン設定パネル/敷設エディタ)の開閉・プラグイン設定パネルの対象の個体ID(開く要求は
// OpenPluginSettings。Editorの選択とは連動しない)・Escの優先順位を持つ。
//
// 描画側(RmlUiWorkspaceView)は、このクラスの操作メソッドを呼んで状態を変え、
// Snapshot() が返す表示用構造体をデータバインディングへ渡すだけにする(ロジックを
// ビュー側へ持ち込まない)。左右タブの3状態(無/閉/開)の解釈や、
// 重ね表示の重なり順・Escで閉じる順もここで決める。
//
#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include <magic_enum/magic_enum.hpp>

#include "EnumIndex.h"
#include "../../world/alignment/Guid.h"

namespace rs::app::workspace {

// 通常画面が取りうる画面。鑑賞は専用の作業モードを持たない無選択状態。
enum class Screen { Spectate, Work, Environment, Camera };
inline constexpr std::size_t kScreenCount = magic_enum::enum_count<Screen>();
// 作業モードの画面か(作業モードの値が意味を持つ画面)。「作業モード中か」の判定はこれだけを使う。
constexpr bool IsWorkScreen(Screen screen) { return screen == Screen::Work; }

// 作業モード(下部バー中央アイコンの順序と一致させる)。モードごとのレイアウトはWorkModeCatalog.h。
enum class WorkMode { Rail, Train, Signal, Dia, Scenery, Terrain };
inline constexpr std::size_t kWorkModeCount = magic_enum::enum_count<WorkMode>();

// レールモードの敷設エディタ(移動できるウィンドウ)の「敷設」「編集」切替。
// 起動中だけメモリに保持する。
enum class RailTab { Lay, Edit };

// 左右タブのノッチ側。
enum class NotchSide { Left, Right };
inline constexpr std::size_t kNotchSideCount = magic_enum::enum_count<NotchSide>();

// 重ね表示の種別。層・モーダルかどうかはOverlayCatalog.hの表を参照。
// 層: ゲーム設定 > 主メニュー > プラグイン設定パネル = 敷設エディタ。
// 敷設エディタは作業モードの遷移で開閉する移動できるウィンドウ。プラグイン
// ブラウザは重ね表示ではなく、左タブの中身として扱う。
// ルート管理ウィンドウ・操作ガイド・見つからないプラグインの一覧も移動できるウィンドウ。
// 層と開閉の性質はOverlayCatalogの表で定義する。
enum class OverlayKind {
    Recovery, LayoutFiles, GameSettings, MainMenu, PluginSettingsPanel, LayingEditor, PluginRoots, MissingPlugins, Help
};
inline constexpr std::size_t kOverlayKindCount = magic_enum::enum_count<OverlayKind>();

// Back()(Esc相当)の結果。呼び出し側はDeferredToToolのときだけ編集ツールの
// 取消を自分で行う(WorkspaceShellはツール状態を持たないため、可否の判断だけを
// 引数(toolOperationPending)で受け取り、結果を返すだけ)。
// LeaveCabinViewは、運転台視点をやめる要求。視点の状態はWorkspaceShellの外にあるので、呼び出し側が実際にやめる。
enum class BackResult { ClosedOverlay, DeferredToTool, LeaveCabinView, ReturnedToSpectate, OpenedMenu };

// 左右タブの中身。Panelはモード固有の一覧・設定、PluginBrowserは
// プラグインブラウザ。開閉・大きさ・重なり順は中身によらず共通。
enum class TabContent { None, Panel, PluginBrowser };

// 左右タブの3状態。Present()==false(content==None): ノッチも含めて画面外(無)。
// Present()&&!open: ノッチだけ画面端に見える(閉)。Present()&&open: パネル本体が見える(開)。
struct EdgePanelSnapshot {
    TabContent content = TabContent::None;
    bool open = false;

    // タブがあるか(contentから求める。同じ情報を別の値で持たない)。
    bool Present() const { return content != TabContent::None; }
};

// 重ね表示1つ分の表示情報。zは重なり順(開いているものに奥から1,2,3...、閉じていれば0)。見た目の
// 重なり順とEscで閉じる順を同じ情報(層+開いた順)から決めるための値であり、
// DOM側はこれをそのままz-indexへ反映する。
struct OverlaySnapshot {
    bool open = false;
    bool visible = false;
    int z = 0;
};

// 表示用スナップショット。画面ごとの
// 固定UIの表示(ScreenCatalog)に沿って導出する。主メニューボタンは全画面で常に表示する
// ため、ここには含めない。
struct WorkspaceSnapshot {
    Screen screen = Screen::Spectate;
    WorkMode workMode = WorkMode::Train; // screen==Workのときのみ有効
    RailTab railTab = RailTab::Lay;      // workMode==Railのときのみ有効

    bool bannerVisible = false;
    bool bottomBarVisible = false;
    // 環境画面・カメラ画面の見出しの題名(ScreenCatalogの題名を持つ画面だけ。ほかはnullptr)。
    const char* screenBackTitle = nullptr;
    // モーダルの重ね表示(主メニュー・ゲーム設定)が1つでも見えているか。
    bool modalVisible = false;
    // 運転台視点中か。このとき左右タブ・バナー・下部バーは出さず、運転台視点のパネルと戻るボタンを出す。
    bool cabinView = false;
    // 信号の配置待ち中か(作業モードが信号のときだけ意味を持つ)。操作説明を配置中のものに替える。
    bool signalPlacing = false;

    // NotchSideで添字付け(Tab()経由での参照を推奨)。
    std::array<EdgePanelSnapshot, kNotchSideCount> tabs{};

    // OverlayKindで添字付け(Overlay()経由での参照を推奨)。
    std::array<OverlaySnapshot, kOverlayKindCount> overlays{};

    const EdgePanelSnapshot& Tab(NotchSide side) const { return tabs[EnumIndex(side)]; }
    const OverlaySnapshot& Overlay(OverlayKind kind) const { return overlays[EnumIndex(kind)]; }

    // 以下はほかの値から求める(同じ情報を別の値で持たない)。
    // 作業モードの画面か(workModeが意味を持つか)。
    bool InWorkMode() const { return IsWorkScreen(screen); }
    // 環境画面・カメラ画面の見出しを出すか。
    bool ScreenBackVisible() const { return screenBackTitle != nullptr; }
    // 敷設エディタ(移動できるウィンドウ)の重ね表示が開いているか。作業モード中で、
    // モード表がそれを持つときに、WorkspaceShellが遷移で開く。
    bool LayingEditorPresent() const { return Overlay(OverlayKind::LayingEditor).open; }
};

class WorkspaceShell {
public:
    // 左右タブは開いた状態から始める。
    WorkspaceShell() { OpenAllTabs(); }

    // 下部バー中央のモードアイコン。別モードなら切替、現在のモードと同じなら
    // 鑑賞へ戻る。カメラ画面では反応しない。
    void SelectMode(WorkMode mode);

    // 左下の環境入口(鑑賞/作業モード/環境画面から遷移、カメラ画面からは移らない)。
    void EnterEnvironment();
    // 左下のカメラ入口(鑑賞/作業モード/環境画面から遷移、カメラ画面からは移らない)。
    void EnterCamera();
    // 環境画面・カメラ画面の「閉じる」。鑑賞へ戻る(それ以外の画面では無視する)。
    void Close();
    // どの画面からでも鑑賞へ戻る(レイアウトの読込開始など、利用者の操作によらない遷移用)。
    void ReturnToSpectate();

    // 左右タブ上端のノッチ。中身(パネル/プラグインブラウザ)によらず、そのタブの開閉を
    // トグルする。
    void ToggleNotch(NotchSide side);

    // 敷設エディタの敷設/編集切替。
    void SelectRailTab(RailTab tab);

    // プラグイン設定パネルを開く要求。入口はこれ1つで、Editorの
    // 選択とは連動しない。対象の個体IDを覚え、すでに開いていても手前へ出し直して対象を切り替える。
    void OpenPluginSettings(const rs::world::Guid& targetEntityId);
    // プラグイン設定パネルを閉じる。
    void ClosePluginSettings();
    // パネルが開いているときだけ対象の個体IDを返し、閉じていれば無効値を返す。「パネルが
    // 開いているか」は重ね表示の開閉だけで決まるので、×・Esc・遷移のどれで閉じても空になる。
    const rs::world::Guid& PluginSettingsTarget() const;

    // 重ね表示を開く/閉じる。開いた順の通し番号で状態を持つので、開いているものをもう一度
    // 開くと、同じ層の中で一番手前へ出し直す。ゲーム設定は主メニューから重ねて開くが、主メニュー自体は閉じない
    // (Escの優先順位でゲーム設定→主メニューの順に閉じるため、開いたままにしておく)。
    // OverlayCatalogのuserToggleableがfalseの種別(敷設エディタ)も、ここでは拒まない。
    // RMLのイベントからの開閉を止めるのは呼び出し側(RmlUiWorkspaceView)の仕事。
    void OpenOverlay(OverlayKind kind);
    void CloseOverlay(OverlayKind kind);

    // 主メニューの保存・ロード・終了のうち、ロードだけが
    // 状態を持ち、プラグイン設定パネルを閉じる(画面は変えない)。保存・終了はここでは
    // 何もしない(呼び出し側がメニューを閉じる)。

    // Esc相当。重ね表示を手前から1つ閉じる→(取消可能なツール操作があればそれを
    // ツール側へ委ねる)→運転台視点中ならそれをやめる要求→作業モード/環境/カメラなら鑑賞へ戻る、の優先順位で処理する。
    // 手前の判定は、開いている重ね表示のうちEscで閉じられるもの
    // (OverlayCatalogのclosableByEsc)で、(層, 開いた順)が最大のもの。
    // toolOperationPendingはWorkspaceShellの外(編集ツール)が判断した「取り消せる
    // 操作の途中か」をそのまま渡す。
    BackResult Back(bool toolOperationPending);

    // 運転台視点中かを知らせる(視点の状態の正典はCabinViewCamera)。毎フレーム呼んでよい。
    void SetCabinView(bool active) { cabinView_ = active; }
    // 信号の配置待ち中かを知らせる(状態の正典はEditorのsignalBlockArmedAction)。毎フレーム呼んでよい。
    void SetSignalPlacing(bool active) { signalPlacing_ = active; }

    WorkspaceSnapshot Snapshot() const;

    Screen CurrentScreen() const { return screen_; }
    WorkMode CurrentWorkMode() const { return workMode_; }
    RailTab CurrentRailTab() const { return railTab_; }

private:
    void EnterWorkModeInternal(WorkMode mode);
    // 左右タブをすべて開いた状態にする(起動時と、作業モードへ入るたび)。
    void OpenAllTabs() { tabOpen_.fill(true); }
    // 環境画面・カメラ画面へ入る共通の処理。ScreenCatalogのallowsDirectTransitionがfalseの画面からは移らない。
    void EnterScreen(Screen screen);
    // 画面遷移に伴う共通の後始末(プラグイン設定パネルを閉じる・敷設エディタの開閉)。
    void ApplyScreenTransitionSideEffects();

    // 重ね表示が開いているか。開いた順の通し番号(overlayOrder_)だけで状態を持ち、0を閉とする。
    bool IsOverlayOpen(OverlayKind kind) const;
    // 開いている重ね表示を奥から手前の順に並べる((層, 開いた順)の昇順)。Escで閉じる
    // 対象(末尾)と重なり順(z)の両方をこの並びから決める。
    std::vector<OverlayKind> OpenOverlaysBackToFront() const;

    bool cabinView_ = false;
    bool signalPlacing_ = false;
    Screen screen_ = Screen::Spectate;
    WorkMode workMode_ = WorkMode::Train;
    RailTab railTab_ = RailTab::Lay;

    // NotchSideで添字付け。起動時・作業モードへ入るたびに開いた状態へ戻す。
    std::array<bool, kNotchSideCount> tabOpen_{};

    // プラグイン設定パネルの対象の個体ID。開閉の正典は重ね表示の状態で、閉じているときは
    // PluginSettingsTarget()が無効値を返す(この値だけを見ない)。
    rs::world::Guid pluginSettingsTarget_ = rs::world::NilGuid();

    // OverlayKindで添字付けした、開いた順の通し番号(大きいほど後から開いた=手前)。
    // 0は閉じていることを表す。
    unsigned int openOrderCounter_ = 0;
    std::array<unsigned int, kOverlayKindCount> overlayOrder_{};
};

} // namespace rs::app::workspace
