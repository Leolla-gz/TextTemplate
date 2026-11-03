//----------------------------------------------------------------------------------
//  テキストテンプレートの保存形式の変換処理
//----------------------------------------------------------------------------------
#include "template_store.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace {

constexpr const char* kSection       = "[Template]";
constexpr const char* kFilterSection = "[Template.filter]";
constexpr const char* kKeyName       = "name";
constexpr const char* kKeyReplace    = "replace_filters";
constexpr const char* kTextPrefix    = "text.";          // テキスト項目のキーの接頭辞
constexpr const char* kDrawEffect    = "draw.effect";    // 出力エフェクト名
constexpr const char* kDrawPrefix    = "draw.";          // 出力エフェクト項目のキーの接頭辞
constexpr const char* kEffectName    = "effect.name";
constexpr const char* kEffectDisable = "effect.disable";

// 行末の CR/LF を取り除く(Windows の改行 CRLF 対策)
void TrimCr(std::string& s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
}

// 改行は1行1項目の保存形式を壊すので、保存前に空白へ置き換える
std::string Sanitize(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        if (c == '\r' || c == '\n') c = ' ';
    }
    return out;
}

bool StartsWith(const std::string& s, const char* prefix) {
    return s.compare(0, std::char_traits<char>::length(prefix), prefix) == 0;
}

int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// 1行ずつ読み、BOM除去・CR除去をして処理関数に渡す共通ループ
template <class Fn>
void ForEachLine(const std::string& text, Fn fn) {
    std::istringstream in(text);
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        TrimCr(line);
        // 先頭行の UTF-8 BOM(EF BB BF)を除去(メモ帳で保存した場合など)
        if (first && line.size() >= 3 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
        first = false;
        fn(line);
    }
}

// "キー=値" に分解(値の中の '=' は許容するため最初の '=' で区切る)
bool SplitKeyValue(const std::string& line, std::string* key, std::string* value) {
    const auto eq = line.find('=');
    if (eq == std::string::npos) return false;
    *key = line.substr(0, eq);
    *value = line.substr(eq + 1);
    return true;
}

// フィルタのキー=値 を EffectSetting に反映
void ApplyEffectKey(EffectSetting* e, const std::string& key, const std::string& value) {
    if (key == kEffectName) e->name = value;
    else if (key == kEffectDisable) e->enabled = (value != "1");
    else if (!IsMetaKey(key)) e->items.emplace_back(key, value);
}

}  // namespace

// ---------------- ItemList ----------------

const std::string* FindItem(const ItemList& items, const std::string& key) {
    for (const auto& kv : items) {
        if (kv.first == key) return &kv.second;
    }
    return nullptr;
}

void SetItem(ItemList* items, const std::string& key, const std::string& value) {
    for (auto& kv : *items) {
        if (kv.first == key) {
            kv.second = value;
            return;
        }
    }
    items->emplace_back(key, value);
}

void EraseItem(ItemList* items, const std::string& key) {
    for (auto it = items->begin(); it != items->end(); ++it) {
        if (it->first == key) {
            items->erase(it);
            return;
        }
    }
}

ItemGroup GroupOfTextKey(const std::string& key) {
    return key == text_key::kSize ? ItemGroup::Size : ItemGroup::Look;
}

ItemGroup GroupOfDrawKey(const std::string& key) {
    // 標準描画の項目名は実データ(.aup2)で確認済み
    static const char* const kSizeKeys[] = {u8"拡大率", u8"縦横比"};
    static const char* const kPosKeys[] = {"X", "Y", "Z", u8"中心X", u8"中心Y", u8"中心Z",
                                           u8"X軸回転", u8"Y軸回転", u8"Z軸回転"};
    for (const char* k : kSizeKeys) if (key == k) return ItemGroup::Size;
    for (const char* k : kPosKeys) if (key == k) return ItemGroup::Position;
    return ItemGroup::Ignore;  // 透明度・合成モード 等
}

bool IsGroupEnabled(ItemGroup group, const ApplyOptions& opt) {
    switch (group) {
        case ItemGroup::Look:     return true;
        case ItemGroup::Size:     return opt.size;
        case ItemGroup::Position: return opt.position;
        default:                  return false;
    }
}

bool IsExcludedTextKey(const std::string& key) {
    // テキスト本文と「長さ自動調節」は字幕ごとに違うので、テンプレートでは扱わない
    return key == text_key::kText || key == u8"オブジェクトの長さを自動調節";
}

bool IsMetaKey(const std::string& key) {
    // effect.* / display.* は値ではなく管理情報(エフェクト名・有効状態・表示の折りたたみ等)
    return StartsWith(key, "effect.") || StartsWith(key, "display.");
}

// ---------------- 保存形式 ----------------

std::vector<TextTemplate> ParseTemplates(const std::string& text) {
    std::vector<TextTemplate> result;
    TextTemplate* tmpl = nullptr;     // 読み込み中のテンプレート
    EffectSetting* filter = nullptr;  // 読み込み中のフィルタ(テンプレート直下なら nullptr)

    ForEachLine(text, [&](const std::string& line) {
        if (line.empty() || line[0] == ';') return;  // 空行・コメント
        if (line == kSection) {
            result.emplace_back();
            tmpl = &result.back();
            // replace_filters キーが無い(旧形式)なら「置き換えない」。
            // 旧形式のテンプレートはフィルタを持たないので、置き換えにすると既存フィルタが消えてしまうため
            tmpl->replace_filters = false;
            filter = nullptr;
            return;
        }
        if (line == kFilterSection) {
            if (!tmpl) return;  // テンプレートより前のフィルタは無視
            tmpl->filters.emplace_back();
            filter = &tmpl->filters.back();
            return;
        }
        if (!tmpl) return;  // セクション外の行は無視

        std::string key, value;
        if (!SplitKeyValue(line, &key, &value)) return;

        if (filter) {
            ApplyEffectKey(filter, key, value);
        } else if (key == kKeyName) {
            tmpl->name = value;
        } else if (key == kKeyReplace) {
            tmpl->replace_filters = (value == "1");
        } else if (key == kDrawEffect) {
            tmpl->draw_effect = value;
        } else if (StartsWith(key, kDrawPrefix)) {
            SetItem(&tmpl->draw_items, key.substr(5), value);  // "draw.X" → "X"
        } else {
            // "text.フォント" → "フォント"。旧形式(接頭辞なし)もテキスト項目として読む
            const std::string item = StartsWith(key, kTextPrefix) ? key.substr(5) : key;
            if (!value.empty() && !IsExcludedTextKey(item)) SetItem(&tmpl->text_items, item, value);
        }
    });
    // effect.name が無い壊れたフィルタは捨てる
    for (auto& t : result) {
        for (auto it = t.filters.begin(); it != t.filters.end();) {
            it = it->name.empty() ? t.filters.erase(it) : it + 1;
        }
    }
    return result;
}

std::string SerializeTemplates(const std::vector<TextTemplate>& templates) {
    std::ostringstream out;
    out << "; AviUtl2 TextTemplate plugin data v2 (UTF-8)\r\n";
    for (const auto& t : templates) {
        out << kSection << "\r\n";
        out << kKeyName << '=' << Sanitize(t.name) << "\r\n";
        out << kKeyReplace << '=' << (t.replace_filters ? "1" : "0") << "\r\n";
        for (const auto& kv : t.text_items) {
            if (kv.second.empty()) continue;  // 空 = 適用しない項目は保存しない
            out << kTextPrefix << Sanitize(kv.first) << '=' << Sanitize(kv.second) << "\r\n";
        }
        if (!t.draw_effect.empty()) {
            out << kDrawEffect << '=' << Sanitize(t.draw_effect) << "\r\n";
            for (const auto& kv : t.draw_items)
                out << kDrawPrefix << Sanitize(kv.first) << '=' << Sanitize(kv.second) << "\r\n";
        }
        for (const auto& f : t.filters) {
            out << kFilterSection << "\r\n";
            out << kEffectName << '=' << Sanitize(f.name) << "\r\n";
            if (!f.enabled) out << kEffectDisable << "=1\r\n";
            for (const auto& kv : f.items) out << Sanitize(kv.first) << '=' << Sanitize(kv.second) << "\r\n";
        }
        out << "\r\n";
    }
    return out.str();
}

std::vector<EffectSetting> ParseAliasEffects(const std::string& alias) {
    // エイリアスの書式(例):
    //   [Object]            ← オブジェクト全体の情報(layer/frame 等)。エフェクトではない
    //   [Object.0]          ← 0番目のエフェクト
    //   effect.name=テキスト
    //   ...
    // プロジェクトファイル(.aup2)では [21.0] のような形式になるので、
    // 「[ と ] の間に '.' を含み、'.' の後ろが数字」をエフェクトの見出しとみなす。
    std::vector<EffectSetting> effects;
    EffectSetting* cur = nullptr;
    ForEachLine(alias, [&](const std::string& line) {
        if (line.empty()) return;
        if (line.front() == '[' && line.back() == ']') {
            const std::string inner = line.substr(1, line.size() - 2);
            const auto dot = inner.rfind('.');
            const bool is_effect = dot != std::string::npos && dot + 1 < inner.size() &&
                                   inner.find_first_not_of("0123456789", dot + 1) == std::string::npos;
            if (is_effect) {
                effects.emplace_back();
                cur = &effects.back();
            } else {
                cur = nullptr;
            }
            return;
        }
        std::string key, value;
        if (cur && SplitKeyValue(line, &key, &value)) ApplyEffectKey(cur, key, value);
    });
    return effects;
}

// ---------------- 小物 ----------------

bool ParseHexColor(const std::string& hex, unsigned char* r, unsigned char* g, unsigned char* b) {
    if (hex.size() != 6) return false;
    int v[6];
    for (int i = 0; i < 6; ++i) {
        v[i] = HexDigit(hex[i]);
        if (v[i] < 0) return false;
    }
    *r = static_cast<unsigned char>(v[0] * 16 + v[1]);
    *g = static_cast<unsigned char>(v[2] * 16 + v[3]);
    *b = static_cast<unsigned char>(v[4] * 16 + v[5]);
    return true;
}

std::string FormatHexColor(unsigned char r, unsigned char g, unsigned char b) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02x%02x%02x", r, g, b);
    return buf;
}

double LeadingNumber(const std::string& value, double fallback) {
    char* end = nullptr;
    const double v = std::strtod(value.c_str(), &end);
    return (end == value.c_str()) ? fallback : v;
}

const std::vector<std::string>& DecorationCandidates() {
    // 「標準文字」は実データで確認済み。それ以外は AviUtl(初代)の選択肢からの推測。
    static const std::vector<std::string> list = {
        u8"標準文字", u8"影付き文字", u8"影付き文字(薄)", u8"縁取り文字", u8"縁取り文字(細)",
    };
    return list;
}
