#include "CameraOverride.h"

#include <exception>
#include <string>

#include <spdlog/spdlog.h>

namespace rs::app {

// --camera の`yaw,pitch,distance[,tx,ty,tz]`をカメラへ適用する。読込完了時のAABB
// フィットに毎フレーム勝たせたいので、値の解析だけ初回に済ませて適用は毎フレーム行う。
void ApplyCameraOverride(const std::string& spec, FlyCamera& fly, CameraOverrideState& state) {
    bool& parsed = state.parsed;
    bool& valid = state.valid;
    float (&values)[6] = state.values;
    int& count = state.count;
    if (!parsed) {
        parsed = true;
        std::size_t pos = 0;
        while (pos <= spec.size() && count < 6) {
            const std::size_t comma = spec.find(',', pos);
            const std::string token =
                spec.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
            try {
                values[count++] = std::stof(token);
            } catch (const std::exception&) {
                spdlog::error("[camera] --camera の値を解析できません: '{}'", token);
                return;
            }
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
        valid = (count == 3 || count == 6);
        if (!valid) {
            spdlog::error("[camera] --camera は yaw,pitch,distance[,tx,ty,tz] の3個か6個: '{}'",
                          spec);
        }
    }
    if (!valid) return;
    fly.yaw = values[0];
    fly.pitch = values[1];
    fly.distance = values[2];
    if (count == 6) {
        fly.target[0] = values[3];
        fly.target[1] = values[4];
        fly.target[2] = values[5];
    }
}

} // namespace rs::app
