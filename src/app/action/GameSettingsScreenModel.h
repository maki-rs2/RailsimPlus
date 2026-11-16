//
// GameSettingsScreenModel.h - ゲーム設定の画面(RmlUi)に出す値を作る純粋関数
//
// GameSettingsAction::ViewModelの値と翻訳キー(assets/locale/strings.json)から、画面が表示できる
// 文字列と選択肢を作る。状態は持たず、ファイルシステムにも触れない(既定のフォルダは引数で受ける)。
//
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "GameSettingsAction.h"
#include "../input/InputBindings.h"
#include "../locale/Localization.h"
#include "../workspace/GameSettingsPresentation.h"
#include "../workspace/InputSettingsPresentation.h"
#include "../workspace/RenderSettingsPresentation.h"

namespace rs::app {

GameSettingsTexts BuildGameSettingsTexts(const Localization& localization);

// タブ(一般・描画・操作)。
std::vector<GameSettingsChoice> BuildGameSettingsTabChoices(const Localization& localization);
workspace::GameSettingsPresentation BuildGameSettingsPresentation(workspace::GameSettingsPresentation presentation,
                                                                 const Localization& localization);
workspace::RenderSettingsPresentation BuildRenderSettingsPresentation(workspace::RenderSettingsPresentation presentation,
                                                                     const Localization& localization);
// 操作タブの全体。行は実際の割当表bindingsから作る(InputSettingsRows.h)。
workspace::InputSettingsPresentation BuildInputSettingsPresentation(workspace::InputSettingsPresentation presentation,
                                                                   const Localization& localization,
                                                                   const std::vector<input::InputBinding>& bindings);
// 言語(日本語・English)。keyはLanguageCode。
std::vector<GameSettingsChoice> BuildLanguageChoices(const Localization& localization);
// UIテーマ。themeNamesの各名前が1項目で(keyはテーマ名)、表示名は翻訳キー
// game_settings.theme.<名前の-を_にしたもの>。翻訳が無い名前はテーマ名のまま出す。
std::vector<GameSettingsChoice> BuildUiThemeChoices(const std::vector<std::string>& themeNames,
                                                    const Localization& localization);
// 自転(時刻連動・常に昼・常に夜)。keyはkEarthRotationKeysの名前。
std::vector<GameSettingsChoice> BuildEarthRotationChoices(const Localization& localization);

// keyに合う言語を引く。どの言語のコードでもなければfalse。
bool FindLanguageByKey(const std::string& key, Language& out);

// ゲーム内時刻の表示。形は「年-月-日(曜) 時:分:秒」(年は4桁、ほかは2桁)。
std::string FormatGameDateTime(const GameSettingsViewModel& viewModel, const Localization& localization);

// fpsの表示。例: 60.0 fps (16.67 ms)。fpsが0以下ならmsは0。
std::string FormatFramerate(float framesPerSecond);

// 音量[0,1]と画面の百分率の変換。
int VolumeToPercent(float volume);
float PercentToVolume(float percent);

// 入力欄に出すTimeScaleの文字。例: 60.0
std::string FormatTimeScale(float scale);

// 出力デバイスの選択肢の実体。先頭は空文字(システム既定)で、続けて列挙した名前を並べる。
// 設定の名前が列挙に無ければ(抜かれたデバイスなど)、末尾に足して選んだままにする。
std::vector<std::string> BuildAudioDeviceNames(const std::vector<std::string>& enumerated,
                                               const std::string& currentDevice);
// namesの各項目の表示名。空文字は「システム既定」にする。
std::vector<std::string> BuildAudioDeviceLabels(const std::vector<std::string>& names,
                                                const Localization& localization);
// currentDeviceがnamesの何番目か(無ければ0=システム既定)。
int AudioDeviceSelectedIndex(const std::vector<std::string>& names, const std::string& currentDevice);

// プラグインルートの表示。1件ならそのパス、2件以上なら件数、無ければ既定のフォルダ。
std::string FormatPluginRootsSummary(const std::vector<std::string>& roots, const std::string& defaultFolder,
                                     const Localization& localization);

// レイアウト保存先の表示。設定が空なら既定のフォルダを「既定: ...」の形で出し、isDefaultをtrueにする
// (画面では薄く表示し、「既定に戻す」を出さない)。
struct LayoutSaveDirectoryDisplay {
    std::string text;
    bool isDefault = false;
    bool operator==(const LayoutSaveDirectoryDisplay&) const = default;
};
LayoutSaveDirectoryDisplay FormatLayoutSaveDirectory(const std::string& directory, const std::string& defaultFolder,
                                                     const Localization& localization);

} // namespace rs::app
