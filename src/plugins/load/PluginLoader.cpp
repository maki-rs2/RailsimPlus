#include "PluginLoadInternal.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include "ModelMeshHelpers.h"
#include "StretchZyMeshAssembly.h"
#include "PluginPaths.h"
#include "PluginDefinitionLookup.h"
#include "../../core/Epsilon.h"
#include "../schema/parser/Parser.h"
#include "../../render/resource/XLoader.h"
#include "../../platform/text/Encoding.h"

namespace rs::legacy_plugin {

namespace {

// 定義ファイル選択(*2k.txt/*2.txt の優先順位付け)。見つからなければ
// lp.log/lp.noDefinition をセットして false を返す。
bool SelectDefinitionFile(const std::wstring& folder, const LoadOptions& opt, LoadedPlugin& lp,
                          std::wstring& outDefFile) {
    std::wstring defFile = opt.forceDefFile;
    if (defFile.empty()) {
        defFile = PickDefinitionFile(folder, opt.preferKBuild);
        if (defFile.empty()) {
            lp.log.push_back("[plugin][error] no definition (*2.txt) found");
            lp.noDefinition = true;
            return false;
        }
    }
    outDefFile = defFile;
    return true;
}

// パース(定義txtのCP932変換+トークナイズ+AST構築+PluginDefinition組立)は
// 同一defFile(パス+更新時刻)であれば結果が不変なので、
// ParsePluginDefinitionFile 側でキャッシュする。同一プラグインが多数配置されるレイアウトでは、
// これが無いとエンティティ数分だけ同じパースを繰り返すことになる
// (lp.def以降は呼び出し側が読み取り専用でしか参照しない前提)。
bool ParseDefinitionInto(const std::wstring& defFile, LoadedPlugin& lp) {
    rs::legacy_pi::ParsedDefinitionResult parsed = ParsePluginDefinitionFile(defFile);
    for (const auto& d : parsed.diagItems) {
        if (d.severity == rs::legacy_pi::Severity::Error) {
            lp.log.push_back("[pi][err] " + std::to_string(d.line) + ": " + d.message);
        }
        ++lp.warnCount;
    }
    if (!parsed.ok) {
        lp.log.push_back("[plugin][error] empty document");
        return false;
    }

    lp.def = std::move(parsed.def);
    lp.ok = true;
    return true;
}

// DefineAnimation の frame(SlideUV/TiledUV/RotationUV/Simple)が参照する
// テクスチャパスを絶対解決する（UV型アニメ、Simple画像切替）。
// プラグイン跨ぎの相対パス(../../Struct/...)も ChangeTexture と同じ
// ResolvePath で解決する。
// Numbered("filenameFmt", start, end, duration; %d等の書式文字列を連番ごとに
// 展開する必要がある)は解決しない(空文字のまま)。
void ResolveAnimationTexturePaths(LoadedPlugin& lp, const std::wstring& baseDir) {
    lp.resolvedAnimations.resize(lp.def.animations.size());
    for (size_t ai = 0; ai < lp.def.animations.size(); ++ai) {
        const rs::legacy_pi::AnimationDef& animDef = lp.def.animations[ai];
        std::vector<ResolvedAnimationFrame>& out = lp.resolvedAnimations[ai];
        out.resize(animDef.frames.size());
        for (size_t fi = 0; fi < animDef.frames.size(); ++fi) {
            const rs::legacy_pi::AnimationFrame& f = animDef.frames[fi];
            if (f.kind == rs::legacy_pi::AnimationFrame::Kind::Numbered) continue;
            if (f.texturePath.empty()) continue;
            std::wstring wp = ResolvePath(baseDir, f.texturePath);
            if (FileExists(wp)) {
                out[fi].base = rs::platform_text::WideToUtf8(wp);
            } else {
                lp.log.push_back("[plugin][warn] missing animation tex: " + f.texturePath);
                ++lp.missingFiles;
            }
        }
    }
}

// Rail/Tie: 断面プロファイルをZ方向へ押し出して表示用モデルを1つ追加する。
// Pier は断面がX-Z水平面でY方向(高さ)へ押し出すため対象外(BuildPierShaftMesh側で処理)。
void AppendRailProfileModel(LoadedPlugin& lp, const std::wstring& baseDir, const LoadOptions& opt) {
    if (lp.def.header.type != rs::legacy_pi::PluginType::Pier && !lp.def.railProfiles.empty()) {
        const float kRailDisplayLength = 25.0f; // 表示用の直線区間長[m]
        rs::render::Mesh railMesh = BuildRailMesh(lp.def, baseDir,
                                                  kRailDisplayLength, opt.curveRadiusM,
                                                  lp.log, lp.missingFiles);
        if (!railMesh.Empty()) {
            LoadedModel lm;
            lm.name = "Rail profile (extruded)";
            lm.kind = "Rail";
            lm.mesh = std::move(railMesh);
            lm.meshOk = true;
            lp.log.push_back("[plugin] rail extruded: " +
                             std::to_string(lm.mesh.submeshes.size()) +
                             " profiles, " +
                             std::to_string(lm.mesh.indices.size() / 3) + " tris");
            lp.models.push_back(std::move(lm));
        }
    }
}

// Tie 等: Interval 反復モデルを読み、Z方向に複製して表示用モデルを追加。
void AppendIntervalModels(LoadedPlugin& lp, const std::wstring& baseDir, const LoadOptions& opt) {
    for (const auto& iv : lp.def.intervals) {
        std::wstring modelPath = ResolvePath(baseDir, iv.modelFile);
        if (!FileExists(modelPath)) {
            lp.log.push_back("[plugin][warn] missing interval model: " + iv.modelFile);
            ++lp.missingFiles;
            continue;
        }
        rs::legacy_x::LoadResult r = rs::legacy_x::LoadXFileAssimp(modelPath);
        if (!r.ok) {
            lp.log.push_back("[plugin][warn] failed to load interval model: " + iv.modelFile);
            continue;
        }
        const float kIntervalDisplayLength = 25.0f; // 押し出しレールと同じ表示長[m]
        rs::render::Mesh intervalSourceMesh = r.mesh; // UIリビルド用に反復前を保持
        rs::render::Mesh im = BuildIntervalMesh(r.mesh, iv.modelScale, iv.interval,
                                                kIntervalDisplayLength,
                                                opt.curveRadiusM, 0.0f, lp.def.gauge,
                                                iv.ignoreCant);
        if (!im.Empty()) {
            LoadedModel lm;
            // interval が極小(≒0)だと除算が発散するので count は1に丸める
            // （BuildIntervalMesh 側も、間隔が最小値以下なら1個扱い）。
            int repeatCount = (iv.interval > rs::core::kMinRepeatIntervalM)
                                  ? static_cast<int>(kIntervalDisplayLength / iv.interval) + 1
                                  : 1;
            lm.name = "Interval models (x" +
                      std::to_string(im.submeshes.size() ? repeatCount : 0) + ")";
            lm.kind = "Interval";
            lm.mesh = std::move(im);
            lm.meshOk = true;
            lm.rebuildableInterval = true;
            lm.intervalSource = std::move(intervalSourceMesh);
            lm.intervalScale = iv.modelScale;
            lm.intervalStep = iv.interval;
            lm.intervalIgnoreCant = iv.ignoreCant;
            lp.log.push_back("[plugin] interval extruded: " + iv.modelFile + ", " +
                             std::to_string(lm.mesh.indices.size() / 3) + " tris");
            lp.models.push_back(std::move(lm));
        }
    }
}

// Pier: Joint/Head/Base モデル + Profile押し出しシャフトを1メッシュに合成。
void AppendPierAssembly(LoadedPlugin& lp, const std::wstring& baseDir, const LoadOptions& opt) {
    if (lp.def.header.type == rs::legacy_pi::PluginType::Pier &&
        (lp.def.pier.hasBase || lp.def.pier.hasHead || lp.def.pier.hasJoint ||
         !lp.def.railProfiles.empty() || !lp.def.intervals.empty())) {
        // 接合点(Joint)の地面からの高さ。RailSim IIのCPier::Initと同じ積み上げ規則で
        // Head=Joint+JointToHead、シャフト上端=Head+HeadToPier、
        // シャフト下端=地面+BaseToPier、Base=地面 とする。
        const float jointHeightM =
            (opt.pierHeightOverrideM >= 0.0f) ? opt.pierHeightOverrideM : 6.0f;
        const float headY = jointHeightM + lp.def.pier.joint.localOffsetY;
        const float shaftTopY = headY + lp.def.pier.head.localOffsetY;
        const float shaftBottomY = lp.def.pier.base.localOffsetY;
        rs::render::Mesh assembly;

        if (!lp.def.railProfiles.empty() && shaftTopY > shaftBottomY) {
            rs::render::Mesh shaft = BuildPierShaftMesh(lp.def, baseDir,
                                                         shaftTopY - shaftBottomY, lp.log,
                                                         lp.missingFiles);
            AppendOffsetMesh(assembly, shaft, 1.0f, shaftBottomY);
        }

        const float shaftLengthM = shaftTopY - shaftBottomY;
        if (shaftLengthM > 0.0f) {
            const std::vector<LoadedIntervalModel> intervalModels = LoadIntervalModels(lp.def, baseDir);
            for (const LoadedIntervalModel& intervalModel : intervalModels) {
                if (intervalModel.interval == nullptr || !intervalModel.interval->wireTracking.empty()) continue;
                const rs::render::Mesh intervalMesh = BuildPierIntervalMesh(
                    intervalModel.mesh, intervalModel.interval->modelScale, lp.def.pier,
                    *intervalModel.interval, shaftLengthM);
                AppendOffsetMesh(assembly, intervalMesh, 1.0f, shaftBottomY);
            }
        }

        auto loadPart = [&](const rs::legacy_pi::PierPartRef& ref, float offsetY,
                            const char* what) {
            if (ref.modelFile.empty()) return;
            std::wstring modelPath = ResolvePath(baseDir, ref.modelFile);
            if (!FileExists(modelPath)) {
                lp.log.push_back(std::string("[plugin][warn] missing pier ") + what +
                                 ": " + ref.modelFile);
                ++lp.missingFiles;
                return;
            }
            rs::legacy_x::LoadResult r = rs::legacy_x::LoadXFileAssimp(modelPath);
            if (!r.ok) {
                lp.log.push_back(std::string("[plugin][warn] failed to load pier ") +
                                 what + ": " + ref.modelFile);
                return;
            }
            AppendOffsetMesh(assembly, r.mesh, ref.modelScale, offsetY);
        };

        if (lp.def.pier.hasBase) loadPart(lp.def.pier.base, 0.0f, "base");
        if (lp.def.pier.hasHead) loadPart(lp.def.pier.head, headY, "head");
        if (lp.def.pier.hasJoint) loadPart(lp.def.pier.joint, jointHeightM, "joint");

        if (!assembly.Empty()) {
            lp.log.push_back("[plugin] pier assembly: " +
                             std::to_string(assembly.submeshes.size()) +
                             " submeshes, " +
                             std::to_string(assembly.indices.size() / 3) + " tris");

            LoadedModel lm;
            lm.name = "Pier assembly";
            lm.kind = "Pier";
            lm.mesh = assembly;
            lm.meshOk = true;
            lp.models.push_back(std::move(lm));

            // RebuildAssembly() は def.parts に対応する partDefIndex>=0 のモデル
            // だけを対象にするため、def.parts を持たない純粋な Pier プラグインでは
            // hasAssembly=false のままになり、SceneRenderer::Sync() の hasAssembly
            // ゲートで読込失敗扱いされてしまう。Pier自身の合成メッシュを直接
            // assemblyMesh として採用し、SceneEntity(離散配置)経路で表示できる
            // ようにする(RebuildAssembly側で既に何か構築済みなら上書きしない)。
            // 配置は AdoptMeshAsAssembly が partDefIndex=-1 のダミー1件を積む
            // (空だとMeshPipeline::UpdatePartWorldsが恒等行列1個へフォールバックし、
            // entityWorldが一切反映されず全インスタンスがワールド原点に描画される)。
            if (!lp.hasAssembly) AdoptMeshAsAssembly(lp, std::move(assembly));
        }
    }
}

} // namespace


float AnimationFrameRateFps(const LoadedPlugin& lp) {
    const std::wstring& path = lp.defPath;
    const bool kBuild =
        path.size() >= 6 && CompareNoCase(path.c_str() + path.size() - 6, L"2k.txt") == 0;
    return kBuild ? kAnimationFrameRateKBuildFps : kAnimationFrameRateStandardFps;
}

LoadedPlugin LoadPluginFolder(const std::wstring& folder, const LoadOptions& opt) {
    const Clock::time_point tTotal0 = Clock::now();
    LoadProfile prof;

    LoadedPlugin lp;
    lp.folder = folder;

    std::wstring defFile;
    if (!SelectDefinitionFile(folder, opt, lp, defFile)) return lp;
    lp.defPath = defFile;

    const Clock::time_point tParse0 = Clock::now();
    const bool parsedOk = ParseDefinitionInto(defFile, lp);
    prof.parseMs = ProfMs(tParse0, Clock::now());
    if (!parsedOk) return lp;

    if (!opt.loadMeshes) return lp;

    const std::wstring baseDir = DirOf(defFile);

    const Clock::time_point tAnim0 = Clock::now();
    ResolveAnimationTexturePaths(lp, baseDir);
    prof.animPathMs = ProfMs(tAnim0, Clock::now());

    LoadPartModels(lp, baseDir, prof);

    const Clock::time_point tAppend0 = Clock::now();
    AppendRailProfileModel(lp, baseDir, opt);
    AppendIntervalModels(lp, baseDir, opt);
    prof.appendMs = ProfMs(tAppend0, Clock::now());

    // Train 等: 全パーツ同時表示用の合成メッシュを構築する。
    const Clock::time_point tAssembly0 = Clock::now();
    lp.stretchZyParts = BuildStretchZyAssemblyMesh(lp.def, baseDir, lp.log, lp.missingFiles);
    RebuildAssembly(lp);
    prof.assemblyMs = ProfMs(tAssembly0, Clock::now());

    const Clock::time_point tPier0 = Clock::now();
    AppendPierAssembly(lp, baseDir, opt);
    prof.appendMs += ProfMs(tPier0, Clock::now());
    lp.hasAssemblyAtLoad = lp.hasAssembly;

    LogLoadProfile(lp, prof, ProfMs(tTotal0, Clock::now()));
    return lp;
}

} // namespace rs::legacy_plugin
