# Visual Studio(MSVC)でビルドする場合用。VS 未導入なら build_mingw.sh のビルド済みファイルを使えばよい。
cmake_minimum_required(VERSION 3.20)
project(TextTemplate CXX)

add_library(TextTemplate SHARED
  src/plugin_main.cpp src/main_window.cpp src/edit_dialog.cpp src/preview.cpp
  src/text_object.cpp src/template_store.cpp src/util_win.cpp)
target_include_directories(TextTemplate PRIVATE sdk src)
target_compile_features(TextTemplate PRIVATE cxx_std_17)
target_compile_options(TextTemplate PRIVATE /utf-8 /FIstdint.h)
target_compile_definitions(TextTemplate PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN)
target_link_libraries(TextTemplate PRIVATE comdlg32 gdi32 user32 comctl32)
# 出力ファイル名を TextTemplate.aux2 にする
set_target_properties(TextTemplate PROPERTIES SUFFIX ".aux2")
