//----------------------------------------------------------------------------------
//  テキストオブジェクトへの適用・取り込み
//----------------------------------------------------------------------------------
#include "text_object.h"

#include <vector>

#include "util_win.h"

namespace {

constexpr LPCWSTR kEffectText = L"テキスト";
constexpr LPCWSTR kEffectDraw = L"標準描画";

// 処理対象のオブジェクト一覧を作る
// 1) タイムラインで選択中のオブジェクト(複数選択に対応)
// 2) 1 が無ければ、オブジェクト設定ウィンドウで表示中のオブジェクト
std::vector<OBJECT_HANDLE> CollectTargets(EDIT_SECTION* edit) {
    std::vector<OBJECT_HANDLE> targets;
    const int num = edit->get_selected_object_num();
    for (int i = 0; i < num; ++i) {
        if (OBJECT_HANDLE obj = edit->get_selected_object(i)) targets.push_back(obj);
    }
    if (targets.empty()) {
        if (OBJECT_HANDLE obj = edit->get_focus_object()) targets.push_back(obj);
    }
    return targets;
}

bool IsTextObject(EDIT_SECTION* edit, OBJECT_HANDLE obj) {
    return edit->count_object_effect(obj, kEffectText) > 0;
}

// フィルタ効果か?
// 種別表に載っていればその種別で判定。載っていない場合は strict に応じて
//  strict=true : フィルタとみなさない(削除判定用。分からない物は消さない = 安全側)
//  strict=false: テキスト/標準描画 以外はフィルタとみなす(取り込み用)
bool IsFilter(const EffectTypeMap& types, const std::wstring& name, bool strict) {
    const auto it = types.find(name);
    if (it != types.end()) return it->second == EDIT_HANDLE::EFFECT_TYPE_FILTER;
    if (strict) return false;
    return name != kEffectText && name != kEffectDraw;
}

// 出力エフェクト(標準描画 等)か
bool IsOutput(const EffectTypeMap& types, const std::wstring& name) {
    const auto it = types.find(name);
    if (it != types.end()) return it->second == EDIT_HANDLE::EFFECT_TYPE_OUTPUT;
    return name == kEffectDraw;
}

// 既存のフィルタ効果をすべて削除
void DeleteFilters(EDIT_SECTION* edit, OBJECT_HANDLE obj, const EffectTypeMap& types) {
    // 一覧を先に取得してから削除する(削除しながら列挙すると番号がずれるため)
    const int n = edit->get_effect_list(obj, nullptr, 0);
    if (n <= 0) return;
    std::vector<EFFECT_HANDLE> list(n);
    const int got = edit->get_effect_list(obj, list.data(), n);
    for (int i = 0; i < got; ++i) {
        LPCWSTR name = edit->get_effect_name(list[i]);
        if (name && IsFilter(types, name, true)) edit->delete_effect(obj, list[i]);
    }
}

}  // namespace

EffectTypeMap BuildEffectTypeMap(EDIT_HANDLE* edit) {
    EffectTypeMap map;
    edit->enum_effect_name(&map, [](void* param, LPCWSTR name, int type, int /*flag*/) {
        (*static_cast<EffectTypeMap*>(param))[name] = type;
    });
    return map;
}

ApplyResult ApplyTemplateToSelection(EDIT_SECTION* edit, const TextTemplate& t, const EffectTypeMap& types,
                                     const ApplyOptions& opt) {
    const std::wstring draw_effect = Utf8ToWide(t.draw_effect);
    ApplyResult result;
    for (OBJECT_HANDLE obj : CollectTargets(edit)) {
        if (!IsTextObject(edit, obj)) {
            ++result.skipped;  // 画像・図形などは対象外
            continue;
        }

        // 1. テキスト設定。空の値は「適用しない」、本文(テキスト)は対象外、サイズはオプション次第
        for (const auto& kv : t.text_items) {
            if (kv.second.empty() || IsExcludedTextKey(kv.first)) continue;
            if (!IsGroupEnabled(GroupOfTextKey(kv.first), opt)) continue;
            if (!edit->set_object_item_value(obj, kEffectText, Utf8ToWide(kv.first).c_str(), kv.second.c_str()))
                ++result.item_errors;
        }

        // 1b. 標準描画の拡大率・位置(オプションで許可された物だけ)
        if (!draw_effect.empty()) {
            for (const auto& kv : t.draw_items) {
                if (!IsGroupEnabled(GroupOfDrawKey(kv.first), opt)) continue;
                if (!edit->set_object_item_value(obj, draw_effect.c_str(), Utf8ToWide(kv.first).c_str(),
                                                 kv.second.c_str()))
                    ++result.item_errors;
            }
        }

        // 2. 既存フィルタの削除(テンプレートの設定で選択)
        if (t.replace_filters) DeleteFilters(edit, obj, types);

        // 3. フィルタ追加(create_effect はオブジェクトの末尾に追加されるので、テンプレートの順序が保たれる)
        for (const auto& f : t.filters) {
            EFFECT_HANDLE e = edit->create_effect(obj, Utf8ToWide(f.name).c_str());
            if (!e) {
                ++result.filter_errors;
                continue;
            }
            for (const auto& kv : f.items) {
                if (!edit->set_effect_item_value(e, Utf8ToWide(kv.first).c_str(), kv.second.c_str()))
                    ++result.item_errors;
            }
            if (!f.enabled) edit->set_effect_enable(e, false);
        }
        ++result.applied;
    }
    return result;
}

bool CaptureTemplateFromSelection(EDIT_SECTION* edit, const EffectTypeMap& types, TextTemplate* out) {
    for (OBJECT_HANDLE obj : CollectTargets(edit)) {
        if (!IsTextObject(edit, obj)) continue;

        // オブジェクト全体をエイリアス文字列(.object と同じ書式)で取得して分解する。
        // 1項目ずつ問い合わせるより、未知のフィルタや項目もまとめて漏れなく取れる。
        LPCSTR alias = edit->get_object_alias(obj);
        if (!alias) continue;
        const std::vector<EffectSetting> effects = ParseAliasEffects(alias);

        out->text_items.clear();
        out->filters.clear();
        out->draw_effect.clear();
        out->draw_items.clear();
        bool text_done = false;
        for (const auto& e : effects) {
            const std::wstring wname = Utf8ToWide(e.name);
            if (!text_done && wname == kEffectText) {
                // テキスト設定(本文などテンプレート対象外の項目は除く)
                for (const auto& kv : e.items) {
                    if (!IsExcludedTextKey(kv.first)) out->text_items.push_back(kv);
                }
                text_done = true;
            } else if (IsOutput(types, wname)) {
                // 標準描画: サイズ系・位置系の項目だけ取り込む(適用するかはパネルで都度選ぶ)
                out->draw_effect = e.name;
                for (const auto& kv : e.items) {
                    if (GroupOfDrawKey(kv.first) != ItemGroup::Ignore) out->draw_items.push_back(kv);
                }
            } else if (IsFilter(types, wname, false)) {
                out->filters.push_back(e);
            }
        }
        return text_done;
    }
    return false;
}
