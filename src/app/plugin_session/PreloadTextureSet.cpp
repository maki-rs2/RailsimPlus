#include "PreloadTextureSet.h"

#include <algorithm>
#include <iterator>
#include <set>

#include <spdlog/spdlog.h>

#include "../../render/pipeline/MeshPipelineTypes.h"
#include "PluginSession.h"

namespace rs::app {

namespace {

// 1つのシステムスイッチについて「実行時に取りうる値」を並べたもの。
struct HypothesisAxis {
    std::string name;
    std::vector<int32_t> values;
};

std::vector<HypothesisAxis> BuildAxes(const PluginSession& session) {
    std::vector<HypothesisAxis> axes;
    for (const char* name : rs::legacy_pi::SystemSwitchNamesWithSource(
             rs::legacy_pi::SystemSwitchSource::StationRuntime)) {
        if (session.SystemSwitches().find(name) == session.SystemSwitches().end()) continue;
        axes.push_back({name, {int32_t(0), int32_t(-1)}});
    }
    const char* camDistName = rs::legacy_pi::SystemSwitchName(rs::legacy_pi::SystemSwitchId::CamDist);
    if (session.SystemSwitches().find(camDistName) != session.SystemSwitches().end()) {
        // UpdateCamDistは生の距離をバンド開始値へ量子化して書くため、_CAMDISTが
        // 実行時に取りうる値の集合はバンド開始値そのもの(CollectCamDistBands)。
        // 量子化不可(条件式が解析できない)プラグインでは生の距離が入りうるので
        // 完全な集合ではないが、解析できた条件ぶんは拾えるので最善努力で使う。
        const std::vector<int32_t>& bands = session.CamDistBandStarts();
        if (!bands.empty()) axes.push_back({camDistName, bands});
    }
    return axes;
}

// ChangeTexture/テクスチャを選ぶSetAnimationの発動条件が参照しているユーザースイッチを、
// 取りうる値(選択肢の数ぶんの 0..n-1)の軸にして返す。ChangeModel専用・色専用の
// スイッチは軸にしない(仮定値の組合せ数が増えて縮退モードを誘発するため)。
// 選択肢が空のスイッチは値の範囲が分からないので軸にしない。
std::vector<HypothesisAxis> BuildUserSwitchAxes(const PluginSession& session) {
    std::vector<std::string> candidates;
    for (const rs::legacy_pi::SwitchDef& def : session.plugin.def.switches) {
        if (!def.entries.empty()) candidates.push_back(def.name);
    }
    std::set<std::string> referenced;
    if (!candidates.empty()) {
        for (const rs::legacy_plugin::PartMaterialRuntime& mrt : session.materialRuntimes)
            mrt.CollectTextureTriggerSwitches(candidates, referenced);
        for (const rs::legacy_plugin::PartAnimationRuntime& art : session.animationRuntimes)
            art.CollectTextureTriggerSwitches(candidates, referenced);
    }

    std::vector<HypothesisAxis> axes;
    std::set<std::string> added;
    for (const rs::legacy_pi::SwitchDef& def : session.plugin.def.switches) {
        if (def.entries.empty() || referenced.count(def.name) == 0) continue;
        if (!added.insert(def.name).second) continue; // 同名の重複宣言は1軸にまとめる
        HypothesisAxis axis;
        axis.name = def.name;
        for (size_t i = 0; i < def.entries.size(); ++i) axis.values.push_back(static_cast<int32_t>(i));
        axes.push_back(std::move(axis));
    }
    return axes;
}

// axesの値をindicesで選んだ1組の仮定値の下で、activeになりうるテクスチャを集める。
void CollectForHypothesis(const PluginSession& session, const std::vector<HypothesisAxis>& axes,
                          const std::vector<size_t>& indices, PreloadTextureSet& out) {
    const rs::legacy_pi::SwitchResolver resolver = [&](const std::string& name) -> int32_t {
        for (size_t axis = 0; axis < axes.size(); ++axis) {
            if (axes[axis].name != name) continue;
            return axes[axis].values[indices[axis]];
        }
        return session.ResolveSwitch(name);
    };
    for (const rs::legacy_plugin::PartAnimationRuntime& art : session.animationRuntimes)
        art.CollectTimedTexturesForResolver(resolver, out.animationTextures);
    for (const rs::legacy_plugin::PartMaterialRuntime& mrt : session.materialRuntimes)
        mrt.CollectTextureCandidatesForResolver(resolver, out.materialTextures);
}

template <typename T>
void SortUnique(std::vector<T>& values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

// axesの仮定値(直積、上限超過なら1次元ずつの縮退)でテクスチャ集合を求める。
PreloadTextureSet CollectForAxes(const PluginSession& session,
                                 const std::vector<HypothesisAxis>& axes) {
    PreloadTextureSet result;

    size_t productCount = 1;
    for (const HypothesisAxis& axis : axes) {
        // 上限を超えた時点で打ち切る(桁溢れ回避も兼ねる)。
        if (axis.values.size() > kMaxPreloadHypotheses / productCount) {
            productCount = kMaxPreloadHypotheses + 1;
            break;
        }
        productCount *= axis.values.size();
    }

    std::vector<size_t> indices(axes.size(), 0);
    if (productCount <= kMaxPreloadHypotheses) {
        // 直積を総当り。スイッチ同士を組み合わせた条件
        // (`If "_CAMDIST" < n & "_APPROACH1"` 等)も漏らさない。
        for (size_t combo = 0; combo < productCount; ++combo) {
            size_t rest = combo;
            for (size_t axis = 0; axis < axes.size(); ++axis) {
                indices[axis] = rest % axes[axis].values.size();
                rest /= axes[axis].values.size();
            }
            CollectForHypothesis(session, axes, indices, result);
        }
        result.hypothesisCount = productCount;
    } else {
        // 縮退モード: 1次元ずつ動かし、他は先頭の値に固定する。組合せ条件の
        // 一部を取りこぼしうるが、読込時間が組合せ爆発するよりは軽い取りこぼしを選ぶ。
        result.degraded = true;
        CollectForHypothesis(session, axes, indices, result);
        result.hypothesisCount = 1;
        for (size_t axis = 0; axis < axes.size(); ++axis) {
            for (size_t value = 1; value < axes[axis].values.size(); ++value) {
                indices[axis] = value;
                CollectForHypothesis(session, axes, indices, result);
                ++result.hypothesisCount;
            }
            indices[axis] = 0;
        }
    }

    result.animationTextures.erase(std::remove(result.animationTextures.begin(),
                                               result.animationTextures.end(), std::string()),
                                   result.animationTextures.end());
    SortUnique(result.animationTextures);
    SortUnique(result.materialTextures);
    return result;
}

// ユニークな3つ組(アニメ分はbaseのみの3つ組)をプールへ登録したときに使うスロット数の合計。
// プールは3つ組ごとに、法線/ORMがあれば3スロット・無ければ1スロットを使う。
size_t CountTextureSlots(const PreloadTextureSet& set) {
    std::set<std::array<std::string, 3>> unique(set.materialTextures.begin(),
                                                set.materialTextures.end());
    for (const std::string& path : set.animationTextures)
        unique.insert({path, std::string(), std::string()});
    size_t slots = 0;
    for (const std::array<std::string, 3>& triple : unique)
        slots += rs::render::TextureSlotCost(triple[1], triple[2]);
    return slots;
}

template <typename T>
void MergeInto(std::vector<T>& dst, const std::vector<T>& src) {
    dst.insert(dst.end(), src.begin(), src.end());
    SortUnique(dst);
}

} // namespace

PreloadTextureSet CollectPreloadTextures(const PluginSession& session,
                                         size_t userSwitchTextureBudget) {
    std::vector<HypothesisAxis> axes = BuildAxes(session);
    PreloadTextureSet baseline = CollectForAxes(session, axes);
    if (userSwitchTextureBudget == 0) return baseline;

    const std::vector<HypothesisAxis> userAxes = BuildUserSwitchAxes(session);
    baseline.userSwitchAxisCount = userAxes.size();
    if (userAxes.empty()) return baseline;

    axes.insert(axes.end(), userAxes.begin(), userAxes.end());
    PreloadTextureSet extended = CollectForAxes(session, axes);
    // 縮退モードは他の軸を先頭値に固定するため、軸を足す前の集合(読込時の確定値の
    // 組合せを含む)を取りこぼしうる。軸を足さない集合との和集合にして、足す前より
    // 減らないようにする。
    MergeInto(extended.animationTextures, baseline.animationTextures);
    MergeInto(extended.materialTextures, baseline.materialTextures);
    if (CountTextureSlots(extended) > userSwitchTextureBudget) {
        baseline.userSwitchAxesOverBudget = true;
        return baseline;
    }
    extended.userSwitchAxesUsed = true;
    extended.userSwitchAxisCount = userAxes.size();
    return extended;
}

size_t PreloadEntryCount(const PreloadTextureSet& set) {
    return set.animationTextures.size() + set.materialTextures.size();
}

void PreloadTextureEntries(rs::render::MeshPipeline& pipeline, ID3D12Device* device,
                           DirectX::ResourceUploadBatch& upload, const PreloadTextureSet& set,
                           size_t begin, size_t end, bool warnOnFailure) {
    const size_t animationCount = set.animationTextures.size();
    end = std::min(end, PreloadEntryCount(set));
    for (size_t i = begin; i < end; ++i) {
        // アニメ分は実行時の適用経路もnormal/pbrを渡さないので、baseだけの3つ組で登録する。
        // ChangeTexture分は仮定値ごとに同じ3つ組が重複しうるが、プールは3つ組キーなので
        // 重複しても再登録されない。
        static const std::string kNone;
        const std::string& base =
            (i < animationCount) ? set.animationTextures[i] : set.materialTextures[i - animationCount][0];
        if (base.empty()) continue;
        const std::string& normal = (i < animationCount) ? kNone : set.materialTextures[i - animationCount][1];
        const std::string& pbr = (i < animationCount) ? kNone : set.materialTextures[i - animationCount][2];
        rs::render::PreloadedTexInfo info;
        if (!pipeline.PreloadTexture(device, upload, base, normal, pbr, info) && warnOnFailure)
            spdlog::warn("[tex] preload failed (slot budget or load error): {}", base);
    }
}

} // namespace rs::app
