//
// SchemaPlacement.cpp - PI パーツ組立配置/ジョイント解決
//
// collectParts（SchemaWalk.cpp）が抽出した JointZY/Coord/Joint3D 情報をもとに、
// 各パーツのワールド配置（offsetZ/offsetY/turn および Joint3D の worldMat）を
// 反復パスで解決する。行列演算は row-major / row-vector 規約。
//
#include "SchemaInternal.h"
#include "ZyLinkageSolve.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "../../core/Math.h"

namespace rs::legacy_pi {

namespace {

// Mat4 の平行移動成分に (x,y,z) をセットする（row-major, row-vector: v'=v*M）。
void SetTranslation(rs::core::Mat4& m, float x, float y, float z) {
    m.m[12] = x; m.m[13] = y; m.m[14] = z;
}

// row-vector 規約でベクトル v にワールド行列 m（回転+平行移動）の回転部だけを適用。
void RotateVec(const rs::core::Mat4& m, const float v[3], float out[3]) {
    out[0] = v[0] * m.m[0] + v[1] * m.m[4] + v[2] * m.m[8];
    out[1] = v[0] * m.m[1] + v[1] * m.m[5] + v[2] * m.m[9];
    out[2] = v[0] * m.m[2] + v[1] * m.m[6] + v[2] * m.m[10];
}

// row-vector 規約でベクトル v にワールド行列 m（平行移動含む）を適用（点変換）。
void TransformPoint(const rs::core::Mat4& m, const float v[3], float out[3]) {
    RotateVec(m, v, out);
    out[0] += m.m[12]; out[1] += m.m[13]; out[2] += m.m[14];
}

// Joint3D の基準フレーム行列を名前解決する。同一プラグイン内の他パーツ名、または
// システムオブジェクト名(_LOCAL/_WORLD は恒等、_CAMERA/_LIGHT は警告+恒等)。
// 見つからない/未解決の場合は resolvedOut=false を返す（呼び出し側は据え置き）。
bool FindFrameMatrix(const std::string& name, const std::vector<ModelPart>& parts,
                     std::vector<std::string>& warnings, rs::core::Mat4& outMat) {
    if (name == "_LOCAL" || name == "_WORLD") {
        outMat = rs::core::Mat4::Identity();
        return true;
    }
    if (name == "_CAMERA" || name == "_LIGHT") {
        warnings.push_back("[plugin][warn] Joint3D references unsupported system "
                            "object '" + name + "' (treated as identity)");
        outMat = rs::core::Mat4::Identity();
        return true;
    }
    for (const auto& q : parts) {
        if (q.label != name) continue;
        if (!q.placeResolved3d) return false;
        std::memcpy(outMat.m, q.worldMat, sizeof(outMat.m));
        return true;
    }
    // 未知の名前: 警告して恒等扱い（呼び出し側で1回だけ出す想定は難しいため簡潔に）。
    warnings.push_back("[plugin][warn] Joint3D unknown target '" + name + "' (identity)");
    outMat = rs::core::Mat4::Identity();
    return true;
}

// Joint3D 単体の配置（基準フレーム行列を前提に）を計算する。
//   dir=normalize(AttachDir), right=normalize(cross(up,dir)), up'=cross(dir,right)
//   という正規直交基底を基準フレームのローカル軸として組み、
//   R_part = R_basis(dir,right,up') * R_target
//   T_part = TransformPoint(target, AttachCoord) − R_part × LocalCoord
// 実行時(DynamicJointPlacement.cpp)からも同じ幾何計算を使うため、公開関数
// rs::legacy_pi::ComposeJoint3DWorld として PluginSchema.h からも呼べる
// （このファイル内では Mat4 引数のラッパとして使う）。
rs::core::Mat4 ComposeJoint3D(const Joint3DRef& j, const rs::core::Mat4& dirFrame,
                              const rs::core::Mat4& upFrame, const rs::core::Mat4& targetFrame) {
    // AttachDir/AttachUp は各 Link 先フレームのローカル軸として解釈し、
    // ワールド空間の方向ベクトルへ変換する。
    float dirWorld[3], upWorld[3];
    RotateVec(dirFrame, j.dir, dirWorld);
    RotateVec(upFrame, j.up, upWorld);
    rs::core::Normalize3(dirWorld);
    rs::core::Normalize3(upWorld);

    // 正規直交基底: right = normalize(cross(up, dir)), up' = cross(dir, right)
    float right[3];
    rs::core::Cross3(upWorld, dirWorld, right);
    rs::core::Normalize3(right);
    float upOrtho[3];
    rs::core::Cross3(dirWorld, right, upOrtho);
    rs::core::Normalize3(upOrtho);

    // 自フレームの (+X,+Y,+Z) がそれぞれ (right, upOrtho, dirWorld) に一致する回転。
    // row-major row-vector: 行ベクトルが変換後の基底軸。
    rs::core::Mat4 rPart = rs::core::Mat4::Identity();
    rPart.m[0] = right[0];   rPart.m[1] = right[1];   rPart.m[2] = right[2];   rPart.m[3] = 0;
    rPart.m[4] = upOrtho[0]; rPart.m[5] = upOrtho[1]; rPart.m[6] = upOrtho[2]; rPart.m[7] = 0;
    rPart.m[8] = dirWorld[0];rPart.m[9] = dirWorld[1];rPart.m[10]= dirWorld[2];rPart.m[11]= 0;
    rPart.m[12]=0; rPart.m[13]=0; rPart.m[14]=0; rPart.m[15]=1;

    float attachWorld[3];
    TransformPoint(targetFrame, j.attach, attachWorld);
    float localWorld[3];
    RotateVec(rPart, j.local, localWorld);

    rs::core::Mat4 result = rPart;
    SetTranslation(result, attachWorld[0] - localWorld[0],
                   attachWorld[1] - localWorld[1],
                   attachWorld[2] - localWorld[2]);
    return result;
}

} // namespace

namespace schema_detail {

// PrimaryAssembly の組立座標を反復解決する。
//   Axle（Coord 保持）      → その場で確定
//   JointZY を持つ Body     → 子位置 + AttachCoord − R(LocalCoord) の平均
//                             （Turn=yes は LocalCoord の z を符号反転）
//   Coord も Joint も無い   → (0,0) で確定（車体補間 Body 等）
// 子が未解決の間は据え置き、パーツ数分だけパスを繰り返す。
void ResolvePlacements(std::vector<ModelPart>& parts) {
    for (auto& p : parts)
        if (p.hasCoord) {
            p.offsetZ = p.coordZ;
            p.offsetY = p.coordY;
            p.placeResolved = true;
        }

    for (size_t pass = 0; pass < parts.size() + 1; ++pass) {
        bool progressed = false;
        for (auto& p : parts) {
            if (p.placeResolved) continue;
            if (p.joints.empty()) {
                p.offsetZ = p.offsetY = 0.0f; // 配置指定の無いパーツは原点
                p.placeResolved = true;
                progressed = true;
                continue;
            }
            // すべての接続先が解決済みなら平均で確定
            float sumZ = 0, sumY = 0;
            int   n = 0;
            bool  allReady = true;
            for (const auto& j : p.joints) {
                const ModelPart* child = nullptr;
                for (const auto& q : parts)
                    if (q.label == j.childLabel) { child = &q; break; }
                if (!child || !child->placeResolved) { allReady = false; break; }
                float localZeff = p.turn ? -j.localZ : j.localZ;
                sumZ += child->offsetZ + j.attachZ - localZeff;
                sumY += child->offsetY + j.attachY - j.localY;
                ++n;
            }
            if (allReady && n > 0) {
                p.offsetZ = sumZ / n;
                p.offsetY = sumY / n;
                p.placeResolved = true;
                progressed = true;
            }
        }
        if (!progressed) break;
    }

    // 循環参照等で解決できなかったパーツは原点扱いにして表示は継続する。
    for (auto& p : parts)
        if (!p.placeResolved) p.placeResolved = true;
}

// Joint3D による配置を反復パスで解決する。resolvePlacements（JointZY/Coord系）とは
// 独立に動作し、Joint3D を持つパーツがあればそちらを優先してワールド行列を確定する。
// 名前参照の解決順に依存するため、他パーツの解決を待つ必要がある場合は据え置き、
// パーツ数+1 回まで反復する。解決不能なら警告して恒等行列で確定する。
void ResolveJoint3D(std::vector<ModelPart>& parts, std::vector<std::string>& warnings) {
    // Joint3D の target/DirLink/UpLink が存在するパーツ名か(_LOCAL 等のシステム名も
    // 可)を先に判定し、未知の名前を指している場合は hasUnresolvedJointRef を立てる
    // （このパーツの StaticRotation 適用可否を DynamicJointPlacement.cpp 側で
    // 判断するために使う。findFrameMatrix 参照）。
    auto isKnownJointName = [&](const std::string& name) {
        if (IsSystemJointTargetName(name)) return true;
        for (const auto& q : parts)
            if (q.label == name) return true;
        return false;
    };
    for (auto& p : parts) {
        if (p.isZyPart) {
            // ZY系: 相手や全関節の取り付け先が未知なら解決不能(DynamicJointPlacement.cpp が
            // StaticRotation 適用可否の判断に使う)。
            if (HasUnresolvedZyRef(parts, p)) p.hasUnresolvedJointRef = true;
            continue;
        }
        if (p.joints3d.empty()) continue;
        const Joint3DRef& j = p.joints3d.back();
        if (!isKnownJointName(j.target) || !isKnownJointName(j.dirLink) ||
            !isKnownJointName(j.upLink))
            p.hasUnresolvedJointRef = true;
    }

    // Joint3D を持たないパーツは、resolvePlacements（JointZY/Coord系、本関数より
    // 先に実行済み）で確定した offsetZ/offsetY/turn をそのままワールド行列化して
    // 確定扱いにする（Joint3D を持つ他パーツが基準名として参照できるように、
    // Train の MainBody 等 JointZY 由来パーツも正しい配置で参照可能にする）。
    // ZY系の可動部は別の解決式(下のループ)を使うためここでは除外する。
    for (auto& p : parts) {
        if (p.joints3d.empty() && !p.isZyPart) {
            rs::core::Mat4 rot = p.turn
                ? rs::core::Mat4{{-1,0,0,0, 0,1,0,0, 0,0,-1,0, 0,0,0,1}}
                : rs::core::Mat4::Identity();
            rs::core::Mat4 world = rot;
            world.m[12] = 0.0f;
            world.m[13] = p.offsetY;
            world.m[14] = p.offsetZ;
            std::memcpy(p.worldMat, world.m, sizeof(p.worldMat));
            p.placeResolved3d = true;
        }
    }

    for (size_t pass = 0; pass < parts.size() + 1; ++pass) {
        bool progressed = false;
        for (auto& p : parts) {
            if (p.placeResolved3d) continue;
            if (p.isZyPart) {
                // ZY系は組(自分と相手)をまとめて1回で解く(SolveZyGroupForPart)。
                // 相手が見つからない、または取り付け先が未解決ならこのパスは据え置く。
                ZyGroupResult group;
                const ZyFrameLookup lookup = [&](const std::string& name, rs::core::Mat4& outFrame) {
                    return FindFrameMatrix(name, parts, warnings, outFrame);
                };
                if (!SolveZyGroupForPart(parts, static_cast<size_t>(&p - parts.data()), lookup, group))
                    continue;
                for (int i = 0; i < group.count; ++i) {
                    ModelPart& member = parts[group.indices[i]];
                    std::memcpy(member.worldMat, group.worlds[i], sizeof(member.worldMat));
                    member.placeResolved3d = true;
                }
                progressed = true;
                continue;
            }
            if (p.joints3d.empty()) continue; // 上で解決済みのはず
            // 複数 Joint3D がある場合は最後の1つを採用する（先頭一致優先の
            // 明確な仕様記載が無いため、宣言順で最後が有効という素直な解釈）。
            const Joint3DRef& j = p.joints3d.back();
            rs::core::Mat4 targetFrame, dirFrame, upFrame;
            bool ok = FindFrameMatrix(j.target, parts, warnings, targetFrame);
            ok = ok && FindFrameMatrix(j.dirLink, parts, warnings, dirFrame);
            ok = ok && FindFrameMatrix(j.upLink, parts, warnings, upFrame);
            if (!ok) continue; // 依存先が未解決。次のパスへ持ち越し
            rs::core::Mat4 world = ComposeJoint3D(j, dirFrame, upFrame, targetFrame);
            std::memcpy(p.worldMat, world.m, sizeof(p.worldMat));
            p.placeResolved3d = true;
            progressed = true;
        }
        if (!progressed) break;
    }

    // 循環参照等で解決できなかったパーツは警告して恒等行列のまま確定する。
    for (auto& p : parts) {
        if (!p.placeResolved3d) {
            warnings.push_back("[plugin][warn] Joint3D unresolved for part '" +
                               p.label + "' (circular reference?), using identity");
            p.placeResolved3d = true;
        }
    }
}

} // namespace schema_detail

bool IsSystemJointTargetName(const std::string& name) {
    return name == "_LOCAL" || name == "_WORLD" || name == "_CAMERA" || name == "_LIGHT";
}

void ComposeJoint3DWorld(const Joint3DRef& j, const float dirFrame[16],
                         const float upFrame[16], const float targetFrame[16],
                         float outWorld[16]) {
    rs::core::Mat4 dir{}, up{}, target{};
    std::memcpy(dir.m, dirFrame, sizeof(dir.m));
    std::memcpy(up.m, upFrame, sizeof(up.m));
    std::memcpy(target.m, targetFrame, sizeof(target.m));
    rs::core::Mat4 result = ComposeJoint3D(j, dir, up, target);
    std::memcpy(outWorld, result.m, sizeof(result.m));
}

} // namespace rs::legacy_pi
