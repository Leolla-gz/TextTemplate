#pragma once
//----------------------------------------------------------------------------------
//  AviUtl2 のテキストオブジェクトとテンプレートの橋渡し(SDK を使う部分)
//
//  Apply/Capture は EDIT_SECTION のコールバック内(=AviUtl2 が編集をロックしている間)でのみ呼ぶこと。
//----------------------------------------------------------------------------------
#include <windows.h>

#include <map>
#include <string>

#include "plugin2.h"
#include "template_store.h"

// エフェクト名 -> 種別(EDIT_HANDLE::EFFECT_TYPE_FILTER 等)の対応表
// 「どれがフィルタ効果か」を判定するために使う(テキスト=入力、標準描画=出力、縁取り=フィルタ)
using EffectTypeMap = std::map<std::wstring, int>;

// AviUtl2 に登録されている全エフェクトの種別を取得する(編集セクション外で呼ぶ)
EffectTypeMap BuildEffectTypeMap(EDIT_HANDLE* edit);

// 適用結果の集計
struct ApplyResult {
    int applied = 0;        // テンプレートを反映できたテキストオブジェクト数
    int skipped = 0;        // テキストではないため対象外にしたオブジェクト数
    int item_errors = 0;    // 個別の設定項目で失敗した数(不正な値・項目名違い等)
    int filter_errors = 0;  // 追加できなかったフィルタの数(フィルタ名が存在しない等)
};

// 選択中のオブジェクト(複数可)にテンプレートを適用する。
// 何も選択されていなければ「設定ウィンドウで表示中のオブジェクト」を対象にする。
//  1. テキスト設定(フォント・色…)を上書き(テキスト本文は変えない)
//  2. replace_filters なら既存のフィルタ効果を削除
//  3. テンプレートのフィルタ効果を上から順に追加して値を設定
// ※call_edit_section 内で呼ぶこと(書き込みを行うため)
//  ※サイズ・位置系の項目は opt で許可された物だけ適用する
ApplyResult ApplyTemplateToSelection(EDIT_SECTION* edit, const TextTemplate& tmpl, const EffectTypeMap& types,
                                     const ApplyOptions& opt);

// 選択中(なければ設定ウィンドウ表示中)のテキストオブジェクトから、
// テキスト設定とフィルタ効果を丸ごと読み取る。テキストが見つからなければ false。
// ※name は取り込まない(呼び出し側で決める)
bool CaptureTemplateFromSelection(EDIT_SECTION* edit, const EffectTypeMap& types, TextTemplate* out);
