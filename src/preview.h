#pragma once
//----------------------------------------------------------------------------------
//  テンプレートの見た目プレビュー描画(GDI)
//
//  GDI = Windows 標準の2D描画API。AviUtl2 本体の描画(DirectWrite + GPU)とは別物なので
//  「近い見た目」の再現にとどまる。再現するのは下記のみ(それ以外のフィルタは名前表示だけ):
//   - テキスト設定: フォント / 文字色 / 影・縁色 / 文字装飾(影付き・縁取り)/ B / I
//   - フィルタ    : 縁取り(何重でも可)/ グラデーション / シャドー
//----------------------------------------------------------------------------------
#include <windows.h>

#include <string>

#include "template_store.h"

// プレビューに使うサンプル文字列(ひらがな・英大文字・英小文字・漢字を1度に確認できる)
constexpr LPCWSTR kPreviewSample = L"あAa漢";

// rect の範囲にプレビュー(背景+装飾付きサンプル文字)を描く
void DrawTemplatePreview(HDC hdc, const RECT& rect, const TextTemplate& tmpl);

// フィルタ構成の要約文字列 例: "縁取り×2 / グラデーション / フェード"(フィルタ無しなら空)
std::wstring FilterSummary(const TextTemplate& tmpl);
