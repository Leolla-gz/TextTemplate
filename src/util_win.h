#pragma once
//----------------------------------------------------------------------------------
//  Windows 用の小物関数(文字コード変換・ファイル入出力・色変換)
//
//  補足: Windows API の文字列は UTF-16(wchar_t / LPCWSTR)、
//        AviUtl2 の設定値は UTF-8(char / LPCSTR) なので、両者の変換が頻繁に必要になる。
//----------------------------------------------------------------------------------
#include <windows.h>

#include <string>

// このプラグイン DLL 自身のインスタンスハンドル
// (GetModuleHandle(nullptr) は AviUtl2 本体 exe を返すので、ウィンドウクラス登録にはこちらを使う)
HINSTANCE ThisModule();

// UTF-8 -> UTF-16
std::wstring Utf8ToWide(const std::string& s);
// UTF-16 -> UTF-8
std::string WideToUtf8(const std::wstring& s);

// ファイル全体をバイト列として読む。失敗時は false
bool ReadWholeFile(const std::wstring& path, std::string* out);
// バイト列をファイルに書く(一時ファイルに書いてから置き換える = 書き込み途中で落ちても壊れにくい)
bool WriteWholeFile(const std::wstring& path, const std::string& data);
// フォルダを(親フォルダも含めて)作成する。既にあれば何もしない
void EnsureDirectory(const std::wstring& dir);

// "rrggbb" -> COLORREF(Windows の色型。内部の並びは 0x00BBGGRR なので注意)
// 不正な文字列なら fallback を返す
COLORREF HexToColorRef(const std::string& hex, COLORREF fallback);
// COLORREF -> "rrggbb"
std::string ColorRefToHex(COLORREF c);

// ウィンドウ(エディット等)の文字列を UTF-8 で取得
std::string GetWindowTextUtf8(HWND hwnd);
