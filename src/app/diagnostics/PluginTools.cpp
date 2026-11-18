#include "PluginTools.h"

#include "../../core/Epsilon.h"

#include "../../platform/EnvironmentVariable.h"
#include <windows.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>
#include <utility>

#include "../../render/resource/TextureLoader.h"
#include "../../plugins/schema/parser/Parser.h"
#include "../../plugins/schema/PluginSchema.h"
#include "../../plugins/load/runtime/MoverRuntime.h"
#include "../../plugins/load/PluginLoader.h"
#include "../../plugins/load/TrainAssembly.h"
#include "../../render/resource/XLoader.h"
#include "../../platform/text/Encoding.h"
#include "../../plugins/load/runtime/DynamicJointPlacement.h"

namespace rs::app {

// ヘッドレス: .x を読み統計を出力
int RunTestX(const std::string& utf8Path) {
    std::wstring w = rs::platform_text::Utf8ToWide(utf8Path);
    rs::legacy_x::LoadResult r = rs::legacy_x::LoadXFileAssimp(w);
    for (const auto& l : r.log) std::printf("%s\n", l.c_str());
    if (!r.ok) {
        std::printf("[testx] FAILED to load\n");
        return 1;
    }
    std::printf("[testx] OK verts=%zu indices=%zu submeshes=%zu materials=%zu\n",
                r.mesh.vertices.size(), r.mesh.indices.size(),
                r.mesh.submeshes.size(), r.mesh.materials.size());
    for (size_t i = 0; i < r.mesh.materials.size(); ++i) {
        const rs::render::Material& m = r.mesh.materials[i];
        std::printf("  mat[%zu] diffuse=(%.2f,%.2f,%.2f) tex='%s'\n", i, m.diffuse[0],
                    m.diffuse[1], m.diffuse[2], m.name.c_str());
    }
    return 0;
}

// ヘッドレス: txt を AST 化
int RunTestPi(const std::string& utf8Path) {
    std::wstring w = rs::platform_text::Utf8ToWide(utf8Path);
    rs::legacy_pi::Diagnostics diag;
    rs::legacy_pi::PiDocument doc = rs::legacy_pi::ParseFile(w, diag);
    std::printf("[testpi] top-level blocks=%zu, diagnostics=%zu (errors=%d)\n",
                doc.nodes.size(), diag.Items().size(), diag.ErrorCount());
    for (const auto& n : doc.nodes) {
        std::printf("  block '%s'%s%s (children=%zu, assigns=%zu)\n", n.name.c_str(),
                    n.label.empty() ? "" : " label=", n.label.c_str(),
                    n.blocks.size(), n.assignments.size());
    }
    int shown = 0;
    for (const auto& it : diag.Items()) {
        if (shown++ >= 20) break;
        std::printf("  [%s] %d:%d %s\n",
                    it.severity == rs::legacy_pi::Severity::Error ? "ERR" : "warn", it.line,
                    it.column, it.message.c_str());
    }
    return diag.HasErrors() ? 2 : 0;
}

// 種別フォルダ配下の各プラグインサブフォルダを列挙
std::vector<std::wstring> ListSubdirs(const std::wstring& dir) {
    std::vector<std::wstring> out;
    std::wstring pattern = dir + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        out.push_back(dir + L"\\" + n);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

// ヘッドレス: 1プラグインフォルダを読み部品/PBR情報をダンプ
int RunTestPlugin(const std::string& folderUtf8, const std::string& defFile) {
    std::wstring folder = rs::platform_text::Utf8ToWide(folderUtf8);
    rs::legacy_plugin::LoadOptions opt;
    if (!defFile.empty()) opt.forceDefFile = rs::platform_text::Utf8ToWide(defFile);
    rs::legacy_plugin::LoadedPlugin lp =
        rs::legacy_plugin::LoadPluginFolder(folder, opt);
    std::printf("[plugin] def=%ls ok=%d\n", lp.defPath.c_str(), lp.ok ? 1 : 0);
    std::printf("[plugin] type=%s name='%s' author='%s'\n",
                rs::legacy_pi::PluginTypeName(lp.def.header.type),
                lp.def.header.name.c_str(), lp.def.header.author.c_str());
    std::printf("[plugin] switches=%zu parts=%zu models=%zu missingFiles=%d\n",
                lp.def.switches.size(), lp.def.parts.size(), lp.models.size(),
                lp.missingFiles);
    {
        size_t customizerCount = 0, joint3dCount = 0, joint3dResolved = 0;
        for (const auto& p : lp.def.parts) {
            customizerCount += p.customizers.size();
            joint3dCount += p.joints3d.size();
            if (!p.joints3d.empty() && p.placeResolved3d) ++joint3dResolved;
        }
        std::printf("[plugin] customizers=%zu joint3d=%zu joint3dResolvedParts=%zu "
                    "joint3dWarnings=%zu hasAssembly=%d\n",
                    customizerCount, joint3dCount, joint3dResolved,
                    lp.def.joint3dWarnings.size(), lp.hasAssembly ? 1 : 0);
        for (const auto& w : lp.def.joint3dWarnings) std::printf("  %s\n", w.c_str());
    }
    // Train型: 物理解決用のアンカー解決(BuildTrainVehicleTemplate)を独立に走らせ、
    // どのパーツにも辿り着けず未解決(anchorKind==None)のまま残るパーツを列挙する。
    // 未解決パーツは描画時にMat4::identity()(ワールド原点)へフォールバックする
    // (rs::legacy_plugin::SolvePartWorlds)。joint3dWarnings(placeResolved3d基準)
    // はこの解決経路をカバーしないため、別途ここで検査する。
    if (lp.def.header.type == rs::legacy_pi::PluginType::Train) {
        const rs::legacy_plugin::TrainVehicleTemplate tmpl =
            rs::legacy_plugin::BuildTrainVehicleTemplate(lp.def);
        std::printf("[plugin] trainAssembly axles=%zu bodies=%zu\n",
                    tmpl.vehicle.axles.size(), tmpl.vehicle.bodies.size());
        int unresolved = 0;
        for (size_t i = 0; i < tmpl.partRender.size(); ++i) {
            using AnchorKind = rs::legacy_plugin::TrainVehicleTemplate::PartRenderInfo::AnchorKind;
            if (tmpl.partRender[i].anchorKind != AnchorKind::None) continue;
            ++unresolved;
            std::printf("  [UNRESOLVED-ANCHOR] part[%zu] '%s' (%s) -> renders at world origin\n",
                        i, lp.def.parts[i].label.c_str(), lp.def.parts[i].kind.c_str());
        }
        std::printf("[plugin] trainAssemblyUnresolvedParts=%d\n", unresolved);
    }
    for (const auto& m : lp.models) {
        std::printf("  part '%s' (%s) meshOk=%d PBR=%d verts=%zu mats=%zu\n",
                    m.name.c_str(), m.kind.c_str(), m.meshOk ? 1 : 0,
                    m.pbrApplied ? 1 : 0, m.mesh.vertices.size(),
                    m.mesh.materials.size());
        for (size_t mi = 0; mi < m.mesh.materials.size(); ++mi) {
            const rs::render::Material& mat = m.mesh.materials[mi];
            if (!mat.normalPath.empty() || !mat.ormPath.empty() || mat.useBumpMap) {
                std::printf("    mat[%zu] base=%d normal=%d orm=%d bump=%d\n",
                            mi, mat.texturePath.empty() ? 0 : 1,
                            mat.normalPath.empty() ? 0 : 1,
                            mat.ormPath.empty() ? 0 : 1,
                            mat.useBumpMap ? 1 : 0);
            }
        }
    }
    for (const auto& l : lp.log) std::printf("  %s\n", l.c_str());

    // Joint3D階層の動的解決(ResolveDynamicPartWorlds)の疎通確認: 全スイッチ0時
    // と全スイッチ1時でパーツごとのワールド行列(平行移動成分)を比較し、
    // アタッチ先パーツのmoverが子パーツへ伝播しているかを見る
    // (ずっと残すヘッドレス回帰チェック)。
    {
        std::vector<rs::legacy_plugin::PartMoverRuntime> moverA(lp.def.parts.size());
        std::vector<rs::legacy_plugin::PartMoverRuntime> moverB(lp.def.parts.size());
        for (size_t i = 0; i < lp.def.parts.size(); ++i) {
            moverA[i].Build(lp.def.parts[i]);
            moverB[i].Build(lp.def.parts[i]);
        }
        rs::legacy_pi::SwitchResolver zero = [](const std::string&) { return int32_t(0); };
        rs::legacy_pi::SwitchResolver one = [](const std::string&) { return int32_t(1); };
        std::vector<rs::core::Mat4> worldsA =
            ResolveDynamicPartWorlds(lp.def, moverA, zero, /*switchGeneration=*/1, 0.0f, 0.0f, 0.0);
        std::vector<rs::core::Mat4> worldsB =
            ResolveDynamicPartWorlds(lp.def, moverB, one, /*switchGeneration=*/1, 0.0f, 0.0f, 0.0);
        int movedCount = 0;
        for (size_t i = 0; i < lp.def.parts.size(); ++i) {
            float dx = worldsA[i].m[12] - worldsB[i].m[12];
            float dy = worldsA[i].m[13] - worldsB[i].m[13];
            float dz = worldsA[i].m[14] - worldsB[i].m[14];
            if (std::fabs(dx) > rs::core::kPlacementLateralEpsM || std::fabs(dy) > rs::core::kPlacementLateralEpsM || std::fabs(dz) > rs::core::kPlacementLateralEpsM) {
                ++movedCount;
                std::printf("  [dynjoint] part '%s' moved by switch 0->1: d=(%.3f,%.3f,%.3f)\n",
                            lp.def.parts[i].label.c_str(), dx, dy, dz);
            }
        }
        std::printf("[plugin] dynamic joint check: %d/%zu parts respond to switch 0->1\n",
                    movedCount, lp.def.parts.size());
    }

    // [profile] テクスチャデコード計測。GPUアップロード時に
    // mesh_upload が material ごとに LoadImageRGBA を呼ぶのと同じ作業量を、
    // デバイス無しで再現する。重複(同一パスの再デコード)がキャッシュ化で
    // 消せる分なので、総数/ユニーク数の両方を計測する。
    // 環境変数 RS_PROFILE_LOAD 指定時のみ実行(既定の診断出力を汚さないため)。
    if (rs::platform::ReadEnvironmentVariable("RS_PROFILE_LOAD") != nullptr) {
        for (const auto& l : lp.log)
            if (l.rfind("[profile]", 0) == 0) std::printf("%s\n", l.c_str());

        std::vector<std::string> allPaths; // アップロード順(重複あり)
        for (const auto& m : lp.models) {
            for (const auto& mat : m.mesh.materials) {
                if (!mat.texturePath.empty()) allPaths.push_back(mat.texturePath);
                if (!mat.normalPath.empty()) allPaths.push_back(mat.normalPath);
                if (!mat.ormPath.empty()) allPaths.push_back(mat.ormPath);
            }
        }
        using Clock = std::chrono::steady_clock;
        auto decodeAll = [](const std::vector<std::string>& paths) {
            Clock::time_point t0 = Clock::now();
            size_t okBytes = 0;
            for (const auto& p : paths) {
                rs::render::ImageData img = rs::render::LoadImageRGBA(p);
                if (img.ok) okBytes += img.pixels.size();
            }
            double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
            return std::pair<double, size_t>(ms, okBytes);
        };
        std::vector<std::string> uniquePaths;
        {
            std::unordered_set<std::string> seen;
            for (const auto& p : allPaths)
                if (seen.insert(p).second) uniquePaths.push_back(p);
        }
        std::pair<double, size_t> allR = decodeAll(allPaths);
        std::pair<double, size_t> uniR = decodeAll(uniquePaths);
        std::printf("[profile] texdecode all=%zu paths %.1fms (%.1fMB) | "
                    "unique=%zu paths %.1fms | dup-savings=%.1fms\n",
                    allPaths.size(), allR.first, allR.second / (1024.0 * 1024.0),
                    uniquePaths.size(), uniR.first, allR.first - uniR.first);
    }
    return lp.ok ? 0 : 1;
}

} // namespace rs::app
