#include "ThumbnailViewCamera.h"

#include "BrowserTuning.h"

#include "../../core/Epsilon.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include "../../core/Math.h"

namespace rs::app::plugin_browser {
namespace {

// 注視点を画面の中央へ寄せる繰り返しの回数(数回で収束する)。
constexpr int kBoxCenteringIterations = 4;

} // namespace

float ThumbnailFovYRadians() {
    return (BrowserTuning::Embedded().thumbnailFovYDeg * rs::core::kDegToRadF);
}

float ComputeThumbnailCameraDistance(float radius, float fovYRadians, float aspect, float fitMargin) {
    const float safeRadius = std::max(radius, BrowserTuning::Embedded().thumbnailMinBoundingRadiusM);
    const float halfFovY = 0.5f * fovYRadians;
    // 横の視野角の半分。縦横の狭い方に外接球を収める。
    const float halfFovX = std::atan(std::tan(halfFovY) * std::max(aspect, 0.01f));
    const float halfFovNarrow = std::min(halfFovY, halfFovX);
    // 球の見かけの半角は asin(radius/distance)。これが狭い方の半角に等しくなる距離。
    return fitMargin * safeRadius / std::sin(halfFovNarrow);
}

float ComputeSideFaceCameraDistance(float halfLength, float halfHeight, float halfDepth, float fovYRadians,
                                    float aspect, float fitMargin) {
    const float tanHalfY = std::tan(0.5f * fovYRadians);
    const float tanHalfX = tanHalfY * std::max(aspect, 0.01f);
    // 手前の面までの距離 d' = d - halfDepth で、半分の幅 d'*tan が矩形の半分に余白を掛けた値以上。
    const float widthDistance = fitMargin * std::max(halfLength, BrowserTuning::Embedded().thumbnailMinBoundingRadiusM) / tanHalfX;
    const float heightDistance = fitMargin * std::max(halfHeight, BrowserTuning::Embedded().thumbnailMinBoundingRadiusM) / tanHalfY;
    return halfDepth + std::max(widthDistance, heightDistance);
}

namespace {

struct BoxFit {
    float distance = 0.0f;
    float shift[3] = {}; // 注視点を AABB の中心からずらす量(画面の中央に箱が寄るように)
};

// 画面の右・上の軸。toCamera が真上でないかぎり、世界の上方向との外積で決まる。
void ScreenAxes(const float toCamera[3], float right[3], float up[3]) {
    right[0] = toCamera[2];
    right[1] = 0.0f;
    right[2] = -toCamera[0];
    const float rightLength = std::sqrt(right[0] * right[0] + right[2] * right[2]);
    if (rightLength < rs::core::kHorizontalDirectionEps) {
        right[0] = 1.0f;
        right[2] = 0.0f;
    } else {
        right[0] /= rightLength;
        right[2] /= rightLength;
    }
    up[0] = toCamera[1] * right[2] - toCamera[2] * right[1];
    up[1] = toCamera[2] * right[0] - toCamera[0] * right[2];
    up[2] = toCamera[0] * right[1] - toCamera[1] * right[0];
}

float Dot(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// 注視点を shift だけずらしたときの、8隅が収まる距離と、画面上の箱の中心のずれ(注視点の位置での長さ[m])。
float FitBoxAt(const float half[3], const float shift[3], const float toCamera[3], float tanHalfX, float tanHalfY,
               float centerOffset[2]) {
    float right[3];
    float up[3];
    ScreenAxes(toCamera, right, up);
    float corners[8][3];
    float distance = 0.0f;
    for (int corner = 0; corner < 8; ++corner) {
        for (int i = 0; i < 3; ++i) corners[corner][i] = ((corner >> i) & 1 ? half[i] : -half[i]) - shift[i];
        const float x = std::abs(Dot(corners[corner], right));
        const float y = std::abs(Dot(corners[corner], up));
        const float depth = Dot(corners[corner], toCamera);
        // 隅が手前 depth にあるとき、視線方向の距離は d - depth。半分の幅 (d - depth)*tan が x 以上。
        distance = std::max({distance, BrowserTuning::Embedded().thumbnailBoxFitMargin * x / tanHalfX + depth, BrowserTuning::Embedded().thumbnailBoxFitMargin * y / tanHalfY + depth});
    }
    distance = std::max(distance, BrowserTuning::Embedded().thumbnailMinBoundingRadiusM);
    float minX = 1e30f, maxX = -1e30f, minY = 1e30f, maxY = -1e30f;
    for (int corner = 0; corner < 8; ++corner) {
        const float scale = distance / std::max(distance - Dot(corners[corner], toCamera), BrowserTuning::Embedded().thumbnailMinBoundingRadiusM);
        const float x = Dot(corners[corner], right) * scale;
        const float y = Dot(corners[corner], up) * scale;
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
        minY = std::min(minY, y);
        maxY = std::max(maxY, y);
    }
    centerOffset[0] = 0.5f * (minX + maxX);
    centerOffset[1] = 0.5f * (minY + maxY);
    return distance;
}

// 遠近で手前が大きく写るため、AABB の中心を見ると箱が画面の端へ寄る。注視点をずらして中央に寄せる。
BoxFit FitBox(const float half[3], const float toCamera[3], float fovYRadians, float aspect) {
    const float tanHalfY = std::tan(0.5f * fovYRadians);
    const float tanHalfX = tanHalfY * std::max(aspect, 0.01f);
    float right[3];
    float up[3];
    ScreenAxes(toCamera, right, up);
    BoxFit fit;
    for (int iteration = 0; iteration < kBoxCenteringIterations; ++iteration) {
        float offset[2] = {};
        fit.distance = FitBoxAt(half, fit.shift, toCamera, tanHalfX, tanHalfY, offset);
        for (int i = 0; i < 3; ++i) fit.shift[i] += right[i] * offset[0] + up[i] * offset[1];
    }
    float offset[2] = {};
    fit.distance = FitBoxAt(half, fit.shift, toCamera, tanHalfX, tanHalfY, offset);
    return fit;
}

} // namespace

float ComputeBoxCameraDistance(const float half[3], const float toCamera[3], float fovYRadians, float aspect) {
    return FitBox(half, toCamera, fovYRadians, aspect).distance;
}

namespace {

// 編成写真の構図(ThumbnailFit::TrainPhoto)の当てはめ結果。
// カメラは先頭の端面の中心から toCamera へ distance 離れ、画面の右・上へ shiftRight・shiftUp だけずれた位置にある。
struct TrainPhotoFit {
    float shiftRight = 0.0f;
    float shiftUp = 0.0f;
    float tanHalfX = 0.0f;
    float tanHalfY = 0.0f;
    float frontHeightRatio = 0.0f; // 先頭の端面の高さが画面の高さに占める割合
    float farDepth = 0.0f;         // いちばん遠い隅までの視線方向の距離[m]
};

// ずれの更新の繰り返し回数(端になる隅が入れ替わらなければ数回で収束する)。
constexpr int kTrainPhotoFitIterations = 8;
// 距離を求める二分探索の回数と、距離の下限[m]。
constexpr int kTrainPhotoSearchSteps = 50;
constexpr float kTrainPhotoMinDistanceM = 1.0f;
// 隅がカメラの後ろへ回っても割り算が壊れないようにする、視線方向の距離の下限[m]。
constexpr float kTrainPhotoMinDepthM = 0.05f;
// 二分探索が距離の上限に当たったとみなす、先頭の割合の目標に対する超過の倍率。
constexpr float kTrainPhotoRatioTolerance = 1.01f;

// fixedTanHalfY が 0 なら、先頭を右端・最後尾を左端に余白つきで合わせる視野角を求める。
// 正なら視野角はその値に固定し、先頭だけを右端に合わせる(最後尾は左端より内側に残る)。
TrainPhotoFit FitTrainPhoto(const float aabbMin[3], const float aabbMax[3], const float front[3],
                            const float toCamera[3], float distance, float aspect, float fixedTanHalfY) {
    const BrowserTuning& tuning = BrowserTuning::Embedded();
    const float margin = tuning.trainPhotoSideMargin;
    const float safeAspect = std::max(aspect, 0.01f);
    float right[3];
    float up[3];
    ScreenAxes(toCamera, right, up);
    TrainPhotoFit fit;
    for (int iteration = 0; iteration < kTrainPhotoFitIterations; ++iteration) {
        float minX = 1e30f, maxX = -1e30f, frontMinY = 1e30f, frontMaxY = -1e30f;
        float leftDepth = distance, rightDepth = distance, frontDepth = distance;
        fit.farDepth = 0.0f;
        for (int corner = 0; corner < 8; ++corner) {
            const bool atFront = (corner & 4) != 0;
            const float offset[3] = {((corner & 1) ? aabbMax[0] : aabbMin[0]) - front[0],
                                     ((corner & 2) ? aabbMax[1] : aabbMin[1]) - front[1],
                                     (atFront ? aabbMax[2] : aabbMin[2]) - front[2]};
            const float depth = std::max(distance - Dot(offset, toCamera), kTrainPhotoMinDepthM);
            fit.farDepth = std::max(fit.farDepth, depth);
            const float x = (Dot(offset, right) - fit.shiftRight) / depth;
            const float y = (Dot(offset, up) - fit.shiftUp) / depth;
            if (x < minX) {
                minX = x;
                leftDepth = depth;
            }
            if (x > maxX) {
                maxX = x;
                rightDepth = depth;
            }
            if (atFront) {
                frontMinY = std::min(frontMinY, y);
                frontMaxY = std::max(frontMaxY, y);
                frontDepth = depth;
            }
        }
        // カメラを shift だけずらすと、距離 depth の隅は画面上で shift/depth だけ逆へ動く。
        if (fixedTanHalfY > 0.0f) {
            fit.tanHalfY = fixedTanHalfY;
            fit.tanHalfX = fixedTanHalfY * safeAspect;
            fit.shiftRight += (maxX - (1.0f - margin) * fit.tanHalfX) * rightDepth;
        } else {
            fit.shiftRight += 0.5f * (minX + maxX) / (0.5f * (1.0f / leftDepth + 1.0f / rightDepth));
            fit.tanHalfX = 0.5f * (maxX - minX) / (1.0f - margin);
            fit.tanHalfY = fit.tanHalfX / safeAspect;
        }
        fit.frontHeightRatio = (frontMaxY - frontMinY) / (2.0f * fit.tanHalfY);
        const float frontCenterY = 0.5f * (frontMinY + frontMaxY);
        fit.shiftUp += (frontCenterY - tuning.trainPhotoFrontCenterY * fit.tanHalfY) * frontDepth;
    }
    return fit;
}

// 編成の長さ[m]から方位[deg]を決める。長い編成ほど正面寄りにする。
float TrainPhotoAzimuthDeg(const ThumbnailViewTraits& traits, float trainLengthM) {
    const BrowserTuning& tuning = BrowserTuning::Embedded();
    const float span = tuning.trainPhotoLongLengthM - tuning.trainPhotoShortLengthM;
    const float ratio = span > 0.0f ? std::clamp((trainLengthM - tuning.trainPhotoShortLengthM) / span, 0.0f, 1.0f)
                                    : 1.0f;
    return std::lerp(traits.azimuthDeg, tuning.trainPhotoLongAzimuthDeg, ratio);
}

// plan.toCamera が決まった後に、目と注視点・視野角・遠クリップを決める。
void PlaceTrainPhotoCamera(const float aabbMin[3], const float aabbMax[3], float aspect, float sphereRadius,
                           ThumbnailCameraPlan& plan) {
    const BrowserTuning& tuning = BrowserTuning::Embedded();
    const float front[3] = {0.5f * (aabbMin[0] + aabbMax[0]), 0.5f * (aabbMin[1] + aabbMax[1]), aabbMax[2]};
    const float targetRatio = tuning.trainPhotoFrontHeightRatio;
    // 遠ざかるほど先頭の端面は画面の中で小さくなる。目標の割合になる距離を、対数の二分探索で求める。
    float nearDistance = kTrainPhotoMinDistanceM;
    float farDistance = std::max(tuning.trainPhotoMaxDistanceM, kTrainPhotoMinDistanceM);
    for (int step = 0; step < kTrainPhotoSearchSteps; ++step) {
        const float middle = std::sqrt(nearDistance * farDistance);
        const TrainPhotoFit fit = FitTrainPhoto(aabbMin, aabbMax, front, plan.toCamera, middle, aspect, 0.0f);
        if (fit.frontHeightRatio > targetRatio) {
            nearDistance = middle;
        } else {
            farDistance = middle;
        }
    }
    const float distance = std::sqrt(nearDistance * farDistance);
    TrainPhotoFit fit = FitTrainPhoto(aabbMin, aabbMax, front, plan.toCamera, distance, aspect, 0.0f);
    if (fit.frontHeightRatio > targetRatio * kTrainPhotoRatioTolerance) {
        // 距離の上限でも先頭が大きすぎる(短い編成を正面寄りから撮るとき)。視野角を広げて先頭の大きさを合わせる。
        fit = FitTrainPhoto(aabbMin, aabbMax, front, plan.toCamera, distance, aspect,
                            fit.frontHeightRatio * fit.tanHalfY / targetRatio);
    }
    float right[3];
    float up[3];
    ScreenAxes(plan.toCamera, right, up);
    for (int i = 0; i < 3; ++i) {
        plan.eye[i] = front[i] + plan.toCamera[i] * distance + right[i] * fit.shiftRight + up[i] * fit.shiftUp;
        plan.target[i] = plan.eye[i] - plan.toCamera[i] * distance;
    }
    plan.fovYRadians = 2.0f * std::atan(fit.tanHalfY);
    plan.renderDistance = fit.farDepth + sphereRadius;
}

} // namespace

void RotateAboutY(const float in[3], float yawRadians, float out[3]) {
    const float c = std::cos(yawRadians);
    const float s = std::sin(yawRadians);
    const float x = in[0] * c - in[2] * s;
    const float y = in[1];
    const float z = in[0] * s + in[2] * c;
    out[0] = x;
    out[1] = y;
    out[2] = z;
}

ThumbnailCameraPlan ComputeThumbnailCamera(ThumbnailView view, const float aabbMin[3], const float aabbMax[3],
                                           float aspect) {
    const ThumbnailViewTraits& traits = GetThumbnailView(view);
    float center[3] = {};
    float half[3] = {};
    for (int i = 0; i < 3; ++i) {
        center[i] = 0.5f * (aabbMin[i] + aabbMax[i]);
        half[i] = 0.5f * (aabbMax[i] - aabbMin[i]);
    }
    const float sphereRadius = std::sqrt(half[0] * half[0] + half[1] * half[1] + half[2] * half[2]);

    const float azimuthDeg = traits.fit == ThumbnailFit::TrainPhoto
                                 ? TrainPhotoAzimuthDeg(traits, aabbMax[2] - aabbMin[2])
                                 : traits.azimuthDeg;
    const float azimuth = azimuthDeg * rs::core::kDegToRadF;
    const float elevation = traits.elevationDeg * rs::core::kDegToRadF;
    ThumbnailCameraPlan plan;
    plan.modelYawRadians = azimuth;
    const float fovYRadians = traits.fovYDeg * rs::core::kDegToRadF;
    plan.fovYRadians = fovYRadians;
    const float horizontal = std::cos(elevation);
    plan.toCamera[0] = horizontal * std::sin(azimuth);
    plan.toCamera[1] = std::sin(elevation);
    plan.toCamera[2] = horizontal * std::cos(azimuth);
    if (traits.fit == ThumbnailFit::TrainPhoto) {
        PlaceTrainPhotoCamera(aabbMin, aabbMax, aspect, sphereRadius, plan);
        return plan;
    }

    float fitRadius = sphereRadius;
    for (int i = 0; i < 3; ++i) plan.target[i] = center[i];
    if (traits.fit == ThumbnailFit::EndFace) {
        plan.target[2] = plan.toCamera[2] >= 0.0f ? aabbMax[2] : aabbMin[2];
        fitRadius = BrowserTuning::Embedded().thumbnailEndFaceSectionScale * std::sqrt(half[0] * half[0] + half[1] * half[1]);
    }
    float distance = 0.0f;
    if (traits.fit == ThumbnailFit::SideFace) {
        distance = ComputeSideFaceCameraDistance(half[2], half[1], half[0], fovYRadians, aspect, traits.fitMargin);
    } else if (traits.fit == ThumbnailFit::BoundingBox) {
        const BoxFit fit = FitBox(half, plan.toCamera, fovYRadians, aspect);
        distance = fit.distance;
        for (int i = 0; i < 3; ++i) plan.target[i] += fit.shift[i];
    } else {
        distance = ComputeThumbnailCameraDistance(fitRadius, fovYRadians, aspect, traits.fitMargin);
    }
    for (int i = 0; i < 3; ++i) plan.eye[i] = plan.target[i] + plan.toCamera[i] * distance;
    plan.renderDistance = distance + sphereRadius * BrowserTuning::Embedded().thumbnailFarClipRadiusMultiplier;
    return plan;
}

} // namespace rs::app::plugin_browser
