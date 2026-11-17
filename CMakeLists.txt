cmake_minimum_required(VERSION 3.28)

project(RailsimPlus VERSION 0.1.0 LANGUAGES CXX RC)

option(RAILSIM_DEV_ENV "Enable development-only environment variable overrides" ON)

# テストのソース(tests/)を含まない配布用のソースでも構成できるよう、tests/ が無ければ既定でOFFにする。
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    set(_rs_build_tests_default ON)
else()
    set(_rs_build_tests_default OFF)
endif()
option(RS_BUILD_TESTS "Build the Catch2 test executables" ${_rs_build_tests_default})
if(RS_BUILD_TESTS AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    message(FATAL_ERROR "RS_BUILD_TESTS=ON ですが tests/ がありません")
endif()

# Windows以外ではDX12に触れない部分だけを組み立てる(描画なしのテスト実行体)。
if(NOT WIN32)
    if(NOT RS_BUILD_TESTS)
        message(FATAL_ERROR "Windows以外ではテスト(RS_BUILD_TESTS=ON)しか組み立てられません")
    endif()
    set(CMAKE_CXX_STANDARD 23)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    include(cmake/Portable.cmake)
    return()
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# VSCode等のIntelliSenseがビルド設定(NOMINMAX等)を認識できるよう compile_commands.json を出力
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 依存は当面 FetchContent で取得（依存が増えたら vcpkg manifest へ移行予定）
include(FetchContent)

FetchContent_Declare(
  openal-soft
  GIT_REPOSITORY https://github.com/kcat/openal-soft.git
  GIT_TAG        1.25.2
)

# OpenAL Soft側の不要なオプションを無効化(サンプル/ユーティリティ等)
set(ALSOFT_UTILS OFF CACHE BOOL "" FORCE)
set(ALSOFT_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ALSOFT_TESTS OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL OFF CACHE BOOL "" FORCE)
# ビルドのたびにgitでversion.hを作り直すのを止める。取得したソースの所有者と違うユーザーが
# ビルドするとgitが失敗して中身が変わり、OpenALが毎回コンパイルし直されるため。
set(ALSOFT_UPDATE_BUILD_VERSION OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS ON CACHE BOOL "Build shared libs" FORCE)

FetchContent_MakeAvailable(openal-soft)
if(MSVC)
    target_compile_options(OpenAL PRIVATE /FS)
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libs" FORCE)

# --- 音声デコード(miniaudio。WAV/FLAC/MP3のみ) ---
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        0.11.25
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  cmake_not_used
)
FetchContent_MakeAvailable(miniaudio)
add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE
    $<BUILD_INTERFACE:${miniaudio_SOURCE_DIR}>
)

# --- RmlUi のフォントエンジン(FreeType)。RmlUi自身はfind_packageするだけで
#     取得はしないため、ここでFetchContentし、find_packageが見るターゲット名
#     (Freetype::Freetype)を自前で用意する ---
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    freetype
    GIT_REPOSITORY https://gitlab.freedesktop.org/freetype/freetype.git
    GIT_TAG        VER-2-15-0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(freetype)
if(NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
endif()

# --- UIウィジェットフレームワーク(RmlUi)。本体への統合は src/app/rmlui/ ---
set(RMLUI_TESTS OFF CACHE BOOL "" FORCE)
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_FONT_ENGINE "freetype" CACHE STRING "" FORCE)
FetchContent_Declare(
    rmlui
    GIT_REPOSITORY https://github.com/mikke89/RmlUi.git
    GIT_TAG        6.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(rmlui)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.90.9-docking
    GIT_SHALLOW    TRUE
)

# --- 単体テスト(数値計算ロジック検証) ---
FetchContent_Declare(
    catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.16.1
    GIT_SHALLOW    TRUE
)

# --- パラメータ外部ファイル化(JSON) ---
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
)

# --- DX12開発補助(GraphicsMemory/DescriptorHeap等) ---
# DirectXTK12はシェーダ事前コンパイルにdxc.exeが必要だが、同梱CMakeの探索は
# CMAKE_SYSTEM_VERSION(ホストOS版)とSDKディレクトリ名がずれると失敗するため、
# Windows SDKのbin配下から明示的に検出してキャッシュ変数で渡す
if(NOT DIRECTX_DXC_TOOL)
    file(GLOB _rs_dxc_candidates "C:/Program Files (x86)/Windows Kits/10/bin/10.*/x64/dxc.exe")
    if(_rs_dxc_candidates)
        list(SORT _rs_dxc_candidates)
        list(GET _rs_dxc_candidates -1 _rs_dxc_latest)
        set(DIRECTX_DXC_TOOL "${_rs_dxc_latest}" CACHE FILEPATH "Path to dxc.exe for DirectXTK12 shaders")
        message(STATUS "railsim: DIRECTX_DXC_TOOL=${DIRECTX_DXC_TOOL}")
    endif()
endif()

# --- M5フェーズ2(Bindless化)用: 実行時HLSLコンパイルをFXC(D3DCompile)からDXC
# (IDxcCompiler3)へ移行するため、dxc.exeと同じWindows SDKからdxcompiler.lib/.dllと
# dxil.dllを解決する。dxc.exe自体は上のDIRECTX_DXC_TOOLで既に検出済み ---
if(DIRECTX_DXC_TOOL)
    get_filename_component(_rs_dxc_bin_dir "${DIRECTX_DXC_TOOL}" DIRECTORY)
    get_filename_component(_rs_dxc_sdk_bin_root "${_rs_dxc_bin_dir}/../.." ABSOLUTE)
    get_filename_component(_rs_dxc_sdk_version "${_rs_dxc_bin_dir}/.." NAME)
    file(GLOB _rs_dxcompiler_lib_candidates
        "C:/Program Files (x86)/Windows Kits/10/Lib/${_rs_dxc_sdk_version}/um/x64/dxcompiler.lib")
    if(NOT _rs_dxcompiler_lib_candidates)
        file(GLOB _rs_dxcompiler_lib_candidates
            "C:/Program Files (x86)/Windows Kits/10/Lib/10.*/um/x64/dxcompiler.lib")
        list(SORT _rs_dxcompiler_lib_candidates)
    endif()
    if(_rs_dxcompiler_lib_candidates AND EXISTS "${_rs_dxc_bin_dir}/dxcompiler.dll")
        list(GET _rs_dxcompiler_lib_candidates -1 _rs_dxcompiler_lib)
        add_library(dxcompiler SHARED IMPORTED GLOBAL)
        set_target_properties(dxcompiler PROPERTIES
            IMPORTED_LOCATION "${_rs_dxc_bin_dir}/dxcompiler.dll"
            IMPORTED_IMPLIB   "${_rs_dxcompiler_lib}"
        )
        set(RS_DXCOMPILER_DLL "${_rs_dxc_bin_dir}/dxcompiler.dll")
        set(RS_DXIL_DLL "${_rs_dxc_bin_dir}/dxil.dll")
        message(STATUS "railsim: dxcompiler.lib=${_rs_dxcompiler_lib}")
    else()
        message(FATAL_ERROR "railsim: dxcompiler.lib/.dllが見つかりません(Windows SDKを確認してください)")
    endif()
else()
    message(FATAL_ERROR "railsim: dxc.exeが見つからずDXC移行に必要なdxcompiler.libを解決できません")
endif()
set(BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(BUILD_SAMPLE OFF CACHE BOOL "" FORCE)
# 音声はOpenAL Soft(src/sim/audio/)で完結しており、DirectXTK12のAudioコンポーネント
# (XAudio2ラッパー)は使わない。既定ONなので明示的に切ってビルド対象から外す。
set(BUILD_XAUDIO_WIN10 OFF CACHE BOOL "" FORCE)
set(BUILD_XAUDIO_REDIST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    directxtk12
    GIT_REPOSITORY https://github.com/microsoft/DirectXTK12.git
    GIT_TAG        may2026
    GIT_SHALLOW    TRUE
)

# --- テクスチャ処理(DDS/ミップ生成/BC圧縮) ---
set(BUILD_DX11 OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    directxtex
    GIT_REPOSITORY https://github.com/microsoft/DirectXTex.git
    GIT_TAG        may2026
    GIT_SHALLOW    TRUE
)

# --- メッシュ処理(法線/接線計算・LOD・頂点キャッシュ最適化) ---
FetchContent_Declare(
    directxmesh
    GIT_REPOSITORY https://github.com/microsoft/DirectXMesh.git
    GIT_TAG        may2026
    GIT_SHALLOW    TRUE
)

# --- ログ出力(レベル管理・ファイル出力) ---
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.17.0
    GIT_SHALLOW    TRUE
)

# --- コマンドライン引数パース ---
FetchContent_Declare(
    cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG        v2.7.2
    GIT_SHALLOW    TRUE
)

# --- enum⇔文字列変換(JSONシリアライズ/ImGuiドロップダウン) ---
FetchContent_Declare(
    magic_enum
    GIT_REPOSITORY https://github.com/Neargye/magic_enum.git
    GIT_TAG        v0.9.8
    GIT_SHALLOW    TRUE
)

# --- .xモデル読込(assimp、bzip/tzip圧縮.x対応) ---
# ビルド時間短縮・依存最小化のためX importerのみ有効化。他ライブラリと同じ静的リンク方針。
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_X_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    assimp
    GIT_REPOSITORY https://github.com/assimp/assimp.git
    GIT_TAG        v6.0.5
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES ""
)

set(_rs_test_deps)
if(RS_BUILD_TESTS)
    list(APPEND _rs_test_deps catch2)
endif()
FetchContent_MakeAvailable(imgui ${_rs_test_deps} nlohmann_json directxtk12 directxtex directxmesh spdlog cli11 magic_enum assimp)

# DirectXTK12のシェーダ生成custom commandは `cmake -E env CompileShaders.cmd` を直接execするため
# cmd.exe経由でないと"no such file or directory"で失敗する。configure時にここで一度
# プリコンパイルして出力を揃え、build時のcustom commandは最新判定でスキップさせる
set(_rs_tk12_shader_marker "${directxtk12_BINARY_DIR}/Shaders/Compiled/SpriteEffect_SpriteVertexShader.inc")
if(NOT EXISTS "${_rs_tk12_shader_marker}")
    file(MAKE_DIRECTORY "${directxtk12_BINARY_DIR}/Shaders/Compiled")
    message(STATUS "railsim: pre-compiling DirectXTK12 shaders...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env
            "CompileShadersOutput=${directxtk12_BINARY_DIR}/Shaders/Compiled"
            "DirectXShaderCompiler=${DIRECTX_DXC_TOOL}"
            cmd /c "${directxtk12_SOURCE_DIR}/Src/Shaders/CompileShaders.cmd" dxil
        WORKING_DIRECTORY "${directxtk12_SOURCE_DIR}/Src/Shaders"
        RESULT_VARIABLE _rs_tk12_shader_result
        OUTPUT_VARIABLE _rs_tk12_shader_out
        ERROR_VARIABLE _rs_tk12_shader_out)
    if(NOT _rs_tk12_shader_result EQUAL 0 OR NOT EXISTS "${_rs_tk12_shader_marker}")
        message(FATAL_ERROR "railsim: DirectXTK12 shader pre-compile failed:\n${_rs_tk12_shader_out}")
    endif()
endif()

# --- GPU計測(PIXマーカー)。NuGetパッケージをURL取得しimportedターゲット化 ---
FetchContent_Declare(
    winpixeventruntime
    URL https://www.nuget.org/api/v2/package/WinPixEventRuntime/1.0.240308001
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(winpixeventruntime)
add_library(winpix SHARED IMPORTED GLOBAL)
set_target_properties(winpix PROPERTIES
    IMPORTED_LOCATION "${winpixeventruntime_SOURCE_DIR}/bin/x64/WinPixEventRuntime.dll"
    IMPORTED_IMPLIB   "${winpixeventruntime_SOURCE_DIR}/bin/x64/WinPixEventRuntime.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${winpixeventruntime_SOURCE_DIR}/Include"
)
target_compile_definitions(winpix INTERFACE USE_PIX)

add_library(imgui_dx12 STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
    ${imgui_SOURCE_DIR}/misc/freetype/imgui_freetype.cpp
)
target_include_directories(imgui_dx12 PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imgui_SOURCE_DIR}/misc/freetype
)
target_compile_definitions(imgui_dx12 PUBLIC UNICODE _UNICODE IMGUI_ENABLE_FREETYPE)
target_link_libraries(imgui_dx12 PUBLIC Freetype::Freetype)

# --- 開発側調整パラメータのビルド時埋め込み ---
# assets/config/*.json は「開発側が決める値」であり、ユーザーが編集する対象では
# ないため、配布物に置かず実行ファイルへ焼き込む。リポジトリ内のJSONは正典として
# 残す(手編集とdiffの可読性を保つ)。改ざん防止ではなく難読化が目的。
set(RS_EMBEDDED_CONFIG_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${RS_EMBEDDED_CONFIG_DIR}")

# JSONを1バイトずつの16進配列にしたヘッダーを生成する。外部ツール不要。
# 生成はconfigureではなくビルド時のルール(add_custom_command)で行う。JSONを編集しても
# 再configureは走らず、変えたJSONのヘッダーだけが作り直される。
set(RS_EMBEDDED_HEADERS "")
function(rs_embed_config_json jsonPath symbolName outHeaderName)
    set(outPath "${RS_EMBEDDED_CONFIG_DIR}/${outHeaderName}")
    add_custom_command(
        OUTPUT "${outPath}"
        COMMAND ${CMAKE_COMMAND}
            -DJSON_PATH=${jsonPath}
            -DSYMBOL_NAME=${symbolName}
            -DOUT_PATH=${outPath}
            -P "${CMAKE_SOURCE_DIR}/cmake/EmbedJson.cmake"
        DEPENDS "${jsonPath}" "${CMAKE_SOURCE_DIR}/cmake/EmbedJson.cmake"
        COMMENT "埋め込みヘッダー生成: ${outHeaderName}"
        VERBATIM)
    set(RS_EMBEDDED_HEADERS ${RS_EMBEDDED_HEADERS} "${outPath}" PARENT_SCOPE)
endfunction()

rs_embed_config_json(
    "${CMAKE_SOURCE_DIR}/assets/config/browser_tuning.json"
    kBrowserTuningJson
    EmbeddedBrowserTuning.h)
rs_embed_config_json(
    "${CMAKE_SOURCE_DIR}/assets/config/environment_presets.json"
    kEnvironmentPresetsJson
    EmbeddedEnvironmentPresets.h)

rs_embed_config_json(
    "${CMAKE_SOURCE_DIR}/assets/config/sim_tuning.json"
    kSimTuningJson
    EmbeddedSimTuning.h)
rs_embed_config_json(
    "${CMAKE_SOURCE_DIR}/assets/config/render_tuning.json"
    kRenderTuningJson
    EmbeddedRenderTuning.h)
rs_embed_config_json(
    "${CMAKE_SOURCE_DIR}/assets/config/audio_tuning.json"
    kAudioTuningJson
    EmbeddedAudioTuning.h)
rs_embed_config_json(
    "${CMAKE_SOURCE_DIR}/assets/config/particle_tuning.json"
    kParticleTuningJson
    EmbeddedParticleTuning.h)
rs_embed_config_json(
    "${CMAKE_SOURCE_DIR}/assets/config/particle_render_tuning.json"
    kParticleRenderTuningJson
    EmbeddedParticleRenderTuning.h)
rs_embed_config_json(
    "${CMAKE_SOURCE_DIR}/assets/config/camera_tuning.json"
    kCameraTuningJson
    EmbeddedCameraTuning.h)
rs_embed_config_json(
    "${CMAKE_SOURCE_DIR}/assets/config/placement_height_tuning.json"
    kPlacementHeightTuningJson
    EmbeddedPlacementHeightTuning.h)
rs_embed_config_json(
    "${CMAKE_SOURCE_DIR}/assets/config/train_info_card_tuning.json"
    kTrainInfoCardTuningJson
    EmbeddedTrainInfoCardTuning.h)
rs_embed_config_json(
    "${CMAKE_SOURCE_DIR}/assets/config/rail_label_hover_tuning.json"
    kRailLabelHoverTuningJson
    EmbeddedRailLabelHoverTuning.h)
rs_embed_config_json(
    "${CMAKE_SOURCE_DIR}/assets/config/toast_tuning.json"
    kToastTuningJson
    EmbeddedToastTuning.h)
rs_embed_config_json(
    "${CMAKE_SOURCE_DIR}/assets/config/render_quality_presets.json"
    kRenderQualityPresetsJson
    EmbeddedRenderQualityPresets.h)
rs_embed_config_json(
    "${CMAKE_SOURCE_DIR}/assets/config/time_control_tuning.json"
    kTimeControlTuningJson
    EmbeddedTimeControlTuning.h)
rs_embed_config_json(
    "${CMAKE_SOURCE_DIR}/assets/config/camera_screen_steps.json"
    kCameraScreenStepsJson
    EmbeddedCameraScreenSteps.h)
rs_embed_config_json(
    "${CMAKE_SOURCE_DIR}/assets/config/editor_geometry_tuning.json"
    kEditorGeometryTuningJson
    EmbeddedEditorGeometryTuning.h)
rs_embed_config_json(
    "${CMAKE_SOURCE_DIR}/assets/config/track_plugin_placement.json"
    kTrackPluginPlacementJson
    EmbeddedTrackPluginPlacement.h)
rs_embed_config_json(
    "${CMAKE_SOURCE_DIR}/assets/config/overlay2d.json"
    kOverlay2DJson
    EmbeddedOverlay2D.h)
rs_embed_config_json(
    "${CMAKE_SOURCE_DIR}/assets/config/overlay_line_tuning.json"
    kOverlayLineTuningJson
    EmbeddedOverlayLineTuning.h)
rs_embed_config_json(
    "${CMAKE_SOURCE_DIR}/assets/config/overlay_colors.json"
    kOverlayColorsJson
    EmbeddedOverlayColors.h)
rs_embed_config_json(
    "${CMAKE_SOURCE_DIR}/assets/locale/strings.json"
    kLocaleStringsJson
    EmbeddedLocaleStrings.h)
rs_embed_config_json(
    "${CMAKE_SOURCE_DIR}/assets/config/ui_theme.json"
    kUiThemeJson
    EmbeddedUiTheme.h)
rs_embed_config_json(
    "${CMAKE_SOURCE_DIR}/assets/config/update_check.json"
    kUpdateCheckJson
    EmbeddedUpdateCheck.h)

# M1 で追加した共通コード（パーサ/エンコーディング/描画）を静的ライブラリに
add_library(RailsimPlusCore STATIC
    src/app/update/UpdateChecker.cpp
    src/platform/net/HttpGet.cpp
    src/core/DebugDraw.cpp
    src/render/overlay2d/OverlayGeometryBuilder.cpp
    src/render/overlay2d/OverlayUtf8.cpp
    src/platform/text/Encoding.cpp
    src/platform/text/EncodingWin32.cpp
    src/platform/EnvironmentVariable.cpp
    src/platform/file/AssetPath.cpp
    src/platform/file/ExecutableDirectory.cpp
    src/platform/file/AtomicFileWrite.cpp
    src/platform/file/FileRead.cpp
    src/platform/file/StableFileWatcher.cpp
    src/platform/file/UserDataDirectory.cpp
    src/platform/file/ShellPickerDialog.cpp
    src/app/entry/RenderTuningHotReload.cpp
    src/app/locale/Localization.cpp
    src/app/settings/UserSettings.cpp
    src/app/settings/RuntimeSettingOverrides.cpp
    src/plugins/schema/parser/Lexer.cpp
    src/plugins/schema/parser/Parser.cpp
    src/plugins/schema/CustomizerParsers.cpp
    src/plugins/schema/CustomizerWindParsers.cpp
    src/plugins/schema/CustomizerTrackingParsers.cpp
    src/plugins/schema/SchemaProfile.cpp
    src/plugins/schema/PluginSchema.cpp
    src/plugins/schema/EnvSchema.cpp
    src/plugins/schema/LensFlareSchema.cpp
    src/plugins/schema/SchemaValueRead.cpp
    src/plugins/schema/EnvLighting.cpp
    src/plugins/schema/SchemaPlatform.cpp
    src/plugins/schema/PluginDefinitionCache.cpp
    src/plugins/schema/sidecar/PluginSidecarParsing.cpp
    src/plugins/schema/sidecar/SwitchAssignmentResolve.cpp
    src/plugins/schema/sidecar/StationSidecar.cpp
    src/plugins/schema/sidecar/TrainSidecar.cpp
    src/plugins/layout/Rs2Schema.cpp
    src/plugins/layout/Rs2RailSchema.cpp
    src/plugins/layout/Rs2DiaSchema.cpp
    src/plugins/layout/Rs2DiaImport.cpp
    src/plugins/layout/Rs2Import.cpp
    src/plugins/layout/Rs2EdgeBuild.cpp
    src/world/alignment/TrackGraphIntegrity.cpp
    src/world/alignment/TrackGraphReferences.cpp
    src/plugins/layout/Rs2RailBlockImport.cpp
    src/plugins/layout/Rs2NodeBuild.cpp
    src/plugins/layout/Rs2PlatformParent.cpp
    src/plugins/layout/Rs2WarpImport.cpp
    src/plugins/layout/Rs2CurveFit.cpp
    src/plugins/layout/RailwayPluginSetSchema.cpp
    src/plugins/layout/RailwayPluginSetWriter.cpp
    src/plugins/layout/TrainGroupTemplateSchema.cpp
    src/plugins/layout/TrainGroupTemplateWriter.cpp
    src/plugins/layout/SceneryTemplateSchema.cpp
    src/plugins/layout/SceneryTemplateWriter.cpp
    src/plugins/layout/TemplateWriteText.cpp
    src/plugins/layout/PluginTreeSchema.cpp
    src/plugins/schema/SchemaPartExtract.cpp
    src/plugins/schema/SchemaWalk.cpp
    src/plugins/schema/SchemaSwitchRefs.cpp
    src/plugins/schema/TextureStep.cpp
    src/plugins/schema/SchemaParts.cpp
    src/plugins/schema/SchemaInfoBlocks.cpp
    src/plugins/schema/SchemaEffectors.cpp
    src/plugins/schema/SchemaPlacement.cpp
    src/plugins/schema/ZyLinkageSolve.cpp
    src/plugins/schema/CamDistBands.cpp
    src/plugins/schema/SwitchExpr.cpp
    src/plugins/schema/SwitchExprAst.cpp
    src/plugins/schema/SwitchExprLexer.cpp
    src/plugins/schema/SwitchExprParser.cpp
    src/plugins/load/ModelMeshHelpers.cpp
    src/plugins/load/PluginLoader.cpp
    src/plugins/load/PluginDefinitionLookup.cpp
    src/plugins/load/PluginLoadProfile.cpp
    src/plugins/load/PluginPartModels.cpp
    src/plugins/load/PluginModelVariant.cpp
    src/plugins/load/PluginPaths.cpp
    src/plugins/load/ProfileMaterial.cpp
    src/plugins/load/PluginHeaderRead.cpp
    src/plugins/load/EnvPluginLoad.cpp
    src/plugins/load/PierMeshBuilder.cpp
    src/plugins/load/PierShaftMeshBuilder.cpp
    src/plugins/load/RailMeshBuilder.cpp
    src/plugins/load/LineWireMeshBuilder.cpp
    src/plugins/load/LineWireGeometry.cpp
    src/plugins/load/runtime/TrackingMoveSolve.cpp
    src/plugins/load/StretchZyMeshBuilder.h
    src/plugins/load/StretchZyMeshBuilder.cpp
    src/plugins/load/StretchZyMeshAssembly.h
    src/plugins/load/StretchZyMeshAssembly.cpp
    src/plugins/load/runtime/StretchZyRuntime.h
    src/plugins/load/runtime/StretchZyRuntime.cpp
    src/plugins/load/LineHangerPlacement.cpp
    src/plugins/load/runtime/MoverRuntime.cpp
    src/plugins/load/runtime/EaseTable.cpp
    src/plugins/load/runtime/DynamicJointPlacement.cpp
    src/plugins/load/runtime/MaterialRuntime.cpp
    src/plugins/load/runtime/AnimationRuntime.cpp
    src/plugins/load/runtime/LabeledFrameResolver.cpp
    src/plugins/load/runtime/PlatformParentPose.cpp
    src/plugins/load/runtime/SoundEffectAttachment.cpp
    src/plugins/load/runtime/SoundEffectWorldTracker.cpp
    src/plugins/load/runtime/HeadlightAttachment.cpp
    src/plugins/load/runtime/HeadlightWorldTracker.cpp
    src/plugins/load/runtime/AttachmentWorldPose.cpp
    src/plugins/load/runtime/ParticleAttachment.cpp
    src/plugins/load/runtime/ParticleWorldTracker.cpp
    src/plugins/load/TrainAssembly.cpp
    src/plugins/load/TrainAssemblyHeads.cpp
    src/render/mesh/MeshAssembler.cpp
    src/render/resource/TextureLoader.cpp
    src/render/resource/EnvTextureLoader.cpp
    src/render/resource/RenderTuning.cpp
    src/render/pipeline/MeshPipeline.cpp
    src/render/pipeline/MeshDynamicBuffers.cpp
    src/render/pipeline/MeshGeometry.cpp
    src/render/pipeline/MeshConstantBuffer.cpp
    src/render/rhi/RetirementQueue.cpp
    src/render/rhi/CommandList.cpp
    src/render/pipeline/ViewConstantsBuffer.cpp
    src/render/pipeline/PartWorldHistory.cpp
    src/render/pipeline/MeshTransparency.cpp
    src/render/pipeline/MeshMaterialSet.cpp
    src/render/pipeline/MeshPipelineSharedState.cpp
    src/render/pipeline/MeshShaderCompile.cpp
    src/render/pipeline/MeshRootSignature.cpp
    src/render/pipeline/MeshPipelinePsoOpaque.cpp
    src/render/pipeline/MeshPipelinePsoMultiPart.cpp
    src/render/pipeline/MeshPipelinePsoTransparent.cpp
    src/render/pipeline/MeshPipelineRecord.cpp
    src/render/pipeline/MeshPipelineConstantsWrite.cpp
    src/render/pipeline/MeshFrameRecord.cpp
    src/render/pipeline/MeshPipelineRecordShadow.cpp
    src/render/pipeline/MeshPipelineRecordPrepass.cpp
    src/render/pipeline/MeshPipelineRecordVelocity.cpp
    src/render/pipeline/MeshPipelinePsoShadow.cpp
    src/render/pipeline/MeshMaterialRuntime.cpp
    src/render/pipeline/MeshTexturePool.cpp
    src/render/pipeline/EnvFlag.cpp
    src/render/pipeline/SubmeshAlphaClassify.cpp
    src/render/pipeline/MeshUpload.cpp
    src/render/pipeline/MeshPipelineClassify.cpp
    src/render/instancing/BatchResidencyEviction.cpp
    src/render/instancing/InstanceBatchManager.cpp
    src/render/instancing/TrainInstanceBatch.cpp
    src/render/instancing/TrainInstanceBatchManager.cpp
    src/app/diagnostics/TrainInstanceSmokeTest.cpp
    src/render/wboit/WboitPass.cpp
    src/render/sky/AerialPerspectiveDebug.cpp
    src/render/sky/AerialPerspectiveLut.cpp
    src/render/sky/AtmosphereGpuUtil.cpp
    src/render/sky/AtmosphereLuts.cpp
    src/render/sky/AtmosphereMath.cpp
    src/render/sky/BrdfLut.cpp
    src/render/sky/EnvCubeSet.cpp
    src/render/sky/EnvPhotoStats.cpp
    src/render/sky/CubeTarget.cpp
    src/render/sky/ExposureTarget.cpp
    src/render/sky/EnvironmentState.cpp
    src/render/sky/HeightFogMath.cpp
    src/render/sky/IblGenerator.cpp
    src/render/sky/LandscapeContrast.cpp
    src/render/sky/LandscapeCube.cpp
    src/render/sky/LandscapeCubeBake.cpp
    src/render/sky/LandscapeMesh.cpp
    src/render/sky/SkyBodyMath.cpp
    src/render/sky/SkyBodyModel.cpp
    src/render/sky/SkyBodyPass.cpp
    src/render/sky/SkyPass.cpp
    src/render/postprocess/SceneHdrTarget.cpp
    src/render/postprocess/BloomPass.cpp
    src/render/postprocess/FxaaPass.cpp
    src/render/postprocess/PrepassDepthTarget.cpp
    src/render/postprocess/VelocityDebug.cpp
    src/render/postprocess/TaaPass.cpp
    src/render/postprocess/MotionBlurPass.cpp
    src/render/postprocess/DepthOfFieldPass.cpp
    src/render/postprocess/FullscreenPassPipelines.cpp
    src/render/postprocess/DepthProbe.cpp
    src/render/postprocess/LuminanceMeterPass.cpp
    src/render/postprocess/AmbientOcclusionPass.cpp
    src/render/postprocess/PostProcessChain.cpp
    src/render/postprocess/PostProcessPlan.cpp
    src/render/effects/LensFlareMath.cpp
    src/render/effects/VolumetricFogDebug.cpp
    src/render/effects/VolumetricFogPass.cpp
    src/render/effects/VolumetricFogSkyPass.cpp
    src/render/effects/LensFlarePass.cpp
    src/render/effects/LensFlarePassPipeline.cpp
    src/render/particle/ParticleBatchBuilder.cpp
    src/render/particle/ParticlePass.cpp
    src/render/particle/ParticlePassPipeline.cpp
    src/render/particle/ParticleRenderTuning.cpp
    src/render/effects/SunFlareFrame.cpp
    src/render/effects/HeadlightFlare.cpp
    src/render/shadow/CascadeFit.cpp
    src/render/shadow/ShadowUpdateSchedule.cpp
    src/render/shadow/ShadowDistance.cpp
    src/render/shadow/ShadowMap.cpp
    src/render/shadow/ShadowPass.cpp
    src/render/postprocess/TonemapMath.cpp
    src/render/postprocess/TonemapPass.cpp
    src/render/thumbnail/ThumbnailEnvironment.cpp
    src/render/thumbnail/ThumbnailRenderer.cpp
    src/render/thumbnail/ThumbnailResources.cpp
    src/render/resource/AsyncLoader.cpp
    src/platform/thread/ParallelFor.cpp
    src/render/resource/GeometryManager.cpp
    src/render/resource/SrvRangeAllocator.cpp
    src/render/resource/ModelManager.cpp
    src/render/resource/TextureManager.cpp
    src/render/resource/ShaderCache.cpp
    src/render/resource/DxcShaderCompiler.cpp
    src/render/resource/ComputePipeline.cpp
    src/render/resource/ComputeSelfTest.cpp
    src/render/resource/VolumeTexture.cpp
    src/render/resource/ShaderCompileFxc.cpp
    src/render/resource/TextureUpload.cpp
    src/render/resource/UploadHeapBlockSlab.cpp
    src/render/resource/UploadBufferPool.cpp
    src/render/resource/MaterialOverwrite.cpp
    src/render/resource/XLoader.cpp
    src/world/alignment/Alignment.cpp
    src/world/alignment/AlignmentSegmentSampler.cpp
    src/world/alignment/AlignmentGraphRebuild.cpp
    src/world/alignment/EdgeEndMismatch.cpp
    src/world/alignment/EdgeShapeBuild.cpp
    src/world/alignment/EdgeShapeGenerator.cpp
    src/world/alignment/AutoCant.cpp
    src/world/alignment/CantProfile.cpp
    src/world/alignment/CantControlPoints.cpp
    src/world/alignment/TrackTraversal.cpp
    src/world/alignment/RigidPose.cpp
    src/world/alignment/TrackForwardScan.cpp
    src/world/alignment/WarpForwardScan.cpp
    src/world/alignment/PlatformChain.cpp
    src/world/alignment/PlatformRoutine.cpp
    src/world/alignment/PointRoutine.cpp
    src/world/alignment/NumberedName.cpp
    src/world/alignment/PointName.cpp
    src/world/alignment/PointHandLinks.cpp
    src/world/alignment/PointRoutineResolve.cpp
    src/world/alignment/GuidJson.cpp
    src/world/alignment/TrackGraphSerialize.cpp
    src/world/alignment/TrackGraphWholeSerialize.cpp
    src/world/alignment/PlatformStopInfoSerialize.cpp
    src/world/alignment/PlatformRoutineSerialize.cpp
    src/world/alignment/PlatformRegistry.cpp
    src/world/alignment/FormationRoutineResolve.cpp
    src/world/alignment/BlockRegistry.cpp
    src/world/alignment/BlockSpeedLimit.cpp
    src/world/alignment/BlockConnectivity.cpp
    src/world/alignment/TravelDirectionFollow.cpp
    src/world/alignment/SignalSpeedColor.cpp
    src/world/alignment/SpeedSignalRegistry.cpp
    src/world/alignment/PointClaims.cpp
    src/world/alignment/RailLinkEdit.cpp
    src/world/alignment/RailLinkOverlay.cpp
    src/world/alignment/SingleTrackSection.cpp
    src/world/alignment/SpeedSignalForwardScan.cpp
    src/world/alignment/SplineSlice.cpp
    src/world/alignment/EdgeSplit.cpp
    src/world/alignment/EdgeMerge.cpp
    src/world/alignment/TrackQuery.cpp
    src/world/alignment/WarpLinkTable.cpp
    src/world/alignment/WarpLinkTableSerialize.cpp
    src/world/alignment/WarpTraversal.cpp
    src/world/alignment/TrackChain.cpp
    src/world/time/CalendarUtil.cpp
    src/world/time/GameClock.cpp
    src/world/time/GameClockSerialize.cpp
    src/world/time/SceneSharedSwitches.cpp
    src/world/time/SolarPosition.cpp
    src/world/time/SolarGeometry.cpp
    src/world/time/MoonPosition.cpp
    src/world/time/EnvironmentPresets.cpp
    src/world/scene/ScenePlacement.cpp
    src/world/scene/SceneRegistry.cpp
    src/world/scene/TrainTagRegistry.cpp
    src/world/scene/TrainTagRegistrySerialize.cpp
    src/world/scene/TrainRunInfoRegistry.cpp
    src/world/scene/TrainRunInfoRegistrySerialize.cpp
    src/sim/TrainVehicle.cpp
    src/sim/WheelJointTrigger.cpp
    src/sim/TrainOccupiedPath.cpp
    src/sim/TrainLeadingEnd.cpp
    src/sim/TrainKinematics.cpp
    src/sim/TrainRuntime.cpp
    src/sim/TrainRuntimeSeek.cpp
    src/sim/TrainRuntimeTick.cpp
    src/sim/TrainRuntimeSnapshot.cpp
    src/sim/DiaRuntime.cpp
    src/sim/SimTuning.cpp
    src/sim/PlatformApproach.cpp
    src/sim/OccupiedNodes.cpp
    src/sim/PlatformPassThrough.cpp
    src/sim/BlockOccupancy.cpp
    src/sim/RouteReservation.cpp
    src/sim/SignalAspectRuntime.cpp
    src/sim/audio/AudioBufferCache.cpp
    src/sim/audio/AudioBufferFactory.cpp
    src/sim/audio/AudioDeviceContext.cpp
    src/sim/audio/AudioEngine.cpp
    src/sim/audio/AudioSourceManager.cpp
    src/sim/audio/PlacedSoundSourcePool.cpp
    src/sim/audio/AudioTuning.cpp
    src/sim/audio/WavDecoder.cpp
    src/sim/audio/WheelSoundVoicePool.cpp
    src/sim/particle/ParticleEmitterParams.cpp
    src/sim/particle/ParticleEmission.cpp
    src/sim/particle/ParticlePool.cpp
    src/sim/particle/ParticleStep.cpp
    src/sim/particle/ParticleSystem.cpp
    src/sim/particle/ParticleNoise.cpp
    src/sim/particle/ParticleRandom.cpp
    src/sim/particle/ParticleTuning.cpp
    src/editor/tool/Command.cpp
    src/editor/tool/CommandStack.cpp
    src/editor/tool/WarpLinkCommand.cpp
    src/editor/tool/SceneEntityCommand.cpp
    src/editor/tool/EdgeCommand.cpp
    src/editor/tool/PlatformCommand.cpp
    src/editor/tool/PointDiaCommand.cpp
    src/editor/tool/BlockCommand.cpp
    src/editor/tool/BlockSignalCleanupCommand.cpp
    src/editor/tool/SpeedSignalCommand.cpp
    src/editor/tool/EdgeSplitMergeCommand.cpp
    src/editor/tool/EdgeMergeCommand.cpp
    src/editor/tool/EditorCenterlineSplit.cpp
    src/editor/tool/EditorCenterlineMerge.cpp
    src/editor/tool/PlacementOverlayCommand.cpp
    src/editor/tool/PlacementRescaleCommand.cpp
    src/editor/tool/PlacementEditCommand.cpp
    src/editor/tool/WarpLinkAuthoring.cpp
    src/editor/tool/Editor.cpp
    src/editor/tool/EditorPlacementInput.cpp
    src/editor/tool/PlacementSnap.cpp
    src/editor/tool/TrackLayingCommit.cpp
    src/editor/tool/TrackLayingSession.cpp
    src/editor/tool/TrackLayingSessionSolve.cpp
    src/editor/tool/TrackLayingSessionPreview.cpp
    src/editor/tool/TrackLayingSessionCommit.cpp
    src/editor/tool/TrackLayingSessionCommitMulti.cpp
    src/editor/tool/EditorPicking.cpp
    src/editor/tool/EditorEditOps.cpp
    src/editor/tool/EditorSceneEntityOps.cpp
    src/editor/tool/EditorStationAnchor.cpp
    src/editor/tool/EditorPlaceTravel.cpp
    src/editor/tool/EditorPlatformRailGen.cpp
    src/editor/tool/EditorNodeDrag.cpp
    src/editor/tool/NodeDragPlan.cpp
    src/editor/tool/NodeDragCenterline.cpp
    src/editor/tool/EdgeSelection.cpp
    src/editor/tool/EditorEdgeProperties.cpp
    src/editor/tool/EditorSectionEdit.cpp
    src/editor/tool/EditorPlacementOverlayOps.cpp
    src/world/geometry/BiarcSolver.cpp
    src/world/geometry/EditorGeometryTuning.cpp
    src/world/geometry/TangentOffsetGeometry.cpp
    src/world/geometry/ThreePointArcSolver.cpp
    src/app/plugin_session/PluginSelector.cpp
    src/app/plugin_session/FormationCatalog.cpp
    src/app/diagnostics/CompatReport.cpp
    src/app/plugin_browser/BrowserTuning.cpp
    src/app/plugin_browser/PluginCatalog.cpp
    src/app/plugin_browser/PluginCatalogScanner.cpp
    src/app/plugin_browser/PluginBrowserState.cpp
    src/app/plugin_browser/PluginTreeIndex.cpp
    src/app/plugin_browser/PluginTreeConfigSource.cpp
    src/app/plugin_browser/BrowserCardSizeCatalog.cpp
    src/app/plugin_browser/BrowserRowHeights.cpp
    src/app/plugin_browser/PluginSetMembership.cpp
    src/app/plugin_browser/PluginThumbnailRailMesh.cpp
    src/app/plugin_browser/IconRectCrop.cpp
    src/app/plugin_browser/PluginImageStore.cpp
    src/app/plugin_browser/PluginImageSource.cpp
    src/app/plugin_browser/ThumbnailViewCamera.cpp
    src/app/plugin_session/PluginSessionRuntimeMetadata.cpp
    src/app/plugin_session/RailTieHeights.cpp
    src/app/plugin_session/RailSoundLookup.cpp
    src/app/plugin_session/Rs2SceneApplier.cpp
    src/app/plugin_session/Rs2StationParentLookup.cpp
    src/app/plugin_session/DefaultPluginCatalog.cpp
    src/app/plugin_session/Rs2PlacementNormalize.cpp
    src/app/plugin_session/LayoutSession.cpp
    src/app/plugin_session/LayoutSessionSceneDelete.cpp
    src/app/plugin_session/MissingPluginCatalog.cpp
    src/app/plugin_session/PluginReferenceWalk.cpp
    src/app/plugin_session/PluginReferenceScenes.cpp
    src/app/plugin_session/PluginReferenceReplace.cpp
    src/app/plugin_session/PluginReferenceDelete.cpp
    src/app/plugin_session/ReplacePluginReferencesCommand.cpp
    src/app/plugin_session/DeletePluginReferencesCommand.cpp
    src/app/plugin_session/ActiveEnv.cpp
    src/app/plugin_session/SceneChoiceCatalog.cpp
    src/app/plugin_session/EnvCubeController.cpp
    src/app/plugin_session/SunFlareSetup.cpp
    src/app/plugin_session/SunFlareController.cpp
    src/app/plugin_session/FlareTextureSlots.cpp
    src/app/plugin_session/TextureSlotRegistry.cpp
    src/app/plugin_session/FlareTextureUpload.cpp
    src/app/plugin_session/LensFlareStyleConvert.cpp
    src/app/plugin_session/PluginSkyLighting.cpp
    src/app/plugin_session/Rs2LayoutDecode.cpp
    src/app/camera/CameraControl.cpp
    src/app/camera/CameraTuning.cpp
    src/app/camera/CabinViewCamera.cpp
    src/app/camera/PhotoCamera.cpp
    src/app/camera/PhotoAutoIso.cpp
    src/app/input/InputBindings.cpp
    src/app/input/InputBindingOverrides.cpp
    src/app/input/InputSource.cpp
    src/app/entry/ScreenshotFileName.cpp
    src/app/workspace/TrainInfoCardTuning.cpp
    src/app/workspace/DiaCardCandidates.cpp
    src/app/workspace/RailLabelHover.cpp
    src/app/workspace/RailLabelHoverTuning.cpp
    src/app/workspace/RailLabelPresentation.cpp
    src/app/workspace/WorkspaceShell.cpp
    src/app/workspace/HelpWindowPresentation.cpp
    src/app/workspace/GameSettingsPresentation.cpp
    src/app/workspace/InputSettingsPresentation.cpp
    src/app/workspace/HelpWindowPlaceholder.cpp
    src/app/workspace/DiaPanelPresentation.cpp
    src/app/workspace/DiaPanelFormat.cpp
    src/app/workspace/ScenePanelPresentation.cpp
    src/app/workspace/TemplateSavePresentation.cpp
    src/app/workspace/CameraFinder.cpp
    src/app/workspace/CameraPhotoPreview.cpp
    src/app/workspace/CameraSevenSegment.cpp
    src/app/workspace/CameraScreenSteps.cpp
    src/app/workspace/CameraScreenPresentation.cpp
    src/app/workspace/CameraScreenPlaceholder.cpp
    src/app/workspace/TimeControlPresentation.cpp
    src/app/workspace/TimeControlTuning.cpp
    src/app/workspace/LayoutDocumentFlow.cpp
    src/app/plugin_session/LayoutFileCatalog.cpp
    src/app/plugin_session/LayoutThumbnail.cpp
    src/app/plugin_session/RslThumbnailRead.cpp
    src/platform/text/Base64.cpp
    src/app/camera/FlyCameraSerialize.cpp
    src/app/plugin_session/RailwayPluginSetApplier.cpp
    src/app/plugin_session/RailwayPluginSetResolve.cpp
    src/app/plugin_session/TrainGroupTemplateApplier.cpp
    src/app/plugin_session/TrainGroupTemplateParse.cpp
    src/app/plugin_session/TemplateSaver.cpp
    src/app/plugin_session/FormationBuilder.cpp
    src/app/plugin_session/Rs2TrainApplier.cpp
    src/app/plugin_session/Rs2TrainRunStateRestore.cpp
    src/app/scene/TrainPlacement.cpp
    src/app/scene/TrainPreviewModel.cpp
    src/app/scene/ScenePicking.cpp
    src/app/scene/TrainPickBox.cpp
    src/app/scene/TrackPluginPlacement.cpp
    src/app/scene/TrackPluginPlacementTuning.cpp
    src/app/scene/TrackPluginRunWalk.cpp
    src/app/scene/PlatformParentDriver.cpp
    src/app/scene/StationRailEffects.cpp
    src/app/scene/ManualPlacementOverlayApply.cpp
    src/app/scene/StructLinePoles.cpp
    src/app/scene/StructLinePoleTracker.cpp
    src/app/scene/PlacementMaterialize.cpp
    src/world/scene/MaterializeSnapshot.cpp
    src/world/scene/ManualPlacementOverlaySerialize.cpp
    src/world/scene/SceneEntitySerialize.cpp
    src/world/scene/CarEntitySerialize.cpp
    src/app/action/LayoutSavePath.cpp
    src/app/action/TrackPluginSetAction.cpp
    src/app/action/MissingPluginAction.cpp
    src/app/action/MissingPluginRefreshGate.cpp
    src/app/action/MissingPluginViewModel.cpp
    src/app/action/ToastQueue.cpp
    src/app/action/NotImplementedNotice.cpp
    src/app/action/TemplateAction.cpp
    src/app/action/TemplateDefBuilder.cpp
    src/app/action/SceneAction.cpp
    src/app/action/HelpAction.cpp
    src/app/action/CameraScreenAction.cpp
    src/app/action/TimeControlAction.cpp
    src/app/action/TimeControlDateTime.cpp
    src/app/action/TimeControlViewModel.cpp
    src/app/action/RenderQualityPreset.cpp
    src/app/action/GameSettingsScreenModel.cpp
    src/app/action/GameSettingsScreenModelPlaceholder.cpp
    src/app/action/InputBindingAction.cpp
    src/app/action/InputSettingsRows.cpp
    src/app/action/RenderSettingsRows.cpp
    src/app/action/TrackPluginSetText.cpp
    src/app/action/SceneryPlacementAction.cpp
    src/app/action/PlacementHeightAction.cpp
    src/app/action/PlacementHeightTuning.cpp
    src/app/action/KeyRepeatStepper.cpp
    src/app/action/SceneryYawAction.cpp
    src/app/action/PluginFavoriteAction.cpp
    src/app/action/PluginFolderMoveAction.cpp
    src/app/audio/UiSoundPlayer.cpp
    src/app/action/PluginBrowserCardSizeAction.cpp
    src/app/action/PluginRootAction.cpp
    src/app/action/LayoutActionResult.cpp
    # アプリ層のうちテストからも使うもの。アプリとテストで二重にコンパイルしないよう、ここに置く
    src/app/plugin_browser/LayingSetPreviewStore.cpp
    src/app/plugin_browser/TemplateImageStore.cpp
    src/app/plugin_browser/PluginThumbnailRecipe.cpp
    src/app/plugin_browser/TemplateThumbnailRecipe.cpp
    src/app/entry/DeviceLostShutdown.cpp
    src/app/gpu/InitSteps.cpp
    src/app/gpu/SceneSlotCulling.cpp
    src/core/Frustum.cpp
    src/app/ui/EditorViewportState.cpp
    src/app/action/EdgePick.cpp
    src/app/ui/OverlayColorTuning.cpp
    src/app/plugin_session/SceneMaterialOverrides.cpp
    src/app/plugin_session/PluginSession.cpp
    src/app/plugin_session/PluginSwitchTable.cpp
    src/app/plugin_session/SharedStateAxes.cpp
    src/app/plugin_session/RslJsonWriter.cpp
    src/app/plugin_session/RslJsonFieldRounding.cpp
    src/app/plugin_session/RslLayoutSerialize.cpp
    src/app/plugin_session/LayoutDocumentState.cpp
    src/app/plugin_session/AutoRecovery.cpp
    src/app/plugin_session/RslLayoutLoad.cpp
    src/app/plugin_session/LayoutLoad.cpp
    src/app/plugin_session/PluginRootResolver.cpp
    src/app/plugin_session/RslSceneCacheSerialize.cpp
    src/app/plugin_session/RslRuntimeState.cpp
    src/app/action/LayoutPersistenceAction.cpp
    src/app/action/LayoutPersistenceActionScene.cpp
    src/app/action/ScenePanelViewModel.cpp
    src/app/action/PluginInfoAction.cpp
    src/app/action/SwitchList.cpp
    src/app/action/EdgeSectionEditAction.cpp
    src/app/action/EdgePropertyAction.cpp
    src/app/action/StationDiaAction.cpp
    src/app/action/PlatformRoutineAction.cpp
    src/app/action/PlatformRoutineLayerAction.cpp
    src/app/action/PlatformRoutineViewModel.cpp
    src/app/action/PointRoutineAction.cpp
    src/app/action/PointOpeningAction.cpp
    src/app/action/PointRoutineLayerAction.cpp
    src/app/action/DiaPanelPresentationBuilder.cpp
    src/app/action/DiaPanelBranchBuilder.cpp
    src/app/action/NodePropertyAction.cpp
    src/app/action/TrackLayingAction.cpp
    src/app/action/TrackLayingText.cpp
    src/app/action/TrackLayingPreviewColor.cpp
    src/app/action/ToolCancelAction.cpp
    src/app/action/SignalBlockAction.cpp
    src/app/action/SignalBlockSignalEdit.cpp
    src/app/action/SignalPluginRules.cpp
    src/app/action/GroupCommonSwitchSync.cpp
    src/app/plugin_session/PreloadTextureSet.cpp
    src/app/scene/ParsedPluginCache.cpp
    src/app/scene/SceneTrainGroupCollector.cpp
    src/app/scene/StationRunInfoDispatcher.cpp
    src/app/scene/SignalModelSet.cpp
    src/app/scene/TrainDoorSide.cpp
    src/app/scene/SceneStopConstraintPhase.cpp
    src/app/scene/SceneRouteReservationPhase.cpp
    src/app/scene/SceneCabinPivot.cpp
    src/app/scene/SceneSceneryPicker.cpp
    src/app/scene/ParticleRequestCollector.cpp
    src/app/scene/SceneSlotPool.cpp
    src/app/scene/SurfaceBoundsTracker.cpp
    src/app/scene/StaticMeshCache.cpp
    src/app/scene/StaticMeshVariantSet.cpp
    src/app/scene/StaticHeadlightRoster.cpp
    src/app/scene/StaticMeshVariantBake.cpp
    src/app/scene/StaticInstanceKey.cpp
    src/app/scene/CamDistBandAssigner.cpp
    src/app/scene/StaticBandedSceneryInstances.cpp
)
target_include_directories(RailsimPlusCore PUBLIC ${RS_EMBEDDED_CONFIG_DIR})
target_sources(RailsimPlusCore PRIVATE ${RS_EMBEDDED_HEADERS})
target_link_libraries(RailsimPlusCore PUBLIC d3d12 dxgi d3dcompiler windowscodecs dxcompiler ole32 uuid version winhttp shell32
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    magic_enum::magic_enum
    DirectXTK12
    DirectXTex
    DirectXMesh
    assimp::assimp
    OpenAL::OpenAL
    miniaudio
)
# RS_PROJECT_SOURCE_DIR はソースツリー相対のアセット解決(フォント・正典JSON等)で使う。
# 以前は RailsimPlus にしか定義しておらず、RailsimPlusCore 内の解決処理では常に "." に
# フォールバックしていたため PUBLIC で core 側へ移した(RailsimPlus/RailsimPlusTests へ伝播する)。
target_compile_definitions(RailsimPlusCore PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    RS_PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}" RS_APP_VERSION="${PROJECT_VERSION}"
    RAILSIM_DEV_ENV=$<BOOL:${RAILSIM_DEV_ENV}>)
if(MSVC)
    target_compile_options(RailsimPlusCore PRIVATE /W4 /utf-8)
endif()

add_executable(RailsimPlus
    src/app/entry/RailsimPlus.rc
    src/app/plugin_browser/PluginBrowserService.cpp
    src/app/plugin_browser/PluginImageWorker.cpp
    src/platform/font/UiFont.cpp
    src/app/entry/main.cpp
    src/app/entry/AppRuntime.cpp
    src/app/entry/AppRuntimeShutdown.cpp
    src/app/entry/FramePacer.cpp
    src/app/entry/OpenedPlugin.cpp
    src/app/entry/ViewerMode.cpp
    src/app/entry/LayoutResultReporter.cpp
    src/app/entry/AppRuntimeFrameUi.cpp
    src/app/entry/AppRuntimeFrameSim.cpp
    src/app/entry/AppRuntimeParticle.cpp
    src/app/entry/AppRuntimeFrameFov.cpp
    src/app/entry/AppRuntimeFramePhoto.cpp
    src/app/entry/AppRuntimeFrameRender.cpp
    src/app/entry/CameraOverride.cpp
    src/app/entry/ScreenshotCapture.cpp
    src/app/entry/CameraPhotoCapture.cpp
    src/app/entry/WorkspaceEditorBridge.cpp
    src/app/entry/LayoutDocumentController.cpp
    src/app/entry/LayoutDocumentRecoveryChoice.cpp
    src/app/entry/AppRuntimeSettingsInit.cpp
    src/app/entry/GpuInitFailureDialog.cpp
    src/app/entry/StartupFailureDialog.cpp
    src/app/gpu/App.cpp
    src/app/gpu/AppFramePasses.cpp
    src/app/gpu/GpuContext.cpp
    src/app/gpu/GpuDeviceCreate.cpp
    src/app/gpu/GpuContextDiagnostics.cpp
    src/app/gpu/GpuContextCapture.cpp
    src/app/gpu/GpuFrameTimer.cpp
    src/app/gpu/FrameRenderer.cpp
    src/app/gpu/FrameRendererFrame.cpp
    src/app/gpu/FrameRendererSceneSlots.cpp
    src/app/gpu/FrameRendererRecord.cpp
    src/app/gpu/FrameRendererShadow.cpp
    src/app/gpu/FrameRendererAmbientOcclusion.cpp
    src/app/gpu/FrameRendererVelocity.cpp
    src/app/gpu/FrameRendererTaa.cpp
    src/app/gpu/FrameRendererMotionBlur.cpp
    src/app/gpu/FrameRendererDepthOfField.cpp
    src/app/gpu/FrameRendererLuminanceMeter.cpp
    src/app/gpu/FrameRendererVolumetricFog.cpp
    src/app/gpu/FrameRendererLensFlare.cpp
    src/app/gpu/FrameRendererAerial.cpp
    src/app/rmlui/RmlUiRenderInterface.cpp
    src/app/rmlui/RmlUiSystemInterface.cpp
    src/app/rmlui/RmlUiIntegration.cpp
    src/app/rmlui/RmlUiIntegrationTheme.cpp
    src/app/rmlui/RmlUiIntegrationInput.cpp
    src/app/rmlui/RmlUiKeyboardRouter.cpp
    src/app/rmlui/RmlUiDropDown.cpp
    src/app/rmlui/RmlUiProtoView.cpp
    src/app/rmlui/RmlUiSegmented.cpp
    src/app/rmlui/RmlUiWindowClamp.cpp
    src/app/rmlui/RmlUiTheme.cpp
    src/app/rmlui/RmlUiWorkspaceView.cpp
    src/app/rmlui/RmlUiWorkspaceViewActions.cpp
    src/app/rmlui/RmlUiWorkspaceViewInput.cpp
    src/app/rmlui/RmlUiToastView.cpp
    src/app/rmlui/RmlUiConfirmDialog.cpp
    src/app/rmlui/RmlUiOutsideClickCloser.cpp
    src/app/rmlui/RmlUiClickSound.cpp
    src/app/rmlui/RmlUiFieldCommit.cpp
    src/app/rmlui/RmlUiContextMenu.cpp
    src/app/rmlui/RmlUiLayingEditorView.cpp
    src/app/rmlui/RmlUiEdgeEditView.cpp
    src/app/rmlui/RmlUiLayingSetView.cpp
    src/app/rmlui/RmlUiLayingFields.cpp
    src/app/rmlui/RmlUiPluginBrowserView.cpp
    src/app/rmlui/RmlUiPluginBrowserRefresh.cpp
    src/app/rmlui/RmlUiPluginBrowserPlacement.cpp
    src/app/rmlui/RmlUiBrowserSelected.cpp
    src/app/rmlui/RmlUiPluginBrowserImages.cpp
    src/app/rmlui/RmlUiMissingPluginsView.cpp
    src/app/rmlui/RmlUiGameSettingsView.cpp
    src/app/rmlui/RmlUiGameSettingsAbout.cpp
    src/app/entry/UpdateNotification.cpp
    src/app/rmlui/RmlUiGameSettingsTime.cpp
    src/app/rmlui/RmlUiGameSettingsEvents.cpp
    src/app/rmlui/RmlUiTrainTabView.cpp
    src/app/rmlui/RmlUiSignalPanelView.cpp
    src/app/action/SignalPanelAction.cpp
    src/app/action/SignalPanelViewModel.cpp
    src/app/workspace/SignalPanelPresentation.cpp
    src/app/rmlui/RmlUiInputSettingsView.cpp
    src/app/rmlui/RmlUiRenderSettingsView.cpp
    src/app/rmlui/RmlUiPluginRootsView.cpp
    src/app/rmlui/RmlUiLayoutFilesView.cpp
    src/app/rmlui/RmlUiRecoveryView.cpp
    src/app/rmlui/RmlUiLoadingView.cpp
    src/app/rmlui/RmlUiPluginSettingsView.cpp
    src/app/rmlui/RmlUiViewerView.cpp
    src/app/rmlui/RmlUiSceneryYawStepView.cpp
    src/app/rmlui/RmlUiFormationListView.cpp
    src/app/rmlui/RmlUiTemplateSaveView.cpp
    src/app/rmlui/RmlUiDiaPanelView.cpp
    src/app/rmlui/RmlUiDiaPanelModel.cpp
    src/app/rmlui/RmlUiScenePanelView.cpp
    src/app/rmlui/RmlUiHelpWindowView.cpp
    src/app/rmlui/RmlUiCameraScreenView.cpp
    src/app/rmlui/RmlUiCameraPhotoPreview.cpp
    src/app/rmlui/RmlUiTimeControlView.cpp
    src/app/rmlui/RmlUiFormationCarsView.cpp
    src/app/rmlui/RmlUiAnchoredArea.cpp
    src/app/rmlui/RmlUiAnchoredElementPlacement.cpp
    src/app/rmlui/RmlUiTrainInfoCardView.cpp
    src/app/rmlui/RmlUiCabinHudView.cpp
    src/app/rmlui/RmlUiDiaCardView.cpp
    src/app/rmlui/RmlUiInlineRename.cpp
    src/app/rmlui/RmlUiRailLabelView.cpp
    src/app/rmlui/RmlUiFormationImages.cpp
    src/app/rmlui/RmlUiUpdatePhase.cpp
    src/app/rmlui/RmlUiNumberParse.cpp
    src/app/rmlui/RmlUiBrowserGridMeasure.cpp
    src/app/rmlui/RmlUiBrowserCardSizer.cpp
    src/app/rmlui/RmlUiBrowserGridDriver.cpp
    src/app/rmlui/RmlUiBrowserHoverText.cpp
    src/app/rmlui/RmlUiBrowserFolder.cpp
    src/app/rmlui/RmlUiPluginBrowserModel.cpp
    src/app/gpu/FrameHighlightState.cpp
    src/app/gpu/PlacementPreviewState.cpp
    src/app/gpu/SceneSlotState.cpp
    src/render/overlay2d/Overlay2DPass.cpp
    src/render/overlay2d/Overlay2DPassSetup.cpp
    src/render/overlay2d/GlyphAtlas.cpp
    src/app/entry/AppCommandLine.cpp
    src/app/gpu/AppInit.cpp
    src/app/input/ImGuiInputBridge.cpp
    src/app/ui/EditorViewport.cpp
    src/app/ui/ViewportInput.cpp
    src/app/ui/ViewportInputModes.cpp
    src/app/ui/ViewportSceneryPlacementInput.cpp
    src/app/ui/ViewportScenerySelectionInput.cpp
    src/app/ui/EdgePickInput.cpp
    src/app/ui/NodeDragInput.cpp
    src/app/ui/PlacementDragInput.cpp
    src/app/ui/OverlayLineTuning.cpp
    src/app/ui/ViewportOverlay.cpp
    src/app/ui/ViewportOverlayGeometry.cpp
    src/app/ui/ViewportSceneEntityOverlay.cpp
    src/app/ui/SceneryTransformGizmo.cpp
    src/app/ui/ViewportPluginSettingsTarget.cpp
    src/app/ui/ViewportSectionOverlay.cpp
    src/app/ui/ViewportStationDia.cpp
    src/app/ui/ViewportStationDiaOverlay.cpp
    src/app/ui/ViewportSignalBlockInput.cpp
    src/app/ui/ViewportSignalBlockOverlay.cpp
    src/app/ui/ViewportRailMeshBuild.cpp
    src/app/ui/ViewportWireSpanBuild.cpp
    src/app/ui/WireConnectPoints.cpp
    src/app/ui/ViewportWireEditInput.cpp
    src/app/ui/ViewportWireEditOverlay.cpp
    src/app/ui/ViewportPlacementInstances.cpp
    src/app/ui/ViewportSceneryInstances.cpp
    src/app/ui/ViewportPierBatchPlan.cpp
    src/app/ui/ImGuiFontManager.cpp
    src/app/entry/MainWindow.cpp
    src/app/diagnostics/PluginTools.cpp
    src/app/diagnostics/CompatScan.cpp
    src/app/diagnostics/LayoutDigest.cpp
    src/app/diagnostics/Rs2Spike.cpp
    src/app/plugin_session/PluginRuntimeApplier.cpp
    src/app/ui/PluginInfoPanel.cpp
    src/app/ui/SwitchListWidget.cpp
    src/app/ui/ViewerSettingsPanel.cpp
    src/app/ui/PluginSelectorPanel.cpp
    src/app/ui/FormationPanel.cpp
    src/app/ui/LoadProgressOverlay.cpp
    src/app/ui/TrainPlacementInput.cpp
    src/app/ui/TrainPlacementHighlight.cpp
    src/app/ui/UiPanelHelpers.cpp
    src/app/ui/SceneEntitySwitchPanel.cpp
    src/app/plugin_session/ViewerState.cpp
    src/app/action/LayoutPersistenceActionProgress.cpp
    src/app/action/LayoutPersistenceActionSceneDelete.cpp
    src/app/action/ViewerSettingsAction.cpp
    src/app/action/GameSettingsAction.cpp
    src/app/action/RenderSettingsAction.cpp
    src/app/action/FormationAction.cpp
    src/app/action/DiaPanelAction.cpp
    src/app/action/DiaPanelRoutineAction.cpp
    src/app/action/DiaPanelBranchAction.cpp
    src/app/action/SceneEntitySwitchAction.cpp
    src/app/plugin_session/SessionFrameStep.cpp
    src/app/ui/ImGuiPathPicker.cpp
    src/app/ui/ModePanels.cpp
    src/app/diagnostics/SceneInstanceSmokeTest.cpp
    src/app/diagnostics/SceneResidencySmokeTest.cpp
    src/app/diagnostics/BackBufferCapturePng.cpp
    src/app/diagnostics/ThumbnailCapture.cpp
    src/app/diagnostics/HeadlessDevice.cpp
    src/app/diagnostics/TonemapCheck.cpp
    src/app/diagnostics/FrameProfiler.cpp
    src/app/diagnostics/SceneCycleSmokeDriver.cpp
    src/app/diagnostics/SwitchFlipSmokeDriver.cpp
    src/app/plugin_session/StartupAssetLoad.cpp
      src/app/scene/SceneRenderer.cpp
      src/app/scene/SceneSlotSynchronizer.cpp
      src/app/scene/SceneSlotSynchronizerResolve.cpp
      src/app/scene/SceneSlotSynchronizerUpload.cpp
      src/app/scene/SceneSlotSynchronizerRebake.cpp
      src/app/scene/SceneTrainController.cpp
      src/app/scene/SceneTrainControllerGroupTick.cpp
      src/app/scene/SceneTrainControllerQueries.cpp
      src/app/scene/SceneTrainFade.cpp
      src/app/scene/SceneSignalPhase.cpp
      src/app/scene/SceneTrainRenderSubmit.cpp
      src/app/scene/SceneTrainWheelSound.cpp
      src/app/scene/SceneTrainBatching.cpp
      src/app/scene/SceneSceneryRuntime.cpp
      src/app/scene/TrainPickBox.cpp
    src/app/scene/PlacedSoundRequestCollector.cpp
    src/app/scene/HeadlightCollector.cpp
    src/app/ui/GameSettingsPanel.cpp
    src/app/ui/RenderSettingsPanel.cpp
    src/app/ui/ViewModeSwitchPanel.cpp
    src/app/ui/SaveDataPanel.cpp
    src/app/ui/EditorPanel.cpp
    src/app/ui/EditorPanelEdgeSection.cpp
    src/app/ui/EditorPanelSectionEdit.cpp
    src/app/ui/EditorPanelNodeSection.cpp
    src/app/ui/StationDiaPanel.cpp
    src/app/ui/StationDiaPointSection.cpp
    src/app/ui/SignalBlockPanel.cpp
    src/app/ui/PlatformRoutinePanel.cpp
)
target_link_libraries(RailsimPlus PRIVATE RailsimPlusCore imgui_dx12 d3d12 dxgi d3dcompiler shell32 dwmapi
    CLI11::CLI11
    Freetype::Freetype
    RmlUi::Core
    winpix
    OpenAL::OpenAL
    miniaudio
)
# RmlUi統合: 公式Win32_DX12バックエンド
# (rmlui_backend_Win32_DX12)はリンクしない。src/app/rmlui/RmlUiRenderInterface.cpp は
# RmlUi_Renderer_DX12.cppのフォークで、RmlUi_Include_DirectX_12.h経由でD3D12MA/
# offsetAllocatorの実装を`#include`する(rmlui本体と同じ単一TU取り込み方式)ため、
# Backendsディレクトリを直接includeパスに加える。UIの図形の縁を滑らかにするためMSAAは4xにする
# (EndFrame合成はResolveSubresource経路になる。実行時のmsaa_sample_countはこのマクロを
# そのまま渡す。RmlUiRenderInterface.h冒頭コメント参照)。
target_include_directories(RailsimPlus PRIVATE "${rmlui_SOURCE_DIR}/Backends")
# SRVヒープの記述子数。既定の128はフォント・レイヤーと共用で、プラグインブラウザのカード画像
# (GPUに置く枚数は数十〜百枚程度)には足りないため広げる。
target_compile_definitions(RailsimPlus PRIVATE RMLUI_RENDER_BACKEND_FIELD_MSAA_SAMPLE_COUNT=4
    RMLUI_RENDER_BACKEND_FIELD_DESCRIPTORAMOUNT_FOR_SRV_CBV_UAV=1024)
# WinPixEventRuntime.dll を実行ファイル横へコピー
add_custom_command(TARGET RailsimPlus POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_PROPERTY:winpix,IMPORTED_LOCATION>"
        "$<TARGET_FILE_DIR:RailsimPlus>"
)
add_custom_command(TARGET RailsimPlus POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:OpenAL>"
        "$<TARGET_FILE_DIR:RailsimPlus>"
)
# M5フェーズ2: 実行時DXCコンパイル用にdxcompiler.dll/dxil.dllを実行ファイル横へコピー
# (dxil.dllはシェーダ署名検証に必要。無いとCompile()自体は成功するがドライバが拒否する)
add_custom_command(TARGET RailsimPlus POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${RS_DXCOMPILER_DLL}" "${RS_DXIL_DLL}"
        "$<TARGET_FILE_DIR:RailsimPlus>"
)
target_compile_definitions(RailsimPlus PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
# Debugだけ、フレームループが render_tuning.json の保存を見て調整値を読み直す(src/app/entry/AppRuntime.cpp)。
# RailsimPlusCore・テストには付けない(テストは埋め込みの値を前提にしている)。
target_compile_definitions(RailsimPlus PRIVATE $<$<CONFIG:Debug>:RS_TUNING_HOT_RELOAD>)
if(MSVC)
    target_compile_options(RailsimPlus PRIVATE /W4 /utf-8)
    target_compile_options(imgui_dx12 PRIVATE /utf-8)
    # Releaseのみ起動時のコンソール窓を出さない(GUIサブシステム化)。エントリは
    # int main()のままにするためmainCRTStartupを明示する。Debugはログ確認・
    # ヘッドレス検証(--smoke等)のためコンソールサブシステムのまま。
    target_link_options(RailsimPlus PRIVATE
        $<$<CONFIG:Release>:/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup>)
endif()

if(RS_BUILD_TESTS)
enable_testing()

# 導入ライブラリのスモークテスト(Catch2)
add_executable(RailsimPlusDepsSmokeTest tests/DepsSmokeTest.cpp)
target_link_libraries(RailsimPlusDepsSmokeTest PRIVATE
    Catch2::Catch2WithMain
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    magic_enum::magic_enum
    CLI11::CLI11
    DirectXTK12
    DirectXTex
    DirectXMesh
    d3d12 dxgi
)
target_compile_definitions(RailsimPlusDepsSmokeTest PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
    target_compile_options(RailsimPlusDepsSmokeTest PRIVATE /W4 /utf-8)
endif()
add_test(NAME RailsimPlusDepsSmokeTest COMMAND RailsimPlusDepsSmokeTest)

# 線形カーネル/メッシュ生成/敷設エディタの数値検証(Catch2、旧--testalign/meshspline/pickから移植)
add_executable(RailsimPlusTests
    tests/UpdateCheckerTests.cpp
    tests/RuntimeSettingOverridesTests.cpp
    tests/OverlayGeometryTests.cpp
    tests/ShaderCacheTests.cpp
    tests/PluginCatalogTests.cpp
    tests/PiParserTests.cpp
    tests/PiCustomizerSchemaTests.cpp
    tests/ZyLinkageTests.cpp
    tests/MoverRuntimeTests.cpp
    tests/TrackingMoveSolveTests.cpp
    tests/SwitchExprTests.cpp
    tests/RailMeshBuildTests.cpp
    tests/AlignmentTests.cpp
    tests/AlignmentTransitionTests.cpp
    tests/MeshSplineTests.cpp
    tests/StretchZyMeshBuilderTests.cpp
    tests/MeshAssemblerTests.cpp
    tests/EditorPickTests.cpp
    tests/TrackTraversalTests.cpp
    tests/SplineSampleBracketTests.cpp
    tests/TrackQueryTests.cpp
    tests/WarpTraversalTests.cpp
    tests/TrainWarpHeadsTests.cpp
    tests/Rs2WarpImportTests.cpp
    tests/TrainRuntimeTests.cpp
    tests/TrainRuntimePlatformTests.cpp
    tests/TrainRuntimeStationStopTests.cpp
    tests/TrainRuntimeStationWarpTests.cpp
    tests/TrainRuntimeStationReverseTests.cpp
    tests/TrainRuntimePointRoutineTests.cpp
    tests/TrainRuntimeWarpTests.cpp
    tests/TrainRuntimeConstraintTests.cpp
    tests/PlatformApproachTests.cpp
    tests/BlockOccupancyTests.cpp
    tests/SignalTests.cpp
    tests/BlockConnectivityTests.cpp
    tests/RouteReservationTests.cpp
    tests/SignalAspectTests.cpp
    tests/SignalModelTests.cpp
    tests/WarpLinkAuthoringTests.cpp
    tests/TrackChainTests.cpp
    tests/CalendarUtilTests.cpp
    tests/GameClockTests.cpp
    tests/DiaRuntimeTests.cpp
    tests/PlatformRoutineTests.cpp
    tests/PointRoutineTests.cpp
    tests/SolarPositionTests.cpp
    tests/EnvironmentPresetsTests.cpp
    tests/EnvSchemaTests.cpp
    tests/EnvTextureLoaderTests.cpp
    tests/LocalizationTests.cpp
    tests/TrainVehicleTests.cpp
    tests/TrainSchemaTests.cpp
    tests/PluginSidecarTests.cpp
    tests/TrainAssemblyTests.cpp
    tests/TrainKinematicsTests.cpp
    tests/AnimationRuntimeTests.cpp
    tests/MaterialRuntimeTests.cpp
    tests/ModelRuntimeTests.cpp
    tests/SoundEffectWorldTrackerTests.cpp
    tests/PlacedSoundSourcePoolTests.cpp
    tests/ParticleSimulationTests.cpp
    tests/ParticleBatchBuilderTests.cpp
    tests/TriggerChainTests.cpp
    tests/PluginSelectorTests.cpp
    tests/ToastQueueTests.cpp
    tests/SceneResidencyTests.cpp
    tests/ResourceManagerTests.cpp
    tests/SrvRangeAllocatorTests.cpp
    tests/FramePipelineTests.cpp
    tests/BatchResidencyEvictionTests.cpp
    tests/PartWorldHistoryTests.cpp
    tests/TaaMathTests.cpp
    tests/MotionBlurMathTests.cpp
    tests/DepthOfFieldMathTests.cpp
    tests/VolumetricFogMathTests.cpp
    tests/CustomizerParsersTests.cpp
    tests/PlatformParentPoseTests.cpp
    tests/Rs2PlatformParentTests.cpp
    tests/Rs2SchemaTests.cpp
    tests/Rs2ImportTests.cpp
    tests/Rs2LayoutSessionTests.cpp
    tests/Rs2SceneApplierTests.cpp
    tests/RailwayPluginSetSchemaTests.cpp
    tests/TrainGroupTemplateSchemaTests.cpp
    tests/SceneryTemplateSchemaTests.cpp
    tests/TemplateSaverTests.cpp
    tests/PluginTreeSchemaTests.cpp
    tests/ScenePlacementDecomposeTests.cpp
    tests/StaticPartWorldBakeTests.cpp
    tests/ManualPlacementOverlayTests.cpp
    tests/TrackPluginPlacementTests.cpp
    tests/ScenePickingTests.cpp
    tests/SceneSlotPoolTests.cpp
    tests/SceneTrainGroupCollectorTests.cpp
    tests/CabinViewCameraTests.cpp
    tests/PhotoCameraTests.cpp
    tests/StaticInstanceKeyTests.cpp
    tests/PluginSessionBakeIdempotenceTests.cpp
    tests/IntervalListTests.cpp
    tests/TrackGraphSerializeTests.cpp
    tests/ManualPlacementOverlaySerializeTests.cpp
    tests/SceneEntitySerializeTests.cpp
    tests/PlatformStopInfoSerializeTests.cpp
    tests/RslLayoutSerializeTests.cpp
    tests/EditorSectionEditTests.cpp
    tests/EdgeSelectionTests.cpp
    tests/EdgeSplitMergeTests.cpp
    tests/RailLinkEditTests.cpp
    tests/CantControlPointTests.cpp
    tests/FrustumTests.cpp
    tests/TonemapMathTests.cpp
    tests/TuningHotReloadTests.cpp
    tests/AtmosphereMathTests.cpp
    tests/PlatformEditTests.cpp
    tests/InputActionTests.cpp
    tests/WorkspaceShellTests.cpp
    tests/CompatReportTests.cpp
    tests/LayoutDocumentFlowTests.cpp
    tests/GameSettingsScreenModelTests.cpp
    tests/RenderSettingsRowsTests.cpp
    tests/RenderSettingsActionStubs.cpp
)
target_link_libraries(RailsimPlusTests PRIVATE RailsimPlusCore Catch2::Catch2WithMain)
target_compile_definitions(RailsimPlusTests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
# 実データ依存テストのデータセット既定位置(tests/TestData.h)。RS2のソース/実行環境と
# 同じ階層(リポジトリの親)に置く運用。環境変数 RAILSIM_TEST_DATA で上書きできる。
target_compile_definitions(RailsimPlusTests PRIVATE
    RS_TEST_DATA_DEFAULT="${CMAKE_CURRENT_SOURCE_DIR}/../Railsim_test")
if(MSVC)
    target_compile_options(RailsimPlusTests PRIVATE /W4 /utf-8)
endif()
add_test(NAME RailsimPlusTests COMMAND RailsimPlusTests)
endif()

# プリコンパイル済みヘッダ。多くの .cpp が読む重いヘッダ(spdlog・nlohmann/json・STL)だけを入れる。
# <windows.h>・d3d12.h は全 .cpp に読ませるとマクロが名前とぶつかるため入れない。
# ターゲットごとにコンパイル定義が違い、PCH を使い回すと不一致になるので、それぞれで作る。
set(RS_PCH_HEADERS
    <algorithm> <array> <chrono> <cmath> <cstdint> <filesystem> <functional> <map> <memory>
    <optional> <string> <string_view> <unordered_map> <vector>
    <nlohmann/json.hpp> <spdlog/spdlog.h>)
target_precompile_headers(RailsimPlusCore PRIVATE ${RS_PCH_HEADERS})
target_precompile_headers(RailsimPlus PRIVATE ${RS_PCH_HEADERS})
if(RS_BUILD_TESTS)
    target_precompile_headers(RailsimPlusTests PRIVATE ${RS_PCH_HEADERS})
endif()

# 自前ターゲットのコンパイル速度の設定。
# - C++ モジュールは使わないので依存スキャンを止める。スキャンが有効だと MSBuild が .cpp ごとに
#   別の cl を起動し、プロセスごとに PCH を読み直すうえ、std モジュールの準備も毎回走る。
# - スキャンを止めると cl が1本で直列に回るので、/MP で並列にする。
# - デバッグ情報は .obj に埋め込む(/Z7)。/Zi だと並列の cl が1つの PDB へ mspdbsrv 経由で
#   書き込み、そこで順番待ちになる。リンク時に PDB へまとめるので、デバッグには影響しない。
set(RS_SPEED_TARGETS RailsimPlusCore RailsimPlus imgui_dx12)
if(RS_BUILD_TESTS)
    list(APPEND RS_SPEED_TARGETS RailsimPlusTests RailsimPlusDepsSmokeTest)
endif()
foreach(_rs_target IN LISTS RS_SPEED_TARGETS)
    set_target_properties(${_rs_target} PROPERTIES
        CXX_SCAN_FOR_MODULES OFF
        MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
    target_compile_options(${_rs_target} PRIVATE /MP)
endforeach()

# ポータブル版のパッケージ(install / CPack)
include(cmake/ReleasePackage.cmake)
