//
// TemplateDefBuilder.h - 敷設プラグインセット・編成を、テンプレート保存用の型付きデータへ変換する
//
// 書き出した本文は、読込側(ApplyRailwayPluginSetFile / ParseTrainGroupTemplateFile)が
// <インストールルート>/<種別フォルダ>/<プラグイン名> へ解決する。そのため、プラグインの
// フォルダは「種別フォルダの下の相対名」へ直して書く。
//
#pragma once

#include <string>
#include <vector>

#include "TrackPluginSetAction.h"
#include "../plugin_session/Formation.h"
#include "../../editor/tool/TrackLayingSettings.h"
#include "../../plugins/layout/RailwayPluginSetSchema.h"
#include "../../plugins/layout/TrainGroupTemplateSchema.h"

namespace rs::app {

// folderUtf8 が <ルート>/<kindDir>/<名前> の形のとき、<名前>(下位フォルダを含む相対名、区切りは "\")を返す。
// 登録済みのルート(rootsUtf8)の下にあればそれを使い、無ければパスの中の kindDir の
// 最後の出現を探す。どちらも見つからなければフォルダ名だけを返す。kindDir の比較は大文字小文字を区別しない。
std::string PluginNameUnderKind(const std::string& folderUtf8, const char* kindDir,
                                const std::vector<std::string>& rootsUtf8);

// 読み込めた項目を種類ごとにまとめ、各種類の先頭を専用の欄(*Check=true)へ、2つ目以降を
// extraPluginsへ入れる。敷設パラメータは settings の現在値。対応する欄の無い
// enableCant / liftRailSurface は既定値のまま。
rs::legacy_layout::RailwayPluginSetDef BuildRailwayPluginSetDef(
    const TrackPluginSetViewModel& set, const rs::editor::TrackLayingSettings& settings,
    const std::string& settingName, const std::vector<std::string>& rootsUtf8);

// 各車を TrainTemplateEntry へ移す。読めなかった車両も参照が消えないよう含める。
rs::legacy_layout::TrainGroupTemplateDef BuildTrainGroupTemplateDef(
    const Formation& formation, const std::vector<std::string>& rootsUtf8);

} // namespace rs::app
