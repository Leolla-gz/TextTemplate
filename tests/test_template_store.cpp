//----------------------------------------------------------------------------------
//  template_store の単体テスト(Windows不要。g++ だけでビルド・実行できる)
//  ビルド例: g++ -std=c++17 -Isrc tests/test_template_store.cpp src/template_store.cpp
//----------------------------------------------------------------------------------
#include <cstdio>
#include <cstdlib>

#include "template_store.h"

static int g_fail = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("NG: %s (line %d)\n", #cond, __LINE__);         \
            ++g_fail;                                                   \
        }                                                               \
    } while (0)

static std::string Get(const ItemList& items, const char* key) {
    const std::string* v = FindItem(items, key);
    return v ? *v : std::string("<none>");
}

int main() {
    // 1) 保存 -> 読込の往復(テキスト項目+多重縁取り+グラデーション)
    TextTemplate a;
    a.name = u8"見出し";
    a.replace_filters = false;
    SetItem(&a.text_items, text_key::kFont, "Yu Gothic UI");
    SetItem(&a.text_items, text_key::kColor, "ffcc00");
    SetItem(&a.text_items, text_key::kSize, u8"100,1,1,直線移動,0");
    SetItem(&a.text_items, text_key::kDeco, "");  // 空 = 適用しない → 保存されない
    a.filters.push_back({u8"縁取り", true, {{u8"サイズ", "4"}, {u8"縁色", "ffffff"}}});
    a.filters.push_back({u8"縁取り", true, {{u8"サイズ", "8"}, {u8"縁色", "000000"}}});
    a.filters.push_back({u8"グロー", false, {{u8"光色", ""}}});  // 空の値も保持(フィルタは空が正規値の場合あり)

    a.draw_effect = u8"標準描画";
    SetItem(&a.draw_items, "X", "0.00");
    SetItem(&a.draw_items, u8"拡大率", "150.000");

    TextTemplate b;
    b.name = u8"名前=に=イコール";

    const auto parsed = ParseTemplates(SerializeTemplates({a, b}));
    CHECK(parsed.size() == 2);
    if (parsed.size() == 2) {
        const auto& p = parsed[0];
        CHECK(p.name == a.name);
        CHECK(p.replace_filters == false);
        CHECK(Get(p.text_items, text_key::kFont) == "Yu Gothic UI");
        CHECK(Get(p.text_items, text_key::kSize) == u8"100,1,1,直線移動,0");
        CHECK(Get(p.text_items, text_key::kDeco) == "<none>");
        CHECK(p.filters.size() == 3);
        if (p.filters.size() == 3) {
            CHECK(p.filters[1].name == u8"縁取り" && Get(p.filters[1].items, u8"サイズ") == "8");
            CHECK(p.filters[2].enabled == false && Get(p.filters[2].items, u8"光色") == "");
        }
        CHECK(p.draw_effect == u8"標準描画" && Get(p.draw_items, "X") == "0.00" &&
              Get(p.draw_items, u8"拡大率") == "150.000");
        CHECK(parsed[1].name == b.name && parsed[1].replace_filters == true && parsed[1].filters.empty());
        CHECK(parsed[1].draw_effect.empty() && parsed[1].draw_items.empty());
    }

    // 2) 旧形式(v0.1)の読込: 接頭辞なしキー・空のサイズは適用しない扱い
    const std::string v1 = std::string("\xEF\xBB\xBF; old\n[Template]\nname=x\n") + u8"フォント=MS Gothic\n" +
                           u8"文字色=112233\nB=1\n" + u8"サイズ=\n";
    const auto p2 = ParseTemplates(v1);
    CHECK(p2.size() == 1);
    if (p2.size() == 1) {
        CHECK(Get(p2[0].text_items, text_key::kFont) == "MS Gothic");
        CHECK(Get(p2[0].text_items, "B") == "1");
        CHECK(Get(p2[0].text_items, text_key::kSize) == "<none>");
        CHECK(p2[0].replace_filters == false);  // 旧形式は既存フィルタを残す
    }

    // 3) エイリアスの分解(get_object_alias の形式 / .aup2 の形式)
    const std::string alias = std::string("[Object]\nlayer=1\nframe=0,99\n[Object.0]\n") + u8"effect.name=テキスト\n" +
                              u8"サイズ=40.00\nテキスト=こんにちは\n[Object.1]\n" + u8"effect.name=標準描画\nX=0.00\n" +
                              "[Object.2]\n" + u8"effect.name=グラデーション\neffect.disable=1\ndisplay.hide=1\n" +
                              u8"開始色=00bfbf\n";
    const auto eff = ParseAliasEffects(alias);
    CHECK(eff.size() == 3);
    if (eff.size() == 3) {
        CHECK(eff[0].name == u8"テキスト" && Get(eff[0].items, u8"テキスト") == u8"こんにちは");
        CHECK(eff[2].name == u8"グラデーション" && eff[2].enabled == false);
        CHECK(eff[2].items.size() == 1);  // display.hide は管理キーなので除外
    }
    CHECK(ParseAliasEffects(std::string("[21]\nlayer=1\n[21.0]\n") + u8"effect.name=テキスト\n").size() == 1);

    // 4) 改行を含む名前は空白に置換
    TextTemplate c;
    c.name = "a\r\nb";
    const auto p3 = ParseTemplates(SerializeTemplates({c}));
    CHECK(p3.size() == 1 && p3[0].name == "a  b");

    // 5) 小物
    unsigned char r, g, bl;
    CHECK(ParseHexColor("ff8001", &r, &g, &bl) && r == 0xff && g == 0x80 && bl == 0x01);
    CHECK(!ParseHexColor("ff80", &r, &g, &bl));
    CHECK(FormatHexColor(0xAB, 0x00, 0x0F) == "ab000f");
    CHECK(LeadingNumber(u8"100,1,1,直線移動,0", 0) == 100.0);
    CHECK(LeadingNumber("", 34) == 34.0);
    CHECK(IsExcludedTextKey(u8"テキスト") && !IsExcludedTextKey(u8"フォント"));

    // 6) 項目の分類と適用オプション
    CHECK(GroupOfTextKey(u8"サイズ") == ItemGroup::Size && GroupOfTextKey(u8"フォント") == ItemGroup::Look);
    CHECK(GroupOfDrawKey(u8"拡大率") == ItemGroup::Size && GroupOfDrawKey("Y") == ItemGroup::Position);
    CHECK(GroupOfDrawKey(u8"透明度") == ItemGroup::Ignore);
    ApplyOptions opt;
    CHECK(IsGroupEnabled(ItemGroup::Look, opt) && !IsGroupEnabled(ItemGroup::Size, opt));
    opt.position = true;
    CHECK(IsGroupEnabled(ItemGroup::Position, opt) && !IsGroupEnabled(ItemGroup::Ignore, opt));

    std::printf(g_fail == 0 ? "ALL OK\n" : "FAILED: %d\n", g_fail);
    return g_fail == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
