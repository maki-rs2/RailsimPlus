#pragma once
#include <string>
#include <vector>
namespace rs::app { class App; class CameraController; bool SetupTrainInstanceSmoke(App&, CameraController&, const std::string&, int); void EvaluateTrainInstanceSmokeCapture(App&, const std::string& capturePngPath, std::vector<std::string>& log); }
