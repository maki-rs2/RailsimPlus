//
// LabeledFrameResolver.cpp - ラベル付きフレームの実行時ワールド行列を解決する。
//
#include "LabeledFrameResolver.h"

#include "../../schema/PluginSchema.h"

namespace rs::app {

bool FindLabeledFrame(const std::string& name, const std::vector<std::string>& labels,
                      const std::vector<bool>& resolved,
                      const std::vector<rs::core::Mat4>& world, rs::core::Mat4& outMat) {
    if (rs::legacy_pi::IsSystemJointTargetName(name)) {
        outMat = rs::core::Mat4::Identity();
        return true;
    }
    for (size_t i = 0; i < labels.size(); ++i) {
        if (labels[i] != name) continue;
        if (!resolved[i]) return false;
        outMat = world[i];
        return true;
    }
    outMat = rs::core::Mat4::Identity();
    return true;
}

} // namespace rs::app
