#pragma once
//----------------------------------------------------------------------------------
//  AviUtl2 内に表示する「テキストテンプレ」パネル
//----------------------------------------------------------------------------------
#include <windows.h>

#include <string>

#include "config2.h"
#include "logger2.h"
#include "plugin2.h"

// パネル(ウィンドウ)を作成する。RegisterPlugin() 内から呼ぶ。
// store_path : テンプレートの保存ファイルのパス
// 戻り値     : 作成したウィンドウ(失敗時 nullptr)
HWND CreateTemplatePanel(EDIT_HANDLE* edit, LOG_HANDLE* logger, CONFIG_HANDLE* config, const std::wstring& store_path);
