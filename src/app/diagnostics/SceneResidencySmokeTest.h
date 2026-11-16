//
// SceneResidencySmokeTest.h - --scene-residency-smoke ヘッドレス検証
//
// SceneRenderer/
// UploadSceneMeshToGpuの常駐スロット経路は--scene-instance-smoke(InstanceBatchManager
// 側)とは別経路のため、そちらでは検証できない。指定フォルダ配下の複数プラグイン
// サブフォルダを異なる常駐スロットへ実際にUploadSceneMeshToGpuで配置し、多数(8件超)を
// 動的SRVアロケータ経由でSRV枯渇無く常駐できることを確認する(GUI目視の代わり)。
//
#pragma once

#include <string>

namespace rs::app {

class App;
class CameraController;

// 起動時1回: rootFolderUtf8直下のサブフォルダを最大maxSlots件走査し、有効な
// プラグイン(組立メッシュ有り)だけを異なる常駐スロットへ配置する。戻り値trueなら
// EvaluateSceneResidencySmokeCapture をフレーム0の描画後に呼ぶこと。
bool SetupSceneResidencySmoke(App& app, CameraController& camera,
                              const std::string& rootFolderUtf8, int maxSlots);

// フレーム0でRequestBackBufferCapture済みのRender()呼び出し後に呼ぶ。
// 読み戻したバックバッファの非背景色ピクセル比率を判定してPASS/FAILをログし、
// PostQuitMessage(0)でプロセスを終了させる。
void EvaluateSceneResidencySmokeCapture(App& app);

} // namespace rs::app
