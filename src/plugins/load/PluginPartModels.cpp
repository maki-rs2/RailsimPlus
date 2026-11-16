//
// PluginPartModels.cpp - 各パーツのモデル読込(LoadPartModels)と
// ChangeModel候補1件分の動的解決用ロード(LoadModelVariant)。
//
#include "PluginLoadInternal.h"

#include "ModelMeshHelpers.h"
#include "PluginPaths.h"
#include "../../render/resource/XLoader.h"
#include "../../platform/text/Encoding.h"

namespace rs::legacy_plugin {

namespace {

// ChangeModel の実行時動的解決用に、候補1件分のモデルファイルを ApplyPbr まで
// 適用してロードする（ModelScaleは未焼込み。実行時に選ばれたscaleで
// BakeModelScaleするのはApplyModelVariant側の責務）。relFile が空、または
// 読込に失敗した場合は ok=false を返す（呼び出し側はその候補が選ばれると
// パーツを非表示にする＝RailSim II の「空モデルで消す」挙動。空文字は意図的なので
// 警告を出さず、実ファイルが無いときだけ warn する）。
// xMs/pbrMs は計測専用の累算先(.xロードとApplyPbrの内訳)。partCache は同じパーツの
// 全モデルで共有する ChangeTexture の解決結果(ApplyPbr の partCache)。
ModelVariant LoadModelVariant(const std::string& relFile,
                              const rs::legacy_pi::ModelPart& part,
                              const std::wstring& baseDir, std::vector<std::string>& log,
                              int& missing, double& xMs, double& pbrMs,
                              PartChangeTextureCache& partCache) {
    using ProfClock = std::chrono::steady_clock;
    auto profMs = [](ProfClock::time_point a, ProfClock::time_point b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };
    ModelVariant v;
    if (relFile.empty()) return v;
    std::wstring modelPath = ResolvePath(baseDir, relFile);
    if (!FileExists(modelPath)) {
        log.push_back("[plugin][warn] missing ChangeModel candidate: " + relFile);
        ++missing;
        return v;
    }
    const ProfClock::time_point tX0 = ProfClock::now();
    rs::legacy_x::LoadResult r = rs::legacy_x::LoadXFileAssimp(modelPath);
    xMs += profMs(tX0, ProfClock::now());
    if (!r.ok) {
        log.push_back("[plugin][warn] failed to load ChangeModel candidate: " + relFile);
        return v;
    }
    v.meshUnscaled = std::move(r.mesh);
    v.baseMaterials = v.meshUnscaled.materials; // ApplyPbr前の素の値
    bool anyPbr = false;
    const ProfClock::time_point tPbr0 = ProfClock::now();
    ApplyPbr(v.meshUnscaled, part, baseDir, log, missing, anyPbr, &v.resolvedChangeTextures,
             nullptr, &partCache);
    pbrMs += profMs(tPbr0, ProfClock::now());
    v.meshUnscaled.ComputeAabb();
    v.ok = true;
    return v;
}

// ApplyPbrを通らずに登録するモデル(読込時にどの候補も選ばれないChangeModelパーツの
// プレースホルダ・モデルが無い/読込失敗)の解決済みChangeTexture一覧を、パーツ単位の
// 解決結果から作る。一覧が空のままだとPartMaterialRuntime::Buildが数の不一致から
// 未解決の相対パスをそのまま使い、テクスチャの事前ロードと読込がすべて失敗する。
// まだ解決していない要素(どのモデルでも材質番号が範囲外だった等)は空のパスにする。
std::vector<LoadedModel::ResolvedChangeTexture> ResolvedFromPartCache(
    const rs::legacy_pi::ModelPart& part, const PartChangeTextureCache& partCache) {
    std::vector<LoadedModel::ResolvedChangeTexture> out(part.ChangeTextureCount());
    for (size_t i = 0; i < out.size() && i < partCache.size(); ++i) {
        if (partCache[i]) out[i] = partCache[i]->paths;
    }
    return out;
}

} // namespace

// 各パーツのモデルを読み PBR を適用
void LoadPartModels(LoadedPlugin& lp, const std::wstring& baseDir, LoadProfile& prof) {
    lp.dynamicModels.resize(lp.def.parts.size());
    for (size_t partIdx = 0; partIdx < lp.def.parts.size(); ++partIdx) {
        const rs::legacy_pi::ModelPart& part = lp.def.parts[partIdx];
        // スキップしてよいのは真のダミー(モデルもChangeModel候補も持たない、
        // 位置アンカー専用)だけ。ChangeModel候補は持つが、読込時(全SW=0で評価)
        // にどの候補もactiveにならず modelFile が空のままのパーツは、後でスイッチが
        // 変わった際に PluginSession::UpdateDynamicModels→ApplyModelVariant が
        // 差し込めるよう lp.models へ空メッシュのプレースホルダを残す
        // (ApplyModelVariant は lp.models 中の partDefIndex 一致エントリを探して
        //  上書きする実装のため、エントリ自体が無いと差し替えが常に無視される)。
        if (part.modelFile.empty() && part.changeModels.empty()) continue; // 真のDummy

        // ChangeTexture の解決結果は基本モデル・全候補・下の lm で共通(パーツの定義だけで決まる)。
        PartChangeTextureCache partCache;

        // ChangeModel の実行時動的解決用データを先に構築する(lm の成否とは
        // 独立。base/候補ごとに個別にファイル読込・ApplyPbrを行うため、この
        // 直後の lm 構築で同じファイルを二重ロードすることになるが、候補数は
        // 小さくロード時1回限りのため許容する)。
        if (!part.changeModels.empty()) {
            const Clock::time_point tVariant0 = Clock::now();
            PartDynamicModel& dyn = lp.dynamicModels[partIdx];
            std::shared_ptr<PartModelVariants> variants = std::make_shared<PartModelVariants>();
            variants->baseVariant = LoadModelVariant(part.baseModelFile, part, baseDir, lp.log,
                                                     lp.missingFiles, prof.variantXMs,
                                                     prof.variantPbrMs, partCache);
            variants->changeModelVariants.reserve(part.changeModels.size());
            for (const rs::legacy_pi::ChangeModelRef& cm : part.changeModels)
                variants->changeModelVariants.push_back(
                    LoadModelVariant(cm.modelFile, part, baseDir, lp.log, lp.missingFiles,
                                     prof.variantXMs, prof.variantPbrMs, partCache));
            dyn.variants = std::move(variants);
            rs::legacy_pi::SwitchResolver zeroResolver =
                [](const std::string&) { return int32_t(0); };
            rs::legacy_pi::ChangeModelResolveResult initial =
                rs::legacy_pi::ResolveChangeModel(part, zeroResolver);
            dyn.currentVariantIndex = initial.variantIndex;
            dyn.currentScale = initial.modelScale;
            prof.variantMs += ProfMs(tVariant0, Clock::now());
            prof.RecordVariant(part, dyn);
        }

        LoadedModel lm;
        lm.name = part.label.empty() ? part.kind : part.label;
        lm.kind = part.kind;
        lm.partDefIndex = static_cast<int>(partIdx);

        if (part.modelFile.empty()) {
            // ChangeModel候補は持つが読込時点でどれもactiveでなかったパーツ。
            // 空メッシュのプレースホルダとして登録し、スイッチ変更時の
            // ApplyModelVariant（上のコメント参照）を待つ。
            lm.resolvedChangeTextures = ResolvedFromPartCache(part, partCache);
            lp.models.push_back(std::move(lm));
            continue;
        }

        std::wstring modelPath = ResolvePath(baseDir, part.modelFile);
        lm.modelPath = rs::platform_text::WideToUtf8(modelPath);
        if (!FileExists(modelPath)) {
            lp.log.push_back("[plugin][warn] missing model: " + part.modelFile);
            ++lp.missingFiles;
            lm.resolvedChangeTextures = ResolvedFromPartCache(part, partCache);
            lp.models.push_back(std::move(lm));
            continue;
        }

        const Clock::time_point tAssimp0 = Clock::now();
        rs::legacy_x::LoadResult r = rs::legacy_x::LoadXFileAssimp(modelPath);
        prof.assimpMs += ProfMs(tAssimp0, Clock::now());
        ++prof.assimpCount;
        for (auto& l : r.log)
            if (l.find("警告") != std::string::npos ||
                l.find("[warn") != std::string::npos)
                lp.log.push_back(l);
        if (!r.ok) {
            lp.log.push_back("[plugin][warn] failed to load model: " + part.modelFile);
            lm.resolvedChangeTextures = ResolvedFromPartCache(part, partCache);
            lp.models.push_back(std::move(lm));
            continue;
        }
        lm.mesh = std::move(r.mesh);
        lm.meshOk = true;
        lm.baseMaterials = lm.mesh.materials; // ApplyPbr前の素の .x マテリアル値

        // modelScale をメッシュ頂点座標へ焼き込む(ChangeModel第2引数/ModelScale)。
        // 既定値は1.0fだが、万一0以下が来た場合はメッシュ潰れ防止のため1.0扱いにする。
        BakeModelScale(lm.mesh, EffectiveModelScale(part.modelScale));

        // RailSim II -k-build PBR / ChangeTexture 適用
        bool anyPbr = false;
        const Clock::time_point tPbr0 = Clock::now();
        int applied =
            ApplyPbr(lm.mesh, part, baseDir, lp.log, lp.missingFiles, anyPbr,
                    &lm.resolvedChangeTextures, &lp.def.animations, &partCache);
        prof.pbrMs += ProfMs(tPbr0, Clock::now());
        lm.pbrApplied = anyPbr;
        if (applied > 0) {
            lp.log.push_back("[plugin] " + lm.name + ": applied " +
                             std::to_string(applied) + " ChangeTexture (PBR=" +
                             (anyPbr ? "yes" : "no") + ")");
        }
        lm.mesh.ComputeAabb();
        lp.models.push_back(std::move(lm));
    }
}

} // namespace rs::legacy_plugin
