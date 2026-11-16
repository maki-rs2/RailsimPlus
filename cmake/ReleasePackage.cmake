# ポータブル版(展開してそのまま動く ZIP)の install() と CPack の設定。
# 使い方: cmake --build build --config Release --target RailsimPlus
#         cpack --config build/CPackConfig.cmake -C Release   (build/ に ZIP ができる)
# または  cmake --install build --config Release --prefix <出力先>  でフォルダだけ作る。
# アセットのうち assets/config と assets/locale は実行ファイルへ埋め込み済みなので入れない。

# 配布フォルダの名前。ZIPを展開したときにファイルが散らばらないよう、この1フォルダの下へ入れる。
set(RS_PORTABLE_DIR RailsimPlus)

# 依存ライブラリ(FetchContent)も install() を持っているので、配布物にはコンポーネント portable だけを入れる。
install(TARGETS RailsimPlus RUNTIME DESTINATION ${RS_PORTABLE_DIR} COMPONENT portable)
install(FILES
    "$<TARGET_FILE:OpenAL>"
    "$<TARGET_PROPERTY:winpix,IMPORTED_LOCATION>"
    "${RS_DXCOMPILER_DLL}"
    "${RS_DXIL_DLL}"
    DESTINATION ${RS_PORTABLE_DIR} COMPONENT portable)
install(DIRECTORY assets/fonts assets/sky assets/sounds assets/ui DESTINATION ${RS_PORTABLE_DIR}/assets COMPONENT portable)
install(FILES LICENSE THIRD_PARTY_NOTICES.md DESTINATION ${RS_PORTABLE_DIR} COMPONENT portable)
install(DIRECTORY licenses DESTINATION ${RS_PORTABLE_DIR} COMPONENT portable)
install(FILES cmake/package/README.txt DESTINATION ${RS_PORTABLE_DIR} COMPONENT portable)

# 利用者のPCにVC++再頒布パッケージが無くても動くよう、ランタイムDLLを実行ファイルの隣へ置く。
set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${RS_PORTABLE_DIR})
set(CMAKE_INSTALL_UCRT_LIBRARIES FALSE)
set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT portable)
include(InstallRequiredSystemLibraries)

set(CPACK_GENERATOR ZIP)
set(CPACK_COMPONENTS_ALL portable)
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
set(CPACK_PACKAGE_NAME RailsimPlus)
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
# 名前に版を入れない。Releases の latest/download/ の直リンクが、版を上げても同じ URL で最新の ZIP を指すようにする。
set(CPACK_PACKAGE_FILE_NAME "RailsimPlus-win64")
include(CPack)
