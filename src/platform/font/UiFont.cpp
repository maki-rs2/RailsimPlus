#include "UiFont.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H

#include <spdlog/spdlog.h>

#include "../file/AssetPath.h"

namespace rs::platform_font {

std::string ResolveUiFontPath() {
    return rs::platform_file::ResolveAssetPath(kUiFontRelativePath);
}

long FindNamedInstanceFaceIndex(const std::string& fontPath, uint16_t weight) {
    FT_Library library = nullptr;
    if (FT_Init_FreeType(&library) != 0) {
        spdlog::warn("[platform_font] FreeTypeの初期化に失敗しました: {}", fontPath);
        return 0;
    }
    FT_Face face = nullptr;
    if (FT_New_Face(library, fontPath.c_str(), 0, &face) != 0) {
        spdlog::warn("[platform_font] フォントの読み込みに失敗しました: {}", fontPath);
        FT_Done_FreeType(library);
        return 0;
    }

    long faceIndex = 0;
    FT_MM_Var* mmVar = nullptr;
    if ((face->face_flags & FT_FACE_FLAG_MULTIPLE_MASTERS) == 0 || FT_Get_MM_Var(face, &mmVar) != 0) {
        spdlog::warn("[platform_font] 可変フォントではないため既定の太さで描画します: {}", fontPath);
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        return 0;
    }

    // wght軸のインデックスを軸の並び順に依存せず探す。
    FT_UInt weightAxis = mmVar->num_axis;
    for (FT_UInt axis = 0; axis < mmVar->num_axis; ++axis) {
        if (mmVar->axis[axis].tag == FT_MAKE_TAG('w', 'g', 'h', 't')) {
            weightAxis = axis;
            break;
        }
    }

    if (weightAxis < mmVar->num_axis) {
        const FT_Fixed targetCoord = static_cast<FT_Fixed>(weight) << 16;
        for (FT_UInt instance = 0; instance < mmVar->num_namedstyles; ++instance) {
            if (mmVar->namedstyle[instance].coords[weightAxis] == targetCoord) {
                // FreeTypeの名前付きインスタンス番号は1始まり、face_indexの上位16bitへ格納する。
                faceIndex = (static_cast<long>(instance + 1) << 16);
                break;
            }
        }
    }

    if (faceIndex == 0) {
        spdlog::warn("[platform_font] 太さ{}の名前付きインスタンスが見つかりません: {}", weight, fontPath);
    } else {
        spdlog::debug("[platform_font] 太さ{}の名前付きインスタンスのface_index={}: {}", weight,
                     faceIndex, fontPath);
    }

    FT_Done_MM_Var(library, mmVar);
    FT_Done_Face(face);
    FT_Done_FreeType(library);
    return faceIndex;
}

} // namespace rs::platform_font
