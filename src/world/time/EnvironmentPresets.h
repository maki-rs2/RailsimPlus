//
// EnvironmentPresets.h - 緯度・地軸傾斜・初期日時のプリセット値をJSONから読む。
//
// legacy RailSim II ではこれらはEnv2.txt(Envプラグイン資産)由来。本クラスは自前プリセットの
// 置き場所で、固定値のJSON読み込みのみを行う(Env2.txtから自動で取り出すことはしない)。
// 太陽の通り道の緯度だけは、アクティブなSceneのEnvプラグインがEnvInfo.Latitudeを持つ
// ときそちらを優先し、このプリセットの緯度はEnvなし・キー無しの既定値として使う
// (ActiveEnv::SolarLatitudeDeg)。
//
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "CalendarUtil.h"

namespace rs::world {

struct EnvironmentPreset {
    std::string name;
    double latitudeDeg = 35.5;        // 実データでの緯度の例
    double axialInclinationDeg = 23.44; // 地球の値(既定)
    DateParts initialDate{2026, 1, 1};
    double initialSecondsOfDay = 0.0;
    // 夜判定の閾値。RailSim II の CEnvPlugin.cpp "sunalt<m_NightThreshold"の
    // sunaltはsin(太陽高度)であり弧度ではないため、ここもsin値で保持する
    // (既定0.0=水平線。PluginSession::UpdateTimeSwitchesがSolarState::
    // directionToSunYと直接比較する)。
    double nightThresholdSin = 0.0;
};

class EnvironmentPresets {
public:
    // 実行ファイルへ埋め込まれたプリセット定義を読み込む(実行時の既定経路)。
    // 開発側が決める調整パラメータであり配布物に外部ファイルを置かないため、
    // ビルド時にJSONを焼き込んだものを使う(CMakeLists.txt の
    // rs_embed_config_json / 生成物 EmbeddedEnvironmentPresets.h)。
    bool LoadEmbedded();

    // utf8Path の JSON ファイルからプリセット一覧を読み込む。
    // 失敗時はプリセットを追加せず false を返す(呼び出し側は既定値を使う)。
    // 埋め込みへ移行したため実行時経路では使わない(任意のJSONを食わせる
    // テスト専用)。
    bool LoadFromFile(const std::string& utf8Path);

    const std::vector<EnvironmentPreset>& Presets() const { return presets_; }

    // name に一致するプリセットを返す。無ければ nullptr。
    const EnvironmentPreset* Find(const std::string& name) const;

    // 読み込んだ中の既定プリセット(JSONの"default"名で指定)。無ければ
    // Presets()[0]、Presets()も空ならプリセット既定値(ヘッダ内既定値)を返す。
    const EnvironmentPreset& Default() const;

private:
    // JSONテキスト(UTF-8バイト列)を解析して presets_/defaultName_ を埋める。
    // LoadEmbedded / LoadFromFile の共通実装。
    bool ParseJsonText(const char* text, std::size_t size, const std::string& sourceLabel);

    std::vector<EnvironmentPreset> presets_;
    std::string defaultName_;
};

// リポジトリ標準配置(assets/config/environment_presets.json)を解決する。
// rs::platform_file::ResolveAssetPath へ委譲する(ソースツリー直下→
// カレントディレクトリ相対)。見つからなければ空文字列を返す。
// 実行時は埋め込み(LoadEmbedded)を使うため、これはテスト用途に残している。
std::string ResolveEnvironmentPresetsPath();

} // namespace rs::world
