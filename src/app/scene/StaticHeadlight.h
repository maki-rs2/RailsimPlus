//
// StaticHeadlight.h - 静的バッチで描く配置の灯具1件(点灯条件が真のもの)
//
// 静的バッチはスロットを通らないので、灯具の位置は毎フレームの追跡(HeadlightWorldTracker)では求まらない。
// 動かない部品の姿勢は配置が決まれば変わらないので、静的な焼き込みのときに部品行列を控え、
// ワールド位置は集める側が配置の行列と合わせて求める(ComputeHeadlightWorldPose)。
//
#pragma once

#include "../../core/Math.h"
#include "../../plugins/load/runtime/HeadlightAttachment.h"

namespace rs::app {

struct StaticHeadlight {
    HeadlightAttachment attachment;
    rs::core::Mat4 partWorld = rs::core::Mat4::Identity();  // def.parts 添字の部品行列(配置の外側にかける前)
};

} // namespace rs::app
