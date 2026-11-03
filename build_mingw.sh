#!/bin/sh
# MinGW-w64(Linux/WSL 上の Windows 向けクロスコンパイラ)で TextTemplate.aux2 を作るスクリプト
#  -static            : 実行に必要なライブラリを .aux2 に同梱(相手PCに追加DLL不要にする)
#  -finput-charset 等 : ソースは UTF-8
#  -municode/-DUNICODE: Windows API を UTF-16(W版)で使う
set -eu
cd "$(dirname "$0")"
mkdir -p build
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -shared -static \
  -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -include stdint.h \
  -Isdk -Isrc \
  src/plugin_main.cpp src/main_window.cpp src/edit_dialog.cpp src/preview.cpp \
  src/text_object.cpp src/template_store.cpp src/util_win.cpp \
  -o build/TextTemplate.aux2 \
  -lcomdlg32 -lgdi32 -luser32 -lcomctl32 \
  -Wl,--kill-at -s
echo "built: build/TextTemplate.aux2"
