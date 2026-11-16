//
// ActiveEnv.h - 現在のEnvプラグイン(アクティブなSceneのEnvPlugin)の定義を保持する
//
// 各SceneはEnvPlugin名(LayoutSceneCache::envPlugin)を持ち、アクティブなSceneのEnvが
// 「現在のEnv」になる。Update()を毎フレーム呼ぶと、アクティブSceneの(名前, フォルダ)が
// 前回と変わったときだけ定義を読み直してログを出す。Scene切替・レイアウト読込・
// 自動復元のどの経路でactiveIndexが変わっても、ここ1か所で追従する。
//
// 描画には使わない。反映するのは太陽の通り道の緯度だけ(SolarLatitudeDeg)。
//
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "../../plugins/schema/EnvSchema.h"

namespace rs::app {

struct LayoutSession;
struct LayoutSceneCache;

// 焼き済みキューブ3つ(拡散・鏡面・BRDF LUT)のフルパス(UTF-8)。
// 3つのうち1つでも欠けていたらすべて空(=キューブ指定なし)。
struct EnvCubePaths {
    std::string diffuse;
    std::string specular;
    std::string brdfLut;

    bool Empty() const { return diffuse.empty(); }  // 欠けていれば全部空なので1つ見れば足りる
};

// プラグインの空が描く Sun.x・Moon.x・Landscape.x のフルパス(UTF-8)と ModelScale。指定が無ければ空文字列(その天体は描かない)。
struct EnvSkyBodyPaths {
    std::string sunModel;
    double sunScale = 1.0;
    std::string moonModel;
    double moonScale = 1.0;
    std::string landscapeModel;
    double landscapeScale = 1.0;
};

// Envの定義とフォルダから、焼き済みキューブ3つのフルパスを解決する。キューブ指定がない・一部だけ欠けている・
// 背景(EnvMapTexFileName)がキューブの.ddsでないときはすべて空。ActiveEnv::CubePathsと、シーンの環境の選択肢の判定が共用する。
EnvCubePaths ResolveEnvCubePaths(const std::string& folderUtf8, const rs::legacy_pi::EnvDefinition& env);

class ActiveEnv {
public:
    // 開発用の上書き(RS_ENV_OVERRIDE)を解決するときだけ呼ばれる、プラグインルート候補(優先度順)の取得関数。
    using RootCandidatesFn = std::function<std::vector<std::string>()>;

    // アクティブSceneのEnvが前回と変わっていたら読み直す。Sceneが空・範囲外・Env未指定・
    // 解決失敗のときは「Envなし」にする。失敗しても比較用の値は更新する(毎フレーム
    // 読み直し・ログ出力を繰り返さない)。
    // 環境変数 RS_ENV_OVERRIDE=<Env名> があれば、Sceneのenv名の代わりにその名前を使う(起動後に1回だけ読み、
    // 名前→フォルダの解決も初回に1回だけ行う。ルート設定が後で変わっても追従しない開発用機能)。
    void Update(const LayoutSession& session, const RootCandidatesFn& rootCandidates);

    // 現在のEnvの定義。Envなしならnullptr。
    const rs::legacy_pi::EnvDefinition* Definition() const;

    // 現在のEnvの焼き済みキューブ4つのフルパス。Envなし・定義にキューブ指定がない・一部だけ欠けている・
    // 背景(EnvMapTexFileName)がキューブの.ddsでない(2Dの.png等)ときはすべて空。
    EnvCubePaths CubePaths() const;

    // 現在のEnvの Sun.x・Moon.x のフルパス。Envなしならどちらも空。
    EnvSkyBodyPaths SkyBodyPaths() const;

    // 現在のEnvのフォルダからの相対パス(定義に書かれたファイル名)のフルパス(UTF-8)。Envなし・空の名前なら空。
    std::string ResolveFilePath(const std::string& relativePathUtf8) const;

    // 現在のEnvの名前(ログ用。Envなしなら空)。
    const std::string& Name() const { return name_; }

    // Envの「名前+フォルダ」が変わるたびに増える番号。最初のUpdateでも1になる。
    std::uint64_t Generation() const { return generation_; }

    // 太陽の通り道に使う緯度[度]。現在のEnvにLatitudeがあればその値、なければfallbackDeg
    // (環境プリセットの緯度)。昼夜判定・季節スイッチ・太陽位置はすべてこの値を使う。
    double SolarLatitudeDeg(double fallbackDeg) const;

private:
    std::string                                key_;  // 前回の「名前+フォルダ」
    std::string                                name_;
    std::string                                folderUtf8_;
    std::uint64_t                              generation_ = 0;
    std::optional<rs::legacy_pi::EnvDefinition> definition_;

    // RS_ENV_OVERRIDE。overrideRead_=環境変数を読んだか、overrideName_=有効な上書き名(解決に失敗したら空に戻す)。
    bool                                       overrideRead_ = false;
    bool                                       overrideResolved_ = false;
    std::string                                overrideName_;
    std::string                                overrideFolderUtf8_;
};

// プラグインルート候補(優先度順)から、Env/<名前>/ が存在する最初のフォルダ(UTF-8)を返す。
// 名前が空・どの候補にも無ければ空文字列。
std::string ResolveEnvPluginFolder(const std::vector<std::string>& rootCandidatesUtf8,
                                   const std::string& envName);

// 各SceneのenvPluginを解決して envFolderUtf8 へ入れる(.rs2・.rslの読込で共通。反映の直前に呼ぶ)。
void ResolveSceneEnvFolders(std::vector<LayoutSceneCache>& scenes,
                            const std::vector<std::string>& rootCandidatesUtf8);

} // namespace rs::app
