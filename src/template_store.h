#pragma once
//----------------------------------------------------------------------------------
//  テキストテンプレートのデータ定義と保存形式(文字列 <-> 構造体)の変換
//  ※Windows API に依存しないので Linux 等でも単体テストできる
//----------------------------------------------------------------------------------
#include <string>
#include <utility>
#include <vector>

// 「項目名=値」の並び。値は AviUtl2 のエイリアス(.object)と同じ書式の UTF-8 文字列。
// 例: {"フォント","Yu Gothic UI"}, {"サイズ","100,1,1,直線移動,0"}(動きのある値もそのまま保持)
// 順序を保つため map ではなく vector にしている。
using ItemList = std::vector<std::pair<std::string, std::string>>;

// ItemList の小物関数
const std::string* FindItem(const ItemList& items, const std::string& key);  // 無ければ nullptr
void SetItem(ItemList* items, const std::string& key, const std::string& value);  // 無ければ末尾に追加
void EraseItem(ItemList* items, const std::string& key);

// フィルタ効果1つ分(縁取り・グラデーション・ぼかし 等)
struct EffectSetting {
    std::string name;      // エフェクト名(エイリアスの effect.name)例: "縁取り"
    bool enabled = true;   // 有効/無効(エイリアスの effect.disable=1 なら無効)
    ItemList items;        // 設定項目 例: {"サイズ","4"}, {"縁色","000000"}
};

// 1件分のテンプレート
struct TextTemplate {
    std::string name;              // テンプレート名(UI表示用)
    ItemList text_items;           // 「テキスト」エフェクトの設定(フォント・文字色・文字装飾…)
                                   // ※値が空の項目は「適用しない」
    std::string draw_effect;       // 出力エフェクト名(通常 "標準描画")。空なら位置・拡大率を持たない
    ItemList draw_items;           // 出力エフェクトの「サイズ系・位置系」の項目(拡大率・X・Y…)
                                   // ※適用するかはパネルのチェック(ApplyOptions)で都度選ぶ
    std::vector<EffectSetting> filters;  // 追加するフィルタ効果(上から順に適用)
    bool replace_filters = true;   // 適用時、対象オブジェクトの既存フィルタを削除してから追加するか
};

// テキスト設定の主な項目名(AviUtl2 の実データで確認済み)
namespace text_key {
constexpr const char* kFont   = u8"フォント";
constexpr const char* kColor  = u8"文字色";
constexpr const char* kEdge   = u8"影・縁色";
constexpr const char* kDeco   = u8"文字装飾";
constexpr const char* kBold   = "B";
constexpr const char* kItalic = "I";
constexpr const char* kSize   = u8"サイズ";
constexpr const char* kText   = u8"テキスト";  // 文字列そのもの(テンプレート対象外)
}  // namespace text_key

// 項目の分類。サイズ・位置は「適用するときとしないときがある」ため、パネルのチェックで都度選ぶ
enum class ItemGroup {
    Look,      // 見た目(フォント・色 等)… 常に適用
    Size,      // 文字のサイズ・拡大率・縦横比
    Position,  // 位置(X・Y・Z)・中心・回転
    Ignore,    // 透明度・合成モード等(テンプレート対象外)
};
ItemGroup GroupOfTextKey(const std::string& key);  // 「テキスト」の項目
ItemGroup GroupOfDrawKey(const std::string& key);  // 「標準描画」の項目

// 適用時のオプション(パネルのチェックボックス)
struct ApplyOptions {
    bool size = false;      // サイズ・拡大率も適用する
    bool position = false;  // 位置・回転も適用する
};
// 指定グループの項目を、このオプションで適用してよいか
bool IsGroupEnabled(ItemGroup group, const ApplyOptions& opt);

// テンプレートに取り込まない「テキスト」の項目か(字幕ごとに違う内容・長さ等)
bool IsExcludedTextKey(const std::string& key);

// エイリアスの管理用キー(effect.name / effect.disable / display.hide 等)か
bool IsMetaKey(const std::string& key);

// 保存ファイルの中身(UTF-8文字列) -> テンプレート一覧
// 書式は INI 風:
//   [Template]          ← テンプレートの開始
//   name=見出し
//   replace_filters=1
//   text.フォント=Yu Gothic UI
//   [Template.filter]   ← 直前のテンプレートに属するフィルタ
//   effect.name=縁取り
//   サイズ=4
// 旧形式(v0.1: [Template] 直下に フォント=… を書く形式)も読める。
std::vector<TextTemplate> ParseTemplates(const std::string& text);

// テンプレート一覧 -> 保存ファイルの中身(UTF-8文字列)
std::string SerializeTemplates(const std::vector<TextTemplate>& templates);

// エイリアス文字列(get_object_alias の戻り値 / .object ファイルの中身)を
// エフェクト単位に分解する。先頭から順に [テキスト, 標準描画, フィルタ…] のように並ぶ。
std::vector<EffectSetting> ParseAliasEffects(const std::string& alias);

// "rrggbb" を R,G,B に分解する。書式が不正なら false。
bool ParseHexColor(const std::string& hex, unsigned char* r, unsigned char* g, unsigned char* b);

// R,G,B -> "rrggbb"(小文字)
std::string FormatHexColor(unsigned char r, unsigned char g, unsigned char b);

// トラックバー値の先頭の数値を取り出す("100,1,1,直線移動,0" -> 100)。数値でなければ fallback
double LeadingNumber(const std::string& value, double fallback);

// 文字装飾の候補(エディット可能なコンボボックスの選択肢として使う)
// ※「標準文字」以外は推測。確実なのは「選択から登録」で実物の値を取り込む方法。
const std::vector<std::string>& DecorationCandidates();
