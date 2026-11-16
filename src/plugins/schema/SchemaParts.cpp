// SchemaParts.cpp - PIパーツ階層の収集（SchemaWalk.cppから分離、walkPartでモデル参照/カスタマイザを収集しつつパーツ木を構築する）
#include "SchemaInternal.h"

#include <unordered_set>

#include "CustomizerParsers.h"

namespace rs::legacy_pi {

namespace {

bool IsPartBlock(const std::string& name) {
    return name == "Axle" || name == "Body" || name == "Object3D" ||
           name == "Object" || name == "Model";
}

} // namespace

namespace schema_detail {

// トップレベル DefineAnimation "name"{...} を AnimationDef にパースする。
AnimationDef ParseDefineAnimation(const PiBlock& node) {
    AnimationDef def;
    def.name = node.label;
    for (const auto& a : node.assignments)
        ParseAnimationFrame(a.key, a, def.frames);
    return def;
}

// ObjectZY / TriangleZY / PistonZY / CrankZY の1ブロックを ZY系のパーツへ分解して out へ追加する。
//   ObjectZY "名"{...}                            -> 1パーツ
//   TriangleZY / PistonZY { Link{...} Link{...} } -> 2パーツ(2つとも Link なので出現順で組にする)
//   CrankZY { Link{...} Slide{...} }              -> 2パーツ(Link=0、Slide=1)
// 組の2パーツには互いのラベルを partnerLabel として持たせる(姿勢は ZyLinkageSolve.h が
// 組で解くため)。RailSim II の読み込みは CFreeObjectContainer::Read(CTrainPlugin.cpp)。
void CollectZyGroup(const PiBlock& block, std::vector<ModelPart>& out) {
    // out へ push_back すると再配置でポインタ/参照が無効化されうるため、
    // 先にローカル変数として完成させてから最後にまとめて push する。
    std::vector<ModelPart> group;
    if (block.name == "ObjectZY") {
        ModelPart p;
        p.kind = block.name;
        p.label = block.label;
        WalkPart(block, p);
        ExtractZyPartInfo(block, ZyPartKind::ObjectZY, 0, p);
        group.push_back(std::move(p));
    } else {
        const bool isCrank = block.name == "CrankZY";
        const ZyPartKind linkKind = isCrank                    ? ZyPartKind::CrankLink
                                    : block.name == "PistonZY" ? ZyPartKind::PistonLink
                                                               : ZyPartKind::TriangleLink;
        int linkCount = 0;
        for (const auto& c : block.blocks) {
            const bool isLink = c.name == "Link";
            const bool isSlide = isCrank && c.name == "Slide";
            if (!isLink && !isSlide) continue;
            ModelPart p;
            p.kind = c.name;
            p.label = c.label;
            WalkPart(c, p);
            // Crank は Link=0・Slide=1。Triangle/Piston は Link の出現順。
            const int memberIndex = isCrank ? (isSlide ? 1 : 0) : linkCount;
            ExtractZyPartInfo(c, isSlide ? ZyPartKind::CrankSlide : linkKind, memberIndex, p);
            if (isLink) ++linkCount;
            group.push_back(std::move(p));
        }
        if (group.size() == 2 && group[0].zyInfo.memberIndex != group[1].zyInfo.memberIndex) {
            group[0].zyInfo.partnerLabel = group[1].label;
            group[1].zyInfo.partnerLabel = group[0].label;
        }
    }
    for (ModelPart& p : group) out.push_back(std::move(p));
}

// container 直下の FlagZY "名"{...} を読む。物理も描画もしないので、保持するのは
// Stiffness / AirResistance / Gravity だけ(Division・LeftTop・RightBottom・Clip は読み捨て)。
// Gravity は読んだ値に 1/60 を掛けて持つ(1フレームあたりの値に直す)。
void CollectFlagZys(const PiBlock& container, std::vector<FlagZyDef>& out) {
    constexpr float kFramesPerSecond = 60.0f;
    for (const PiBlock* block : container.Children("FlagZY")) {
        FlagZyDef flag;
        flag.label = block->label;
        if (const PiAssignment* a = block->Find("Stiffness"))
            if (a->First()) flag.stiffness = static_cast<float>(a->First()->AsFloat(flag.stiffness));
        if (const PiAssignment* a = block->Find("AirResistance"))
            if (a->First()) flag.airResistance = static_cast<float>(a->First()->AsFloat(flag.airResistance));
        if (const PiAssignment* a = block->Find("Gravity"))
            if (a->First()) flag.gravity = static_cast<float>(a->First()->AsFloat(0.0)) / kFramesPerSecond;
        out.push_back(std::move(flag));
    }
}

// Struct 最上位の LinePoint{ Coord; Dir } 1ブロックを読む。
LinePointDef ParseLinePoint(const PiBlock& node) {
    LinePointDef point;
    if (const PiAssignment* a = node.Find("Coord"))
        if (const PiValue* v = a->First()) v->AsVec3(point.coord.x, point.coord.y, point.coord.z);
    if (const PiAssignment* a = node.Find("Dir"))
        if (const PiValue* v = a->First()) v->AsVec3(point.dir.x, point.dir.y, point.dir.z);
    return point;
}

// PrimaryAssembly やトップレベルからパーツを収集
void CollectParts(const PiBlock& container, const std::vector<std::string>& effectorLabels,
                   std::vector<std::string>& warnings,
                   std::vector<ModelPart>& out) {
    std::vector<ModelPart> collected;
    for (const auto& c : container.blocks) {
        if (IsPartBlock(c.name)) {
            ModelPart p;
            p.kind = c.name;
            p.label = c.label;
            WalkPart(c, p);
            ExtractPlacement(c, p);
            collected.push_back(std::move(p));
        } else if (c.name == "ObjectZY" || c.name == "TriangleZY" || c.name == "PistonZY" ||
                   c.name == "CrankZY") {
            CollectZyGroup(c, collected);
        }
    }

    // RailSim II(CheckObjectHybrid、CNamedObject.cpp)はラベル(オブジェクト名)の
    // 重複を定義時にエラーとして拒否するため、有効なPIファイルには本来重複が存在しない。
    // ここでは読込全体は失敗させず(実データの破損耐性を優先し、Joint3D解決は既存どおり
    // 「最初に一致したラベル」で進む)、診断として警告だけ残す。
    {
        std::unordered_set<std::string> seenLabels;
        for (const ModelPart& p : collected) {
            if (p.label.empty()) continue;
            if (!seenLabels.insert(p.label).second) {
                warnings.push_back("duplicate part label (rejected by RailSim II at definition time): \"" +
                                   p.label + "\"");
            }
        }
    }

    // Joint3D "名"{...} が参照する基準名の集合。ModelFileName="" の純粋な
    // 位置アンカー（Object3D "HomeDummy" 等、StaticMove等で座標だけ持つ
    // ダミー）でも、他パーツから Joint3D の Target/DirLink/UpLink として
    // 参照されている場合は表示対象でなくても解決対象として残す必要がある
    // (ダミーを黙って捨てると
    //  参照元パーツが SchemaPlacement::findFrameMatrix で「unknown target」
    //  → identity 行列フォールバックとなり、位置ずれ/パーツ非表示になる)。
    std::unordered_set<std::string> referencedLabels;
    for (const auto& p : collected)
        for (const auto& j3 : p.joints3d) {
            referencedLabels.insert(j3.target);
            referencedLabels.insert(j3.dirLink);
            referencedLabels.insert(j3.upLink);
        }
    // JointZY(Body→Axle/Body の物理連結)の接続先も同様に残す必要がある。
    // Axle/BodyはModelFileNameが空("見えないダミー台車/車輪"の構成、実データで
    // 確認)でもJointZY経由でBodyPiece階層の自己アンカーとして必須であり、
    // Joint3Dからしか見ないこの走査では捕捉できず誤って刈られると、
    // 該当パーツ全体が解決不能のままワールド原点へ描画される。
    for (const auto& p : collected)
        for (const auto& j : p.joints)
            referencedLabels.insert(j.childLabel);
    // ZY系の可動部(ObjectZY/TriangleZY/PistonZY/CrankZY)は、モデルを持たない位置アンカーの
    // ことが多く、組の相手からしか参照されない。相手を落とすと姿勢の解決が「相手が見つからない」
    // まま据え置かれ、そのパーツに依存する Joint3D のチェーン全体が解決不能になる。
    // 全関節の取り付け先も Joint3D とは別の参照経路のため、上の joints3d 走査には現れない
    // (ModelFileName="" のダミーが取り付け先になる)。
    for (const auto& p : collected)
        if (p.isZyPart) {
            if (!p.zyInfo.partnerLabel.empty()) referencedLabels.insert(p.zyInfo.partnerLabel);
            for (int j = 0; j < p.zyInfo.jointCount; ++j)
                referencedLabels.insert(p.zyInfo.joints[j].target);
        }

    // エフェクタ(SoundEffect/Headlight/Particle)のAttachObjectが指す位置アンカー
    // (ModelFileName=""でJoint3D等からも参照されない、スピーカーや灯具の位置マーカー等)も、
    // 上と同じ理由で解決対象として残す必要がある(エフェクタ用ダミーObject3Dが
    // 無条件に捨てられると、AttachObject解決が全滅する)。
    for (const std::string& label : effectorLabels)
        referencedLabels.insert(label);

    // モデルを1つも持たないダミー（車体補間用 Body "Dummy" 等）も
    // テクスチャ情報だけ持つ場合があるが、表示対象は modelFile を持つもの。
    // ただし上記の通り Joint3D 参照先として必要なダミーは例外的に残す。
    //
    // changeModels を持つパーツも必ず残す: この時点の modelFile は
    // 「全スイッチ0」で焼いた結果なので、初期値で消える指定
    // (`If SW==0 { ChangeModel = "", 1.0; }` = セクションOFFなら非表示)の
    // パーツは modelFile が空になる。ここで捨てるとスイッチをONにしても
    // 二度と現れない。実際の非表示は下流(LoadModelVariant→ApplyModelVariant が
    // meshOk=false にする)が担当する。
    for (auto& p : collected) {
        if (!p.modelFile.empty() || !p.changeModels.empty() ||
            p.ChangeTextureCount() > 0 ||
            referencedLabels.count(p.label) > 0) {
            out.push_back(std::move(p));
        }
    }
}

} // namespace schema_detail

} // namespace rs::legacy_pi
