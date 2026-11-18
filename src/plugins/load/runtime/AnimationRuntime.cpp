#include "AnimationRuntime.h"

#include <algorithm>
#include <cmath>
#include "../../../core/Epsilon.h"
#include "../../../core/Math.h"
#include "../../schema/PluginSchema.h"

namespace rs::legacy_plugin {

using rs::legacy_pi::AnimationFrame;
using rs::legacy_pi::TriggerInfo;

namespace {


// frame文1件分の表示時間(秒)。Simple/Numbered は durationFrames そのもの、
// UV系は frameCount 個のサブフレームを durationFrames ずつ回すループ全体の長さ。
// durationFrames が表す「1フレーム」の実時間は定義ファイルの版で異なる
// (RailSim II -k-build版=60fps基準 / 標準版=30fps基準)ため、frameRateFps を呼び出し側から
// 受け取る。RailSim IIのHelpは一律「1/30秒単位」と記述しているが、実際の挙動を優先する。
float FrameStmtSeconds(const AnimationFrame& f, float frameRateFps) {
    int dur = f.durationFrames > 0 ? f.durationFrames : 1;
    if (f.kind == AnimationFrame::Kind::Simple || f.kind == AnimationFrame::Kind::Numbered)
        return float(dur) / frameRateFps;
    int count = f.frameCount > 0 ? f.frameCount : 1;
    return (float(dur) / frameRateFps) * float(count);
}

bool IsUvKind(AnimationFrame::Kind k) {
    return k == AnimationFrame::Kind::SlideUV || k == AnimationFrame::Kind::TiledUV ||
           k == AnimationFrame::Kind::RotationUV;
}

// UV系1frame文のサブフレーム k(0-based)における2x3アフィン変換。
// p0..p3の意味は種別ごとに異なる(Help pi_sym_animation_*_uv_frame.html準拠)。
//   TiledUV:    p0,p1=u,vスケール p2,p3=タイルu,vサイズ
//               （実データ(車両プラグイン): 0.25,0.25,0.25,0.25,frameCount16 →
//                 1列あたり4枚(=round(1/0.25))×4行=16枚で一致、スケール=タイル
//                 サイズと揃えるのが定石という Help の記述とも合致）
//   SlideUV:    p0,p1=初期u,v座標 p2,p3=フレーム当たりの移動u,v量
//   RotationUV: p0=初期回転角[deg] p1=フレーム当たりの回転角[deg] p2,p3=回転中心u,v
UvTransform ComputeUv(const AnimationFrame& f, int k) {
    UvTransform t;
    switch (f.kind) {
    case AnimationFrame::Kind::TiledUV: {
        int perRow = (f.p2 > rs::core::kAnimationUvFrameWidthEps) ? std::max(1, int(std::lround(1.0f / f.p2))) : 1;
        int col = k % perRow;
        int row = k / perRow;
        t.a = f.p0; t.d = f.p1; t.b = 0; t.c = 0;
        t.tx = col * f.p2; t.ty = row * f.p3;
        break;
    }
    case AnimationFrame::Kind::SlideUV: {
        t.a = 1; t.d = 1; t.b = 0; t.c = 0;
        t.tx = f.p0 + k * f.p2;
        t.ty = f.p1 + k * f.p3;
        break;
    }
    case AnimationFrame::Kind::RotationUV: {
        float angleDeg = f.p0 + k * f.p1;
        float rad = angleDeg * (rs::core::kPiF / 180.0f);
        float cs = std::cos(rad), sn = std::sin(rad);
        float cx = f.p2, cy = f.p3;
        // uv' = R(θ)*(uv-center) + center = R*uv + (center - R*center)
        t.a = cs; t.c = -sn; t.b = sn; t.d = cs;
        t.tx = cx * (1.0f - cs) + cy * sn;
        t.ty = -cx * sn + cy * (1.0f - cs);
        break;
    }
    default:
        break; // Simple/Numbered: UV変換は恒等のまま
    }
    return t;
}

// texture-transformer(ShiftTexture/ScaleTexture/RotateTexture/TransformTexture)の
// 即値UV変換(Help pi_sym_texture_transformer.html準拠)。
//   Shift:     p0,p1=du,dv                              （平行移動のみ）
//   Scale:     p0,p1=su,sv p2,p3=du,dv                   （スケール→平行移動）
//   Rotate:    p0=角度[deg](反時計回り) p1,p2=中心u,v      （中心回転、animationのRotationUVと同じ式）
//   Transform: p0..p5=a11,a12,a13,a21,a22,a23             （u'=a11u+a12v+a13, v'=a21u+a22v+a23）
UvTransform ComputeStaticUv(const rs::legacy_pi::TextureTransformCustomizer& tt) {
    using Kind = rs::legacy_pi::TextureTransformCustomizer::Kind;
    UvTransform t;
    switch (tt.kind) {
    case Kind::Shift:
        t.a = 1; t.d = 1; t.b = 0; t.c = 0;
        t.tx = tt.p[0]; t.ty = tt.p[1];
        break;
    case Kind::Scale:
        t.a = tt.p[0]; t.d = tt.p[1]; t.b = 0; t.c = 0;
        t.tx = tt.p[2]; t.ty = tt.p[3];
        break;
    case Kind::Rotate: {
        float rad = tt.p[0] * (rs::core::kPiF / 180.0f);
        float cs = std::cos(rad), sn = std::sin(rad);
        float cx = tt.p[1], cy = tt.p[2];
        t.a = cs; t.c = -sn; t.b = sn; t.d = cs;
        t.tx = cx * (1.0f - cs) + cy * sn;
        t.ty = -cx * sn + cy * (1.0f - cs);
        break;
    }
    case Kind::Transform:
        t.a = tt.p[0]; t.c = tt.p[1]; t.tx = tt.p[2];
        t.b = tt.p[3]; t.d = tt.p[4]; t.ty = tt.p[5];
        break;
    }
    return t;
}

// entry内の累積経過時間 elapsed[秒] から、現在activeなframe文とそのサブフレーム
// (UV系のみ意味を持つ)を特定して適用結果を作る。
AnimationApplyResult EvalEntry(const AnimationEntry& e, float elapsed, int materialId,
                               float frameRateFps) {
    AnimationApplyResult r;
    r.materialId = materialId;
    if (e.frames.empty() || e.totalSeconds <= rs::core::kAnimationFrameDurationEpsS) return r;

    float acc = 0.0f;
    for (size_t fi = 0; fi < e.frames.size(); ++fi) {
        float segSeconds = FrameStmtSeconds(e.frames[fi], frameRateFps);
        bool isLast = (fi + 1 == e.frames.size());
        if (elapsed < acc + segSeconds || isLast) {
            const AnimationFrame& f = e.frames[fi];
            if (IsUvKind(f.kind)) {
                float local = elapsed - acc;
                int dur = f.durationFrames > 0 ? f.durationFrames : 1;
                float perFrame = float(dur) / frameRateFps;
                int count = f.frameCount > 0 ? f.frameCount : 1;
                int k = (perFrame > rs::core::kAnimationFrameDurationEpsS) ? int(local / perFrame) : 0;
                k = std::max(0, std::min(k, count - 1));
                r.uv = ComputeUv(f, k);
                r.isUvNow = true;
                if (fi < e.resolvedTex.size() && !e.resolvedTex[fi].empty()) {
                    r.hasTexture = true;
                    r.texturePath = e.resolvedTex[fi];
                }
            } else if (f.kind == AnimationFrame::Kind::Simple) {
                // Simple(画像差し替え): テクスチャを切り替える。フレームにUV変換が
                // 付いていればUVも上書きし(isUvNow)、無ければUVには触らない。
                if (f.hasTransform) {
                    r.uv = ComputeStaticUv(f.transform);
                    r.isUvNow = true;
                }
                if (fi < e.resolvedTex.size() && !e.resolvedTex[fi].empty()) {
                    r.hasTexture = true;
                    r.texturePath = e.resolvedTex[fi];
                }
            }
            // Numbered: 扱わない(resolvedTexが常に空文字のためhasTexture=falseのまま)。
            return r;
        }
        acc += segSeconds;
    }
    return r;
}

} // namespace

void PartAnimationRuntime::Build(
    const rs::legacy_pi::ModelPart& part,
    const std::vector<rs::legacy_pi::AnimationDef>& defs,
    const std::vector<std::vector<std::string>>& resolvedAnimTex, float frameRateFps) {
    frameRateFps_ = (frameRateFps > rs::core::kAnimationFrameRateEpsHz) ? frameRateFps : 30.0f;
    entries_.clear();
    staticEntries_.clear();
    results_.clear();
    cachedStaticActive_.clear();
    cachedAnimActive_.clear();
    cachedMarkerActive_.clear();
    cachedActiveGeneration_ = ~uint64_t(0); // 再Build後は必ず再評価させる

    steps_.clear();
    markers_.clear();

    // 宣言順にたどって、種類ごとの実体(staticEntries_/entries_/markers_)へ振り分ける。
    // steps_ はその並びを覚えておくだけで、Update が同じ順に1回たどる。
    const bool haveResolved = resolvedAnimTex.size() == defs.size();
    for (const rs::legacy_pi::TextureStep& step : part.textureSteps) {
        if (step.kind == rs::legacy_pi::TextureStep::Kind::Transform) {
            StaticUvEntry e;
            e.materialId = step.transform.materialId;
            e.uv = ComputeStaticUv(step.transform);
            e.trig = step.transform.trig;
            staticEntries_.push_back(std::move(e));
            steps_.push_back({StepKind::Transform, staticEntries_.size() - 1});
        } else if (step.kind == rs::legacy_pi::TextureStep::Kind::ChangeTexture) {
            markers_.push_back({step.changeTexture.materialId, step.changeTexture.trig});
            steps_.push_back({StepKind::ChangeTexture, markers_.size() - 1});
        } else if (AddAnimationEntry(step.setAnimation, defs, resolvedAnimTex, haveResolved)) {
            steps_.push_back({StepKind::Animation, entries_.size() - 1});
        }
    }
    if (entries_.empty() && staticEntries_.empty()) {
        // ChangeTextureの印だけでは、テクスチャの勝ち負けも結果も生まれない
        steps_.clear();
        markers_.clear();
    }
}

bool PartAnimationRuntime::AddAnimationEntry(
    const rs::legacy_pi::SetAnimationCustomizer& sa,
    const std::vector<rs::legacy_pi::AnimationDef>& defs,
    const std::vector<std::vector<std::string>>& resolvedAnimTex, bool haveResolved) {
    for (size_t di = 0; di < defs.size(); ++di) {
        if (defs[di].name != sa.animationName) continue; // 同名の重複は先勝ち(プラグイン側の責務)
        AnimationEntry e;
        e.materialId = sa.materialId;
        e.frames = defs[di].frames;
        e.trig = sa.trig;
        if (haveResolved && resolvedAnimTex[di].size() == defs[di].frames.size())
            e.resolvedTex = resolvedAnimTex[di];
        else
            e.resolvedTex.assign(defs[di].frames.size(), std::string());
        e.totalSeconds = 0.0f;
        for (const auto& f : e.frames) e.totalSeconds += FrameStmtSeconds(f, frameRateFps_);
        e.hasResolvedTexFrame = false;
        for (size_t fi = 0; fi < e.frames.size(); ++fi) {
            if (e.frames[fi].kind == AnimationFrame::Kind::Simple &&
                fi < e.resolvedTex.size() && !e.resolvedTex[fi].empty()) {
                e.hasResolvedTexFrame = true;
                break;
            }
        }
        entries_.push_back(std::move(e));
        return true;
    }
    return false;
}

void PartAnimationRuntime::EnsureActiveCache(const rs::legacy_pi::SwitchResolver& resolve,
                                             uint64_t generation) {
    if (generation == cachedActiveGeneration_) return;
    cachedStaticActive_.resize(staticEntries_.size());
    for (size_t i = 0; i < staticEntries_.size(); ++i)
        cachedStaticActive_[i] = rs::legacy_pi::IsTriggerActive(staticEntries_[i].trig, resolve);
    cachedAnimActive_.resize(entries_.size());
    for (size_t i = 0; i < entries_.size(); ++i)
        cachedAnimActive_[i] = rs::legacy_pi::IsTriggerActive(entries_[i].trig, resolve);
    cachedMarkerActive_.resize(markers_.size());
    for (size_t i = 0; i < markers_.size(); ++i)
        cachedMarkerActive_[i] = rs::legacy_pi::IsTriggerActive(markers_[i].trig, resolve);
    cachedActiveGeneration_ = generation;
}

void PartAnimationRuntime::CollectActiveTimedTextures(const rs::legacy_pi::SwitchResolver& resolve,
                                                      uint64_t generation,
                                                      std::vector<std::string>& out) {
    if (entries_.empty()) return;
    EnsureActiveCache(resolve, generation);
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (!cachedAnimActive_[i]) continue;
        // UV系frame(タイル画像等)・Simple系frame(画像差し替え)のどちらも
        // resolvedTexにテクスチャを持ちうるため区別せず集める。
        for (const std::string& tex : entries_[i].resolvedTex) {
            if (!tex.empty()) out.push_back(tex);
        }
    }
}

void PartAnimationRuntime::CollectTimedTexturesForResolver(
    const rs::legacy_pi::SwitchResolver& resolve, std::vector<std::string>& out) const {
    for (const AnimationEntry& entry : entries_) {
        if (!rs::legacy_pi::IsTriggerActive(entry.trig, resolve)) continue;
        for (const std::string& tex : entry.resolvedTex) {
            if (!tex.empty()) out.push_back(tex);
        }
    }
}

void PartAnimationRuntime::CollectTextureTriggerSwitches(
    const std::vector<std::string>& candidates, std::set<std::string>& referenced) const {
    for (const AnimationEntry& entry : entries_) {
        const bool selectsTexture =
            std::any_of(entry.resolvedTex.begin(), entry.resolvedTex.end(),
                        [](const std::string& tex) { return !tex.empty(); });
        if (!selectsTexture) continue;
        rs::legacy_pi::AddReferencedSwitches(entry.trig, candidates, referenced);
    }
}

void PartAnimationRuntime::CollectStaticUv(const rs::legacy_pi::SwitchResolver& resolve,
                                           uint64_t generation,
                                           std::vector<StaticUvResult>& out) {
    out.clear();
    if (staticEntries_.empty()) return;
    EnsureActiveCache(resolve, generation);

    std::vector<int> materialOrder;
    for (size_t i = 0; i < staticEntries_.size(); ++i) {
        if (!cachedStaticActive_[i]) continue;
        int materialId = staticEntries_[i].materialId;
        size_t oi = out.size();
        bool found = false;
        for (size_t j = 0; j < materialOrder.size(); ++j) {
            if (materialOrder[j] == materialId) { oi = j; found = true; break; }
        }
        if (!found) {
            materialOrder.push_back(materialId);
            StaticUvResult r;
            r.materialId = materialId;
            r.uv = staticEntries_[i].uv;
            out.push_back(r);
        } else {
            out[oi].uv = staticEntries_[i].uv; // 後勝ち
        }
    }
}

void PartAnimationRuntime::Update(double clockSeconds, const rs::legacy_pi::SwitchResolver& resolve,
                                  uint64_t generation) {
    results_.clear();
    if (entries_.empty() && staticEntries_.empty()) return;

    // スイッチ値が前回から変わっていなければ(generation不変)、各エントリの
    // active判定(IsTriggerActive呼び出し)をスキップして前回の結果を使い回す。
    // 時間経過に伴うUVフレーム送り(下の位相計算/EvalEntry)は generation と
    // 無関係に毎フレーム必要なため、ここでは「activeかどうか」のみキャッシュする。
    EnsureActiveCache(resolve, generation);

    // 宣言順に1回たどり、materialId ごとに UV とテクスチャの勝者を決める
    // (同じ材質ではあとの宣言が勝つ。規則はヘッダの冒頭コメント)。
    // 結果に載せる材質は Transform/SetAnimation の対象になったものだけ。ChangeTexture は、
    // すでに対象になっている材質のアニメのテクスチャを打ち消す(実体はPartMaterialRuntime)。
    // textureCapable は(activeかどうかに関わらず)テクスチャを切り替えうるアニメを持つ材質。
    struct MaterialState {
        int materialId = 0;
        UvTransform uv;
        bool uvFromAnimation = false;
        bool hasAnimationTexture = false; // テクスチャの勝者がアニメか
        std::string animationTexture;
        bool textureCapable = false;
    };
    std::vector<MaterialState> states;
    states.reserve(staticEntries_.size() + entries_.size());
    const auto findState = [&states](int materialId) -> MaterialState* {
        for (MaterialState& state : states)
            if (state.materialId == materialId) return &state;
        return nullptr;
    };
    const auto touchState = [&](int materialId) -> MaterialState& {
        MaterialState* found = findState(materialId);
        if (found != nullptr) return *found;
        states.emplace_back();
        states.back().materialId = materialId;
        return states.back();
    };

    // 再生位相はエントリ個別の経過時間ではなく、呼び出し側の共有時計
    // clockSeconds からの fmod で求める(同一周期のアニメが全エントリで同位相に
    // なり切り替えタイミングが揃う+dt累積のfloat誤差が出ない)。
    for (const StepRef& step : steps_) {
        if (step.kind == StepKind::Transform) {
            MaterialState& state = touchState(staticEntries_[step.index].materialId);
            if (cachedStaticActive_[step.index]) {
                state.uv = staticEntries_[step.index].uv;
                state.uvFromAnimation = false;
            }
        } else if (step.kind == StepKind::ChangeTexture) {
            if (!cachedMarkerActive_[step.index]) continue;
            MaterialState* state = findState(markers_[step.index].materialId);
            if (state != nullptr) {
                state->hasAnimationTexture = false;
                state->animationTexture.clear();
            }
        } else {
            const AnimationEntry& entry = entries_[step.index];
            MaterialState& state = touchState(entry.materialId);
            if (entry.hasResolvedTexFrame) state.textureCapable = true;
            if (!cachedAnimActive_[step.index]) continue;
            const float phase = (entry.totalSeconds > rs::core::kAnimationCycleEpsS)
                                    ? float(std::fmod(clockSeconds, double(entry.totalSeconds)))
                                    : 0.0f;
            AnimationApplyResult eval = EvalEntry(entry, phase, entry.materialId, frameRateFps_);
            if (eval.isUvNow) {
                state.uv = eval.uv;
                state.uvFromAnimation = true;
            }
            if (eval.hasTexture) {
                state.hasAnimationTexture = true;
                state.animationTexture = std::move(eval.texturePath);
            }
        }
    }

    results_.reserve(states.size());
    for (MaterialState& state : states) {
        AnimationApplyResult r;
        r.materialId = state.materialId;
        r.textureCapable = state.textureCapable;
        r.uv = state.uv;
        r.isUvNow = state.uvFromAnimation;
        r.hasTexture = state.hasAnimationTexture;
        if (state.hasAnimationTexture) r.texturePath = std::move(state.animationTexture);
        results_.push_back(std::move(r));
    }
}

} // namespace rs::legacy_plugin
