//
// SceneChoiceCatalog.h - シーンに選べる地形と環境の一覧(プラグインルートの Surface/・Env/ の直下を走査する)
//
// 地形は、シーンを追加する瞬間にだけ選ぶ。環境は、焼き済みキューブを持つものだけを選択肢にする
// (キューブの判定は ActiveEnv と共通の ResolveEnvCubePaths)。ブラウザのカタログは Surface・Env を走査しないので、
// ここで直接列挙する。フォルダを読むので毎フレームは呼ばず、呼び出し側が結果を持つ。
//
#pragma once

#include <string>
#include <vector>

namespace rs::app {

struct SceneChoice {
    std::string name;        // 表示名(フォルダ名。環境は .rs2/.rsl の envPlugin に入る名前でもある)
    std::string folderUtf8;  // 採用したルートでの絶対パス
    bool operator==(const SceneChoice&) const = default;
};

struct SceneChoiceCatalog {
    std::vector<SceneChoice> surfaces;  // 定義を読めた Surface プラグイン
    std::vector<SceneChoice> envs;      // 焼き済みキューブを持つ Env プラグイン
    bool operator==(const SceneChoiceCatalog&) const = default;
};

// rootCandidatesUtf8 は優先度順(BuildPluginRootCandidates の結果)。同じ名前が複数のルートにあれば先のルートを採用する。
// 並びは名前の昇順(大文字小文字を区別しない)。
SceneChoiceCatalog ScanSceneChoices(const std::vector<std::string>& rootCandidatesUtf8);

}  // namespace rs::app
