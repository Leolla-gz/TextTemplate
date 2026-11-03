#pragma once
//----------------------------------------------------------------------------------
//  テンプレート編集ダイアログ
//
//  リソースファイル(.rc)を使わず、コードでウィンドウと部品を作る方式。
//  「モーダル」= ダイアログを閉じるまで親ウィンドウを操作できない表示方法。
//----------------------------------------------------------------------------------
#include <windows.h>

#include <string>
#include <vector>

#include "template_store.h"

// ダイアログを表示し、OK なら tmpl を書き換えて true を返す。
// owner : 親ウィンドウ(このウィンドウは閉じるまで無効化される)
// fonts : フォント選択肢(AviUtl2 に登録されているフォント名)
// title : ウィンドウタイトル
bool ShowTemplateEditDialog(HWND owner, const std::vector<std::wstring>& fonts, LPCWSTR title, TextTemplate* tmpl);
