//
// ScreenCatalog.h - 画面(Screen)ごとの固定UIの表示(上部バナー・下部バー・戻る見出し)を
// 1つの表にまとめる。
//
// 画面ごとの違いはこの表だけで表し、ほかのコードに画面名での分岐を書かない。
// 画面を1種類増やすときに触るのは、Screenの列挙・この表の2か所。
//
#pragma once

#include <array>
#include <cstddef>
#include <iterator>

#include "CatalogCheck.h"
#include "WorkspaceShell.h"

namespace rs::app::workspace {

struct ScreenLayout {
    Screen screen;
    bool bannerVisible;    // 上部バナー(作業モード名)を出すか
    bool bottomBarVisible; // 下部バーを出すか
    // 環境画面・カメラ画面の見出しの題名。見出しを出さない画面はnullptr。
    const char* backHeaderTitle;
    // この画面から作業モード・環境画面・カメラ画面へ直接移れるか(falseなら鑑賞を経由する)。
    bool allowsDirectTransition;
    // 「閉じる」(Close)でこの画面から鑑賞へ戻るか。
    bool closeReturnsToSpectate;
    // ビューポートの個体をクリックするとプラグイン設定パネルを開くか。
    // 作業モードの画面では、この列ではなく作業モードの表(WorkModeCatalog.h)の列を使う。
    // 判定はClickOpensPluginSettings()だけで行う。
    bool clickOpensPluginSettings;
    // 作業モード以外の左右タブ。作業モードはWorkModeCatalogの値を使う。
    std::array<TabContent, kNotchSideCount> tabs{};
    bool hasScenePanel = false;
    bool hasCameraScreen = false;
};

// Screenの列挙順に並べる。
inline constexpr ScreenLayout kScreenCatalog[] = {
    {Screen::Spectate, false, true, nullptr, true, false, true},
    {Screen::Work, true, true, nullptr, true, false, false}, // 作業モードでは作業モードの表の列を使う
    {Screen::Environment, false, true, "シーン", true, true, false, {TabContent::None, TabContent::None}, true},
    {Screen::Camera, false, false, "カメラ", false, true, false, {}, false, true},
};

// 列挙と表の件数・順番が食い違ったらコンパイルエラーにする。
static_assert(std::size(kScreenCatalog) == kScreenCount, "kScreenCatalogの行数がScreenと一致しない");
static_assert(IsCatalogInEnumOrder(kScreenCatalog, &ScreenLayout::screen), "kScreenCatalogの並びがScreenの列挙順と一致しない");

constexpr const ScreenLayout& GetScreenLayout(Screen screen) {
    return kScreenCatalog[EnumIndex(screen)];
}

} // namespace rs::app::workspace
