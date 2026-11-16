//
// PluginThumbnailRailMesh.h - レール系(Rail/Tie/Girder/Line)のサムネイル用メッシュを作る
//
// プラグインを、(表で決めた長さの)直線に押し出して描く。
// 単体のサムネイルは積み上げる相手(Rail・Tie)がいないので、沈め量・持ち上げ量は0のまま。
// 敷設プラグインセットの合成は、敷設と同じ値(ExtrusionOffsetY・Rail+Tieの高さ)を渡す。
// GPUには触らない(ワーカースレッドで呼ぶ)。
//
#pragma once

#include "../../plugins/load/PluginLoader.h"
#include "../../render/mesh/Mesh.h"
#include "BrowserKindCatalog.h"

namespace rs::app::plugin_browser {

// trackLengthM は直線の長さ[m]。
// Rail/Tie/Girder: 断面の押し出しと、Intervalのモデルの反復を、直線に沿って1メッシュにまとめる。
// extraOffsetY は押し出しと反復の両方に足す上下の移動量(沈めるなら負)。
// 押し出し情報(RailInfo等)を持たない・メッシュが空なら false。
bool BuildExtrudedThumbnailMesh(const rs::legacy_plugin::LoadedPlugin& plugin, float trackLengthM,
                                  rs::render::Mesh& meshOut, float extraOffsetY = 0.0f);

// Line: 直線の両端(0mと表の長さ)のトロリ線の間に張る架線。liftY は架線の基準点の持ち上げ量。
// メッシュが空なら false。
bool BuildLineThumbnailMesh(const rs::legacy_plugin::LoadedPlugin& plugin, float trackLengthM,
                                  rs::render::Mesh& meshOut, float liftY = 0.0f);

// 種別表の作り方(ThumbnailMethod)に合わせて、上の2つのどちらかで直線へ押し出す。
// サムネイルと単体表示(長さだけが違う)で共用する。Model(押し出さない種別)・失敗は false。
// extrusionOffsetY は Extrude のときの沈め量、lineLiftY は Line のときの持ち上げ量(使わない種別では無視する)。
bool BuildTrackKindMesh(ThumbnailMethod method, const rs::legacy_plugin::LoadedPlugin& plugin,
                        float trackLengthM, rs::render::Mesh& meshOut, float extrusionOffsetY = 0.0f,
                        float lineLiftY = 0.0f);

// 単体表示(プラグインビューアー・--openplugin)で、組立メッシュを持たないプラグインを見せるメッシュ。
struct ViewerDisplayMesh {
    rs::render::Mesh mesh;
    // 真なら種別表の長さの直線へ押し出したメッシュ。偽なら最初にメッシュが読めたパーツ1個。
    bool extruded = false;
    // 押し出したときの最初のカメラを合わせる範囲。直線(原点から+Z方向)全体ではなく中央の短い区間
    // (サムネイルの長さ×kViewerCameraFocusScale)にする。断面の形が読み取れる近さにするためで、
    // 直線の前後は画面からはみ出る。
    float focusMin[3] = {0.0f, 0.0f, 0.0f};
    float focusMax[3] = {0.0f, 0.0f, 0.0f};
};

// 種別表に行があり押し出す種別(Model以外)なら、viewerTrackLengthMの直線へ押し出して返す。
// 押し出す種別でない・押し出しに失敗したときは false。
bool BuildViewerExtrudedMesh(const rs::legacy_plugin::LoadedPlugin& plugin, ViewerDisplayMesh& out);

// 最初にメッシュが読めた部品(plugin.models の添字)。無ければ -1。
int FindFirstDisplayableModel(const rs::legacy_plugin::LoadedPlugin& plugin);

// 組立メッシュを持たないプラグインの表示メッシュ。押し出し→最初に読めたパーツの順で選ぶ。何も出せなければ false。
bool BuildViewerDisplayMesh(const rs::legacy_plugin::LoadedPlugin& plugin, ViewerDisplayMesh& out);

} // namespace rs::app::plugin_browser
