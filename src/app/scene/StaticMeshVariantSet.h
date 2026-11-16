//
// StaticMeshVariantSet.h - 静的インスタンス化用の「_CAMDISTバンドごとに焼いたメッシュ」の集合
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "../../render/mesh/Mesh.h"
#include "StaticHeadlight.h"

namespace rs::app {

// バンド開始値bandStartが入るバンドの番号。bandStartsに無い値は直近下のバンド、先頭より小さい値は
// 先頭のバンド。bandStartsが空なら0。
size_t BandIndexOfStart(const std::vector<int32_t>& bandStarts, int32_t bandStart);

// 1フォルダ+スイッチ確定値につき1つ。バンド(_CAMDIST)と共有状態(_NIGHT/_SEASON/_SHADOW/_ENVMAP)の
// 組ごとに焼いたメッシュを、焼き上がりが同一のものを1つにまとめて持つ。どちらも参照しない
// プラグインでは bandStarts が空・sharedStateMask が0で variants が1個。
struct StaticMeshVariantSet {
    // _CAMDISTを参照しない場合は空。参照する場合はPluginSession::CamDistBandStarts()の
    // コピー(昇順・重複なし)。
    std::vector<int32_t> bandStarts;
    // 参照している共有状態の軸のビットマスク(SharedStateAxesのkSharedStateAxesの添字がビット番号)。
    // 参照しなければ0。
    uint32_t sharedStateMask = 0;
    // 焼き上がりが異なるメッシュの一覧。空メッシュ(全パーツ非表示のバンド)も有効な要素。
    std::vector<rs::render::Mesh> variants;
    // (状態スロット, バンド)→variantsの添字。添字は「状態スロット×BandCount()+バンド番号」。
    // 長さは状態スロット数×BandCount()。バンドも共有状態も参照しないときは空でもよい
    // (その場合の添字は常に0)。
    std::vector<uint32_t> variantOfBand;
    // (状態スロット, バンド)ごとの、点灯条件が真の灯具。添字は variantOfBand と同じ作り方
    // (状態スロット×BandCount()+バンド番号)。灯具が無い組は nullptr。静的バッチ側の灯具のレンズフレアが使う
    // (StaticHeadlightRoster)。共有するのは、キャッシュを切り詰めても個体の一覧が灯具を持ち続けられるように。
    std::vector<std::shared_ptr<const std::vector<StaticHeadlight>>> headlightsOfVariant;

    bool ReferencesCamDist() const { return !bandStarts.empty(); }
    // どれかの組に灯具があるか。
    bool HasAnyHeadlight() const;
    bool ReferencesSharedState() const { return sharedStateMask != 0; }

    // バンドの個数。_CAMDISTを参照しない場合は1。
    size_t BandCount() const { return bandStarts.empty() ? 1 : bandStarts.size(); }

    // 配置ごと・状態ごとにバッチを切り替える対象か。バリアントが2個以上で、
    // _CAMDISTを参照するか共有状態を参照する(共有状態の軸は取りうる値が必ず2通り以上)。
    // 偽なら、全配置が1つのバッチ(または共有状態によらず同じメッシュ)で済む。
    bool NeedsVariantSwitching() const {
        return variants.size() > 1 && (ReferencesCamDist() || ReferencesSharedState());
    }

    // 状態スロットstateSlotとバンド開始値bandStartに対応するvariantsの添字。bandStartsに
    // 無い値は直近下のバンドとして扱い、先頭より小さい値は先頭のバンドとして扱う。
    // _CAMDISTを参照しない場合はbandStartを見ない。表の範囲外なら0。
    size_t VariantIndex(size_t stateSlot, int32_t bandStart) const;

    // 状態スロット0でのVariantIndex(_CAMDISTだけを参照するときの引き)。
    size_t VariantIndexForBandStart(int32_t bandStart) const { return VariantIndex(0, bandStart); }
};

// 2つのメッシュが頂点・インデックス・サブメッシュ・マテリアルまで完全に同一か。
// 浮動小数はビット列で比べる(別物を同一とみなす誤判定を出さない側に倒す)。
bool MeshesIdentical(const rs::render::Mesh& a, const rs::render::Mesh& b);

} // namespace rs::app
