//
// OverlayCatalog.h - 重ね表示の性質(層・モーダルか・RMLでの名前・Escや操作で閉じられるか)を
// 1つの表にまとめる。
//
// 重ね表示ごとの違いはこの表だけで表し、ほかのコードに種別名での分岐を書かない。
// 重ね表示を1種類増やすときに触るのは、OverlayKindの列挙・この表・workspace.rmlの3か所。
//
#pragma once

#include <cstddef>
#include <iterator>
#include <optional>
#include <string_view>

#include "CatalogCheck.h"
#include "WorkspaceShell.h"

namespace rs::app::workspace {

struct OverlayTraits {
    OverlayKind kind;
    int layer;            // 大きいほど手前(ルート管理3 > ゲーム設定2 > 主メニュー1 > プラグイン設定パネル=敷設エディタ=0)
    bool modal;           // モーダル系列(同時に1つしか見せない)。プラグイン設定パネル・敷設エディタは含めない
    const char* bindName; // RMLのデータモデルで使う名前(全行が持つ)。open_overlay/close_overlayの引数にも使う
    // Esc(WorkspaceShell::Back)で閉じられるか。falseの行は、Escで閉じる「一番手前」の
    // 判定から外れる(重なり順zの計算には含める)。
    bool closableByEsc;
    // RMLのイベント(open_overlay/close_overlay)で開閉してよいか。falseの行は、開閉を
    // WorkspaceShellの遷移処理だけが行い、イベントで名前を指定されても無視する。
    bool userToggleable;
    // 左タブがプラグインブラウザの画面にいる間だけ開いておけるか。trueの行は、ブラウザの無い画面
    // (ブラウザを持たない作業モード・鑑賞・環境画面・カメラ画面)へ移ると遷移の後始末で閉じる。
    bool requiresPluginBrowser;
    // 見えている間、ほかのUIとビューポートの操作を止めるか(全画面の幕を出す)。モーダル系列の行は
    // 常に止めるので、ここはモーダルでない行だけが使う。省略したらfalse。
    bool blocksInput = false;
};

// OverlayKindの列挙順に並べる。同じ層は開いた順で手前が決まる。
inline constexpr OverlayTraits kOverlayCatalog[] = {
    // 異常終了のあとの復元の画面。選ぶまで閉じない(Escでも閉じない)。
    {OverlayKind::Recovery, 3, true, "recovery", false, false, false},
    // レイアウトの一覧の画面。開くのはコード(LayoutDocumentController)だけだが、×はclose_overlayで閉じる。
    {OverlayKind::LayoutFiles, 1, true, "layoutFiles", true, true, false},
    {OverlayKind::GameSettings, 2, true, "gameSettings", true, true, false},
    {OverlayKind::MainMenu, 1, true, "mainMenu", true, true, false},
    {OverlayKind::PluginSettingsPanel, 0, false, "pluginSettings", true, true, false},
    // 敷設エディタは作業モードの遷移で開閉する。EscでもRMLのイベントでも閉じない。
    {OverlayKind::LayingEditor, 0, false, "layingEditor", false, false, false},
    // ブラウザの歯車ボタンとゲーム設定の「ルートを管理…」から開くルート管理ウィンドウ。ゲーム設定より
    // 手前(層3)に出す非モーダル(モーダル系列に入れないので、主メニュー・ゲーム設定は隠れない)。RMLでは
    // モーダル層の中の、幕より手前に置く。×かEscで閉じ、ブラウザの無い画面へ移ったときも閉じる
    // (ブラウザのあるモード同士の切替では開いたまま)。開く操作は後始末を起こさないので、ブラウザの無い
    // 画面でもゲーム設定から開ける。見えている間は、幕を出してほかのUIとビューポートの操作を止める。
    {OverlayKind::PluginRoots, 3, false, "pluginRoots", true, true, true, true},
    // 右上の警告バッジから開く、見つからないプラグインの一覧。バッジはどの画面にも出るので、
    // ブラウザの有無に関わらず開いたままにできる。
    {OverlayKind::MissingPlugins, 0, false, "missingPlugins", true, true, false},
    {OverlayKind::Help, 3, false, "helpWindow", true, true, false},
};

// 列挙と表の件数・順番が食い違ったらコンパイルエラーにする。
static_assert(std::size(kOverlayCatalog) == kOverlayKindCount, "kOverlayCatalogの行数がOverlayKindと一致しない");
static_assert(IsCatalogInEnumOrder(kOverlayCatalog, &OverlayTraits::kind), "kOverlayCatalogの並びがOverlayKindの列挙順と一致しない");

constexpr const OverlayTraits& GetOverlayTraits(OverlayKind kind) {
    return kOverlayCatalog[EnumIndex(kind)];
}

// RMLで使う名前(bindName)から種類を引く。表に無い名前ならstd::nulloptを返す。
constexpr std::optional<OverlayKind> FindOverlayKindByName(std::string_view name) {
    for (const OverlayTraits& traits : kOverlayCatalog) {
        if (name == traits.bindName) return traits.kind;
    }
    return std::nullopt;
}

} // namespace rs::app::workspace
