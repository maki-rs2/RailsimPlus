//
// ImGuiFontManager.h - 日本語フォント(Noto Sans JP可変フォント)アトラスの構築
//
// main.cpp のフォント初期化・再構築ロジックを集約。基本範囲(ASCII+常用漢字/
// かな)に加え、プラグイン読込時にそのプラグイン固有の稀な漢字(駅名等)を追加
// してアトラスを再構築する。フォントパス解決・太さ指定はsrc/platform/font/
// UiFont.hに委ねる。
//
#pragma once

struct ImGuiIO;
template <typename T> struct ImVector;
using ImWchar = unsigned short;

namespace rs::legacy_plugin {
struct LoadedPlugin;
}

namespace rs::app {

// フォントアトラスを構築する。plugin==nullptr なら基本範囲のみ。plugin 指定時は
// そのプラグインの名称/作者/スイッチ/モデル名の文字を追加する。
// rebuildBackend=true のときはアトラスを Clear してから作り直し、Build() と
// ImGui_ImplDX12_InvalidateDeviceObjects() まで行う(プラグイン読込後の再構築用)。
// rebuildBackend=false は DX12 バックエンド初期化前の初回構築用。
void BuildJapaneseFontAtlas(ImGuiIO& io, ImVector<ImWchar>& ranges,
                            const rs::legacy_plugin::LoadedPlugin* plugin,
                            bool rebuildBackend);

} // namespace rs::app
