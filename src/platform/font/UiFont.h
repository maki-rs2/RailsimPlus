//
// UiFont.h - UIフォント(Noto Sans JP可変フォント)のパス解決・太さ指定の共通処理
//
// RmlUi/ImGui/ビューポート注記(GlyphAtlas)の3系統が同じフォントファイルを共有する
// ための土台。可変フォントの太さは名前付きインスタンス(wght軸)経由でFreeTypeへ
// 指定する必要があり、その面番号の解決をここへ集約する。
//
#pragma once

#include <cstdint>
#include <string>

namespace rs::platform_font {

// UIフォント(Noto Sans JP可変フォント)のソースツリー相対パス。
inline constexpr const char* kUiFontRelativePath = "assets/fonts/NotoSansJP-VariableFont_wght.ttf";

// UI全体で使う太さ(wght軸の値)。SemiBold相当。
inline constexpr uint16_t kUiFontWeight = 600;

// UIフォントのパスを解決する。ビルド時のプロジェクトルート基準の絶対パス
// (RS_PROJECT_SOURCE_DIR)を優先し、無ければ実行ディレクトリ相対へフォールバック。
// どちらも読めなければ空文字列を返す(呼び出し側でフォールバック処理すること)。
std::string ResolveUiFontPath();

// fontPathの可変フォントから、wght軸の値がweightに一致する名前付きインスタンスの
// 面番号(FreeTypeのface_index、上位16bitに名前付きインスタンス番号を格納した値)を
// 返す。可変フォントでない・一致するインスタンスが無い・読込失敗の場合は0を返し
// (=デフォルトインスタンス)、spdlogで警告を1回だけ出す。
long FindNamedInstanceFaceIndex(const std::string& fontPath, uint16_t weight);

} // namespace rs::platform_font
