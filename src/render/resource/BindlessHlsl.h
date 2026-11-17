#pragma once

namespace rs::render {

// 型ごとにレジスタ空間を分け、どの配列も現在のヒープの先頭を添字0として読む。
inline constexpr const char* kBindlessHlsl = R"HLSL(
Texture2D<float4> gBindlessTex2D[] : register(t0, space100);
Texture2D<float2> gBindlessTex2DRg[] : register(t0, space101);
Texture2D<float> gBindlessTex2DR[] : register(t0, space102);
Texture2DArray<float> gBindlessTex2DArrR[] : register(t0, space103);
Texture3D<float4> gBindlessTex3D[] : register(t0, space104);
TextureCube<float4> gBindlessTexCube[] : register(t0, space105);
RWTexture3D<float4> gBindlessRwTex3D[] : register(u0, space106);
)HLSL";

} // namespace rs::render
