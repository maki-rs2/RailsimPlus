//
// UserSettings.h - ユーザープロファイル設定(settings.dat)の読み書き。
//
#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "../../render/effects/LensFlareSwitches.h"
#include "../../render/postprocess/PostProcessPlan.h"
#include "../../render/postprocess/TonemapMath.h"
#include "../locale/Localization.h"
#include "../plugin_browser/BrowserCardSizeCatalog.h"
#include "ViewOperationMode.h"

namespace rs::app {

struct UserSettingsData {
    // 音量倍率は無音から等倍まで。画面では百分率で表示する。
    static constexpr float kMinAudioVolume = 0.0f;
    static constexpr float kMaxAudioVolume = 1.0f;
    static constexpr float kVolumePercentScale = 100.0f;
    static constexpr float kMinAudioVolumePercent = kMinAudioVolume * kVolumePercentScale;
    static constexpr float kMaxAudioVolumePercent = kMaxAudioVolume * kVolumePercentScale;
    static float ClampAudioVolume(float volume) {
        return std::clamp(volume, kMinAudioVolume, kMaxAudioVolume);
    }

    Language language = Language::English;
    bool updateCheckEnabled = true;
    // 最後に通信を始めた時刻(UTCのepoch秒)。0は未確認。
    std::int64_t lastUpdateCheckTime = 0;
    std::string dismissedUpdateVersion;
    std::vector<std::string> pluginRoots;
    // プラグインブラウザのお気に入り。ルートからの相対パス("Rail/foo"の形)。
    // ルートを入れ替えても同じプラグインを指せるよう、絶対パスでは持たない。
    std::vector<std::string> favoritePlugins;
    // 種別ごとの既定プラグイン。キーは種別名(PluginTypeName、"Surface"の形)、値はルートからの
    // 相対パス("Surface/Default"の形)。favoritePluginsと同じく絶対パスでは持たない。
    std::map<std::string, std::string> defaultPlugins;
    // プラグインブラウザのカードの大きさ(settings.datには表のキー"large"/"medium"/"small"で保存)。
    plugin_browser::BrowserCardSize browserCardSize = plugin_browser::kDefaultBrowserCardSize;
    // 画面(RmlUi)のUIテーマ名(settings.datには"uiTheme"で保存。名前はassets/config/ui_theme.jsonのthemesのキー)。
    // 空 = 既定(ui_theme.jsonのbaseTheme)。テーマの一覧はRmlUi層が持つので、読めない名前はここでは
    // そろえず、使う側(ResolveUiThemeName)が既定にする。
    std::string uiTheme;
    std::string layoutSaveDirectory;
    // 最近使ったレイアウトの絶対パス(使った順)。
    std::vector<std::string> recentLayouts;
    bool autoRecoverEnabled = true;
    int autoRecoverIntervalMinutes = 5;
    // OpenAL Softの再生デバイス名(UTF-8)。空 = システム既定(自動追従が働く)。
    std::string audioOutputDeviceName;
    // 全体音量(0〜1)。audio_tuning.jsonのmasterVolumeに掛かる。
    float audioMasterVolume = 1.0f;
    // 列車音(車輪音・編成車両のSoundEffect)と環境音(情景のSoundEffect)の音量倍率(0〜1)。
    float audioTrainVolume = 1.0f;
    float audioEnvironmentVolume = 1.0f;
    // 操作の効果音(UI音)の音量倍率(0〜1)。
    float audioEffectsVolume = 1.0f;
    // 大気遠近(遠くのメッシュが空の色にかすむ効果)を使うか(settings.datには"atmosphericPerspective"で保存)。
    // 読めないときは真。起動時の --setting atmosphericPerspective で上書きできる。
    bool atmosphericPerspective = true;
    // 高さフォグ(低い所に溜まる霧)を使うか(settings.datには"heightFog"で保存)。
    // 読めないときは真。起動時の --setting heightFog で上書きできる。
    bool heightFog = true;
    // トーンマップの曲線の名前(settings.datには"tonemap"で保存。名前はTonemapModeName)。
    std::string tonemapMode = rs::render::TonemapModeName(rs::render::kDefaultTonemapMode);
    // シャドウ品質(settings.datには"shadowQuality"で保存。文字列はShadowQuality.hのkShadowQualityChoices)。
    // 読めない値は既定(低)になる。
    std::string shadowQuality = "low";
    // シャドウ距離の選択肢(settings.datには"shadowDistance"で保存。文字列はShadowDistance.hのkShadowDistanceChoices)。
    // 読めない値は既定(標準)になる。
    std::string shadowDistance = "standard";
    // レンズフレア・白飛びの効果スイッチ(settings.datには"sunLensFlare"・"sunWhiteout"・"miscLensFlare"で保存)。
    // 順に、太陽のフレア・太陽の白飛び・灯具のフレア。読めないときは真(出す)。
    bool sunLensFlare = true;
    bool sunWhiteout = true;
    bool miscLensFlare = true;
    bool miscParticle = true;
    // ブルーム(明るい光のまわりの滲み)を掛けるか(settings.datには"bloom"で保存)。読めないときは真。
    bool bloom = true;
    // モーションブラー(動くものの流れる滲み)を掛けるか(settings.datには"motionBlur"で保存)。読めないときは偽。
    bool motionBlur = false;
    // ボリュメトリックフォグ(霧に差し込む光の筋)を使うか(settings.datには"volumetricFog"で保存)。読めないときは偽。
    bool volumetricFog = false;
    // アンチエイリアスの方式(settings.datには"aa"で保存。文字列はPostProcessPlan.hのkAntiAliasingChoices)。
    // 読めない値は既定(FXAA)になる。
    std::string antiAliasing = "fxaa";
    // アンビエントオクルージョン(GTAO)の品質(settings.datには"ambientOcclusion"で保存。文字列はAoQuality.hのkAoQualityChoices)。
    // 読めない値は既定(中)になる。
    std::string ambientOcclusion = "medium";
    // VSync(settings.datには"vsync"で保存)。
    bool vsync = true;
    // FPS上限(settings.datには"fpsLimit"の整数で保存。0=無制限)。VSyncがオフのときだけ有効になる。
    // 選択肢(FpsLimitChoices.h)に無い値は無制限になる。
    int fpsLimit = 0;
    // 全画面表示(枠なしでモニター全体に広げる。settings.datには"fullscreen"で保存)。読めないときは偽。
    bool fullscreen = false;
    // キー割当の上書き(settings.datには"keyBindings"の文字列で保存。1行1バインドの
    // action,context,key,ctrl,shift,alt,held。既定と違う操作の行だけを持ち、すべて既定なら空=項目を書かない)。
    std::string keyBindings;
    // カメラ画面のファインダーに重ねる三分割の線と水平線を出すか(settings.datには"cameraFinderGrid"・
    // "cameraFinderHorizon"で保存)。読めないときは出す。
    bool cameraFinderGrid = true;
    bool cameraFinderHorizon = true;
    // 俯瞰カメラの視点操作(settings.datには"viewOperation"で保存。文字列はViewOperationMode.hのkViewOperationChoices)。
    // 読めない値は既定(WASD)になる。
    ViewOperationMode viewOperation = kDefaultViewOperationMode;
    // 読み込んだsettings.datの内容そのもの。このバージョンが知らないキーを、保存で消さないために持つ。
    nlohmann::json loadedDocument = nlohmann::json::object();
    // JSONには保存せず、設定を読み込んだファイルを次回の保存先として保持する。
    std::filesystem::path sourcePath;
};

class UserSettings {
public:
    static std::filesystem::path DefaultPath();
    static std::filesystem::path ExecutableDirectory();
    static std::filesystem::path DefaultLayoutSaveDirectory();
    static UserSettingsData Load(const std::filesystem::path& path);
    static bool Save(const std::filesystem::path& path, const UserSettingsData& data);
};

// 設定の3つのスイッチを描画側の型へ移す(起動時と設定変更時で同じ変換を使う)。
rs::render::LensFlareSwitches LensFlareSwitchesFrom(const UserSettingsData& data);

// ポストプロセスのスイッチを描画側の型へ移す(起動時と設定変更時で同じ変換を使う)。
rs::render::PostProcessSettings PostProcessSettingsFrom(const UserSettingsData& data);

// 保存に失敗したらfailureMessageを警告ログへ出す。Actionが設定を変えた直後の保存に使う。
bool SaveUserSettingsOrWarn(const UserSettingsData& data, const char* failureMessage);

} // namespace rs::app
