//
// RailSoundLookup.h - 軌道上の位置から、車輪音を鳴らすRailを引く。
//
// 取り込みedgeもエディタで敷いたedgeも、edgeの区間ごとのプラグイン一覧（pluginSections）が
// 同じ形で持つので、出自で分けずに同じ関数で引く。
//
#pragma once

#include <functional>
#include <string>

#include "PluginSelector.h"

namespace rs::world {
struct TrackEdge;
} // namespace rs::world

namespace rs::app {

// フォルダ名から読込済みのレール系エントリを引く関数（PluginSelector::FindTrackPluginEntryなど）。
using TrackPluginFinder = std::function<const TrackPluginEntry*(const std::string&)>;

// SoundInfoが鳴らせる内容か（ファイル名が空でなく、継目間隔が有限の正の値）。
bool IsWheelSoundAudible(const rs::legacy_pi::RailSoundDef& sound);

// edgeの弧長sの区間に乗っているRailのうち、区間の並び順で最初のRailを返す。そのRailが
// 鳴らせるSoundInfoを持たないとき、またはRailが無いときはnullptr（無音）。
const TrackPluginEntry* FindWheelSoundRail(const rs::world::TrackEdge& edge, double s,
                                           const TrackPluginFinder& findEntry);

// RailのSoundInfoの音声ファイルを絶対パスへ解決する（存在確認はしない）。
std::wstring ResolveWheelSoundPath(const TrackPluginEntry& rail);

} // namespace rs::app
