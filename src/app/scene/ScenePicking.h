//
// ScenePicking.h - シーン内オブジェクト選択の共通ヒットテスト基盤。
//
// 「ワールド行列+ローカルAABB」の組だけを入力に取るため、配置物の由来
// (TrackPlugin/Scenery/Train等)を問わない。各カテゴリ固有の候補列挙
// (どのメッシュ/AABBを使うか)は呼び出し側が担い、ここでは画面空間の
// ヒットテストだけを共通化する。
//
#pragma once

#include <cstdint>
#include <vector>

#include "../../core/Math.h"
#include "../gpu/CameraMath.h"

namespace rs::app {

// ピッキング候補1件。hasAabb==falseの場合はworldMatrixの平行移動成分(原点)を
// 対象点として扱い、固定半径のみでヒット判定する(モデル読込失敗時のフォール
// バック、PickTrackPluginPlacementの2nd passと同じ規約)。
struct ScreenPickCandidate {
    rs::core::Mat4 worldMatrix;
    bool hasAabb = false;
    float aabbMin[3] = {0.0f, 0.0f, 0.0f};
    float aabbMax[3] = {0.0f, 0.0f, 0.0f};
};

// ローカルAABBの8頂点をworldMatrixでワールドへ変換して返す。頂点の番号は、ビット0がX・ビット1がY・
// ビット2がZで、立っていればaabbMax、立っていなければaabbMinの値(辺は番号が1ビットだけ違う頂点の組)。
// 変換後のwが0に近い頂点があればfalse。
bool TransformAabbCorners(const rs::core::Mat4& worldMatrix, const float aabbMin[3],
                          const float aabbMax[3], rs::core::Vec3 outCorners[8]);

// worldMatrixで変換したAABB8頂点をスクリーンへ投影し、そのバウンディング矩形を
// 返す(全頂点が画面外ならfalse)。
bool ProjectAabbScreenRect(const rs::core::Mat4& worldMatrix, const float aabbMin[3],
                          const float aabbMax[3], const rs::core::Mat4& viewProj, unsigned int width,
                          unsigned int height, float& outMinX, float& outMinY, float& outMaxX,
                          float& outMaxY);

bool RayIntersectAabb(const rs::core::Ray& ray, const rs::core::Mat4& worldMatrix,
                      const float aabbMin[3], const float aabbMax[3], float& outTEntry,
                      float& outTExit);

// RayIntersectAabbは8頂点をワールド空間へ変換した後、そこからワールド軸並行の
// 外接AABBを作り直して判定する近似(worldMatrixに回転が乗っていると、実際の
// オブジェクト(OBB)より一回り大きく・軸方向にズレた領域になる。回転した配置のホバー判定が実モデルとズレる原因になる)。
// こちらはレイをworldMatrixの逆行列でローカル空間へ変換し、回転の影響を受けない
// ローカルAABB(aabbMin/aabbMax)へ直接スラブ判定する厳密版。worldMatrixが特異
// (逆行列が求まらない)ならfalseを返す。PickByRayは全候補にこれを毎回呼ぶと
// 数千候補規模で逆行列コストが嵩むため、RayIntersectAabbで絞り込んだ候補にのみ
// 適用する(2nd pass的な精密化)。
bool RayIntersectObb(const rs::core::Ray& ray, const rs::core::Mat4& worldMatrix,
                     const float aabbMin[3], const float aabbMax[3], float& outTEntry,
                     float& outTExit);

// 1st pass: hasAabbな候補をまずRayIntersectAabb(ワールド軸並行、軽い)で粗く絞り込み、
// 通過した候補だけRayIntersectObb(ローカル空間、厳密)で精密化する。精密判定で外れた
// 候補(粗判定だけの偽陽性)はここで除外する。残った候補からレイに沿った最近傍
// (tEntry最小)を採用する。重なる(=手前候補のtEntry〜tExit区間に他候補のtEntryが
// 収まる、入れ子AABB)場合はその中でワールド空間AABBの対角線長(二乗)が最小の
// ものを優先する(単純な体積だと、地面/路面等の極端に薄い巨大平面が体積上は
// 小さく評価され、その上の小さな物体より優先されてしまうため)。maxDistはレイ長の上限(誤ヒット防止、通常は非常に大きい値でよい)。
// 2nd pass: hasAabb==falseの候補のみ、原点1点+`WorldRadiusForPixels(viewProj, width,
// height, screenX, screenY, 候補原点, pxRadius)`で算出したワールド半径球とレイの
// 距離判定にフォールバックする(候補ごとに距離が異なるため、画面空間のpxRadius相当を
// 候補の実際の奥行きから毎回算出する。maxDistを流用すると、
// 呼び出し側がAABB用の大きな値を渡したとき実質無制限になるため分離している)。
bool PickByRay(const std::vector<ScreenPickCandidate>& candidates, const rs::core::Ray& ray,
               float maxDist, const rs::core::Mat4& viewProj, uint32_t width, uint32_t height,
               float screenX, float screenY, float pxRadius, size_t& outIndex);

// candidatesから(screenX,screenY)に最も近い1件を選ぶ。
// 1st pass: AABBを持つ候補をスクリーン矩形(pxRadiusぶん余白)でヒット判定し、
// 複数ヒットは面積最小(=最も的が小さい=クリック意図に近い)を優先する。
// 2nd pass: AABB無しの候補のみ、原点1点+固定pxRadiusのフォールバックで判定する。
bool PickByScreenRect(const std::vector<ScreenPickCandidate>& candidates, float screenX,
                     float screenY, const rs::core::Mat4& viewProj, unsigned int width, unsigned int height,
                     float pxRadius, size_t& outIndex);

} // namespace rs::app
