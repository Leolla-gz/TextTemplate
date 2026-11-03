//----------------------------------------------------------------------------------
//  疑似ホスト(AviUtl2 の代わり)による結合テスト
//
//  TextTemplate.aux2 を LoadLibrary で読み込み、AviUtl2 と同じ順番で関数を呼び、
//  偽物のオブジェクト(テキスト2個+画像1個)に対して「選択中に適用」「選択から登録」を実行して
//  結果を検証する。Wine 上でも動く(実機の AviUtl2 の挙動そのものは検証できない点に注意)。
//
//  使い方: fake_host.exe <TextTemplate.aux2 のパス> <作業フォルダ> [画面画像の出力先.bmp]
//----------------------------------------------------------------------------------
#include <windows.h>

#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "config2.h"
#include "logger2.h"
#include "plugin2.h"

// ---- 偽オブジェクト ----
struct FakeEffect {
    std::wstring name;
    bool enabled = true;
    std::vector<std::pair<std::wstring, std::string>> items;  // 項目名 -> 値(順序保持)
    std::string* Find(const std::wstring& k) {
        for (auto& kv : items) if (kv.first == k) return &kv.second;
        return nullptr;
    }
};
struct FakeObject {
    std::vector<std::shared_ptr<FakeEffect>> effects;  // [0]=入力(テキスト等) [1]=標準描画 [2..]=フィルタ
};
static std::vector<FakeObject> g_objects;
static std::vector<int> g_selected;  // 選択中オブジェクトの番号
static std::string g_ret;            // 文字列返却用のバッファ
static int g_fail = 0;
static HWND g_panel = nullptr;
// エフェクト種別(実機では enum_effect_name で取得される)
static const std::map<std::wstring, int> kTypes = {
    {L"テキスト", 2}, {L"画像ファイル", 2}, {L"標準描画", 5},
    {L"縁取り", 1},   {L"グラデーション", 1}, {L"フェード", 1}, {L"ぼかし", 1},
};
// 実機で作れるフィルタの既定項目(create_effect 直後の状態)
static FakeEffect DefaultEffect(const std::wstring& name) {
    FakeEffect e{name, true, {}};
    if (name == L"縁取り") e.items = {{L"サイズ", "3"}, {L"ぼかし", "10"}, {L"縁色", "000000"}};
    if (name == L"グラデーション") e.items = {{L"強さ", "100.0"}, {L"角度", "0.00"}, {L"幅", "100"}, {L"開始色", "ffffff"}, {L"終了色", "000000"}};
    if (name == L"フェード") e.items = {{L"イン", "0.50"}, {L"アウト", "0.50"}};
    return e;
}

#define CHECK(c) do { if (!(c)) { std::printf("NG: %s (line %d)\n", #c, __LINE__); ++g_fail; } } while (0)

static FakeObject* Obj(OBJECT_HANDLE h) { return static_cast<FakeObject*>(h); }
static FakeEffect* Eff(EFFECT_HANDLE h) { return static_cast<FakeEffect*>(h); }
static std::string U8(const std::wstring& w) {
    std::string s(WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), (int)s.size(), nullptr, nullptr);
    s.pop_back();
    return s;
}
static FakeEffect* FindEffect(OBJECT_HANDLE o, LPCWSTR name) {
    for (auto& e : Obj(o)->effects) if (e->name == name) return e.get();
    return nullptr;
}

// ---- EDIT_SECTION の偽実装 ----
static int  F_sel_num() { return static_cast<int>(g_selected.size()); }
static OBJECT_HANDLE F_sel(int i) { return (i >= 0 && i < (int)g_selected.size()) ? &g_objects[g_selected[i]] : nullptr; }
static OBJECT_HANDLE F_focus() { return nullptr; }
static int  F_count(OBJECT_HANDLE o, LPCWSTR eff) {
    int n = 0;
    for (auto& e : Obj(o)->effects) n += (e->name == eff);
    return n;
}
static bool F_set(OBJECT_HANDLE o, LPCWSTR eff, LPCWSTR item, LPCSTR v) {
    FakeEffect* e = FindEffect(o, eff);
    std::string* p = e ? e->Find(item) : nullptr;
    if (!p) return false;  // 実機同様、存在しない項目名は失敗
    *p = v;
    return true;
}
static LPCSTR F_get(OBJECT_HANDLE o, LPCWSTR eff, LPCWSTR item) {
    FakeEffect* e = FindEffect(o, eff);
    std::string* p = e ? e->Find(item) : nullptr;
    if (!p) return nullptr;
    g_ret = *p;
    return g_ret.c_str();
}
static LPCSTR F_alias(OBJECT_HANDLE o) {
    // .object と同じ書式で出力
    g_ret = "[Object]\r\nlayer=0\r\nframe=0,99\r\n";
    int i = 0;
    for (auto& e : Obj(o)->effects) {
        g_ret += "[Object." + std::to_string(i++) + "]\r\neffect.name=" + U8(e->name) + "\r\n";
        if (!e->enabled) g_ret += "effect.disable=1\r\n";
        for (auto& kv : e->items) g_ret += U8(kv.first) + "=" + kv.second + "\r\n";
    }
    return g_ret.c_str();
}
static int F_effect_list(OBJECT_HANDLE o, EFFECT_HANDLE* list, int n) {
    const int total = (int)Obj(o)->effects.size();
    if (!list) return total;
    int i = 0;
    for (; i < n && i < total; ++i) list[i] = Obj(o)->effects[i].get();
    return i;
}
static LPCWSTR F_effect_name(EFFECT_HANDLE e) { return Eff(e)->name.c_str(); }
static bool F_delete_effect(OBJECT_HANDLE o, EFFECT_HANDLE e) {
    auto& v = Obj(o)->effects;
    for (auto it = v.begin(); it != v.end(); ++it) if (it->get() == e) { v.erase(it); return true; }
    return false;
}
static EFFECT_HANDLE F_create_effect(OBJECT_HANDLE o, LPCWSTR name) {
    auto it = kTypes.find(name);
    if (it == kTypes.end() || it->second != 1) return nullptr;  // 未知のフィルタは失敗
    Obj(o)->effects.push_back(std::make_shared<FakeEffect>(DefaultEffect(name)));
    return Obj(o)->effects.back().get();
}
static bool F_set_effect_item(EFFECT_HANDLE e, LPCWSTR item, LPCSTR v) {
    std::string* p = Eff(e)->Find(item);
    if (!p) return false;
    *p = v;
    return true;
}
static void F_set_effect_enable(EFFECT_HANDLE e, bool en) { Eff(e)->enabled = en; }

static EDIT_SECTION g_section = {};

// ---- EDIT_HANDLE の偽実装 ----
static bool F_call_edit(void* p, void (*f)(void*, EDIT_SECTION*)) { f(p, &g_section); return true; }
static void F_enum_font(void* p, void (*f)(void*, LPCWSTR)) { f(p, L"Noto Sans CJK JP"); f(p, L"MS Gothic"); }
static void F_enum_effect(void* p, void (*f)(void*, LPCWSTR, int, int)) { for (auto& kv : kTypes) f(p, kv.first.c_str(), kv.second, 0); }
static HWND F_host_window() { return nullptr; }
static EDIT_HANDLE g_edit = {};

// ---- HOST_APP_TABLE の偽実装 ----
static EDIT_HANDLE* F_create_edit() { return &g_edit; }
static void F_register_window(LPCWSTR, HWND hwnd) { g_panel = hwnd; }

// ---- LOG / CONFIG ----
static void F_log(LOG_HANDLE*, LPCWSTR m) { std::printf("[log] %s\n", U8(m).c_str()); }
static int F_layout(CONFIG_HANDLE*, LPCSTR) { return 0; }

// ウィンドウ内容をBMPファイルに保存(画面確認用)
static void SaveBmp(HWND hwnd, const wchar_t* path) {
    RECT rc; GetWindowRect(hwnd, &rc);
    const int w = rc.right - rc.left, h = rc.bottom - rc.top;
    HDC wdc = GetDC(nullptr);
    HDC mdc = CreateCompatibleDC(wdc);
    BITMAPINFO bi = {}; bi.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 24, BI_RGB};
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(wdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    SelectObject(mdc, bmp);
    // 子ウィンドウも含めて今すぐ描画させてから、画面(仮想ディスプレイ)の該当範囲をコピー
    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN | RDW_FRAME);
    BitBlt(mdc, 0, 0, w, h, wdc, rc.left, rc.top, SRCCOPY);
    GdiFlush();
    const int stride = ((w * 3 + 3) & ~3);
    BITMAPFILEHEADER fh = {0x4D42, (DWORD)(sizeof(fh) + sizeof(BITMAPINFOHEADER) + stride * h), 0, 0,
                           sizeof(fh) + sizeof(BITMAPINFOHEADER)};
    FILE* fp = _wfopen(path, L"wb");
    if (fp) {
        fwrite(&fh, sizeof(fh), 1, fp);
        fwrite(&bi.bmiHeader, sizeof(BITMAPINFOHEADER), 1, fp);
        fwrite(bits, stride * h, 1, fp);
        fclose(fp);
    }
    DeleteObject(bmp); DeleteDC(mdc); ReleaseDC(nullptr, wdc);
}

static void Pump() {
    MSG m;
    while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
}

static std::string StatusText() {
    wchar_t buf[256] = {};
    GetWindowTextW(GetDlgItem(g_panel, 1009), buf, 256);
    return U8(buf);
}

static FakeObject MakeText(const char* font, const char* color, const char* text) {
    FakeObject o;
    auto t = std::make_shared<FakeEffect>();
    t->name = L"テキスト";
    t->items = {{L"サイズ", "100,1,1,直線移動,0"}, {L"字間", "0.00"}, {L"フォント", font}, {L"文字色", color},
                {L"影・縁色", "000000"}, {L"文字装飾", "標準文字"}, {L"B", "0"}, {L"I", "0"}, {L"テキスト", text},
                {L"オブジェクトの長さを自動調節", "0"}};
    o.effects.push_back(t);
    o.effects.push_back(std::make_shared<FakeEffect>(FakeEffect{L"標準描画", true,
        {{L"X", "120.00"}, {L"Y", "300.00"}, {L"Group", "1"}, {L"拡大率", "100.000"}, {L"透明度", "0.00"}}}));
    o.effects.push_back(std::make_shared<FakeEffect>(DefaultEffect(L"フェード")));
    return o;
}

static std::vector<std::wstring> Names(const FakeObject& o) {
    std::vector<std::wstring> v;
    for (auto& e : o.effects) v.push_back(e->name);
    return v;
}

// ダイアログが開いたら(画像保存して)OK を押すタイマーを仕掛ける
static const wchar_t* g_dialog_shot = nullptr;
static void ArmDialogOk() {
    SetTimer(nullptr, 0, 300, [](HWND, UINT, UINT_PTR id, DWORD) {
        KillTimer(nullptr, id);
        HWND dlg = FindWindowW(L"TextTemplateEditDialog", nullptr);
        if (!dlg) { std::printf("NG: dialog not found\n"); ++g_fail; return; }
        if (g_dialog_shot) {
            SetWindowPos(dlg, HWND_TOP, 0, 0, 0, 0, SWP_NOSIZE);  // 画面内に移動してから撮影
            Pump();
            SaveBmp(dlg, g_dialog_shot);
        }
        SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
    });
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { std::printf("usage: fake_host <aux2> <workdir> [shot.bmp]\n"); return 2; }
    const std::wstring workdir = argv[2];

    // テンプレートを事前に用意: [0]旧形式(v0.1) [1]新形式・多重縁取り+グラデーション
    CreateDirectoryW((workdir + L"\\Plugin").c_str(), nullptr);
    CreateDirectoryW((workdir + L"\\Plugin\\TextTemplate").c_str(), nullptr);
    {
        FILE* fp = _wfopen((workdir + L"\\Plugin\\TextTemplate\\templates.ini").c_str(), L"wb");
        fputs("[Template]\nname=見出し(旧形式)\nフォント=MS Gothic\n文字色=ffcc00\n影・縁色=202060\n文字装飾=縁取り文字\nB=1\nI=0\nサイズ=\n"
              "[Template]\nname=字幕(二重縁+グラデ)\nreplace_filters=1\ntext.フォント=Noto Sans CJK JP\ntext.文字色=ffffff\n"
              "text.文字装飾=標準文字\ntext.B=1\ntext.サイズ=48.00\n"
              "draw.effect=標準描画\ndraw.X=0.00\ndraw.Y=400.00\ndraw.拡大率=150.000\n"
              "[Template.filter]\neffect.name=グラデーション\n角度=0.00\n幅=60\n開始色=fff27a\n終了色=ff7a2e\n"
              "[Template.filter]\neffect.name=縁取り\nサイズ=4\n縁色=ffffff\n"
              "[Template.filter]\neffect.name=縁取り\nサイズ=6\n縁色=e0457b\n", fp);
        fclose(fp);
    }

    // 偽オブジェクト: [0]テキスト [1]画像 [2]テキスト
    FakeObject image;
    image.effects.push_back(std::make_shared<FakeEffect>(FakeEffect{L"画像ファイル", true, {}}));
    image.effects.push_back(std::make_shared<FakeEffect>(FakeEffect{L"標準描画", true, {}}));
    g_objects = {MakeText("MS UI Gothic", "ffffff", "こんにちは"), image, MakeText("Meiryo", "00ff00", "よろしく")};
    g_selected = {0, 1, 2};

    g_section.get_selected_object_num = F_sel_num;
    g_section.get_selected_object = F_sel;
    g_section.get_focus_object = F_focus;
    g_section.count_object_effect = F_count;
    g_section.set_object_item_value = F_set;
    g_section.get_object_item_value = F_get;
    g_section.get_object_alias = F_alias;
    g_section.get_effect_list = F_effect_list;
    g_section.get_effect_name = F_effect_name;
    g_section.delete_effect = F_delete_effect;
    g_section.create_effect = F_create_effect;
    g_section.set_effect_item_value = F_set_effect_item;
    g_section.set_effect_enable = F_set_effect_enable;
    g_edit.call_edit_section_param = F_call_edit;
    g_edit.call_read_section_param = F_call_edit;
    g_edit.enum_font_name = F_enum_font;
    g_edit.enum_effect_name = F_enum_effect;
    g_edit.get_host_app_window = F_host_window;
    HOST_APP_TABLE host = {};
    host.create_edit_handle = F_create_edit;
    host.register_window_client = F_register_window;
    LOG_HANDLE logger = {F_log, F_log, F_log, F_log};
    CONFIG_HANDLE config = {};
    config.app_data_path = workdir.c_str();
    config.get_layout_size = F_layout;

    // ---- AviUtl2 と同じ順に呼ぶ ----
    HMODULE dll = LoadLibraryW(argv[1]);
    CHECK(dll != nullptr);
    if (!dll) return 1;
    auto get = [&](const char* n) { return GetProcAddress(dll, n); };
    CHECK(get("RegisterPlugin") && get("GetCommonPluginTable") && get("RequiredVersion"));
    reinterpret_cast<void (*)(LOG_HANDLE*)>(get("InitializeLogger"))(&logger);
    reinterpret_cast<void (*)(CONFIG_HANDLE*)>(get("InitializeConfig"))(&config);
    CHECK(reinterpret_cast<bool (*)(DWORD)>(get("InitializePlugin"))(2003300));
    reinterpret_cast<void (*)(HOST_APP_TABLE*)>(get("RegisterPlugin"))(&host);
    CHECK(g_panel != nullptr);
    if (!g_panel) return 1;

    // 実機では AviUtl2 が子ウィンドウとして配置する。ここでは単独表示
    SetWindowPos(g_panel, nullptr, 50, 50, 460, 300, SWP_NOZORDER | SWP_SHOWWINDOW);
    Pump();

    HWND list = GetDlgItem(g_panel, 1001);
    CHECK(SendMessageW(list, LB_GETCOUNT, 0, 0) == 2);
    FakeEffect& text0 = *g_objects[0].effects[0];

    // ---- テスト1: 旧形式テンプレート → テキスト設定のみ反映、既存フィルタ(フェード)は残る ----
    SendMessageW(list, LB_SETCURSEL, 0, 0);
    SendMessageW(g_panel, WM_COMMAND, MAKEWPARAM(1002, BN_CLICKED), 0);
    std::printf("status1: %s\n", StatusText().c_str());
    CHECK(*text0.Find(L"フォント") == "MS Gothic");
    CHECK(*text0.Find(L"文字装飾") == "縁取り文字");
    CHECK(*text0.Find(L"サイズ") == "100,1,1,直線移動,0");  // 空 → 動きを保持
    CHECK(Names(g_objects[0]) == (std::vector<std::wstring>{L"テキスト", L"標準描画", L"フェード"}));
    CHECK(StatusText().find("2個のテキストに適用") != std::string::npos);

    // ---- テスト2: 二重縁+グラデ → 既存フィルタを置き換え、順序どおり追加 ----
    SendMessageW(list, LB_SETCURSEL, 1, 0);
    SendMessageW(g_panel, WM_COMMAND, MAKEWPARAM(1002, BN_CLICKED), 0);
    std::printf("status2: %s\n", StatusText().c_str());
    for (int i : {0, 2}) {
        const auto& o = g_objects[i];
        CHECK(Names(o) == (std::vector<std::wstring>{L"テキスト", L"標準描画", L"グラデーション", L"縁取り", L"縁取り"}));
        if (o.effects.size() == 5) {
            CHECK(*o.effects[2]->Find(L"開始色") == "fff27a");
            CHECK(*o.effects[3]->Find(L"サイズ") == "4" && *o.effects[3]->Find(L"ぼかし") == "10");  // 未指定項目は既定値のまま
            CHECK(*o.effects[4]->Find(L"縁色") == "e0457b");
            CHECK(*o.effects[1]->Find(L"X") == "120.00");  // 標準描画(位置)は変えない
        }
    }
    // オプション未チェック → サイズ・拡大率・位置は変えない
    CHECK(*text0.Find(L"サイズ") == "100,1,1,直線移動,0");
    CHECK(*g_objects[0].effects[1]->Find(L"拡大率") == "100.000");
    CHECK(*text0.Find(L"テキスト") == "こんにちは");          // 本文は変えない
    CHECK(*g_objects[2].effects[0]->Find(L"テキスト") == "よろしく");
    CHECK(g_objects[1].effects.size() == 2);                    // 画像は変更なし

    // ---- テスト2b: 「サイズ・拡大率も適用」だけオン → サイズ系のみ反映、位置は不変 ----
    CheckDlgButton(g_panel, 1011, BST_CHECKED);
    SendMessageW(g_panel, WM_COMMAND, MAKEWPARAM(1011, BN_CLICKED), 0);  // 設定保存
    SendMessageW(g_panel, WM_COMMAND, MAKEWPARAM(1002, BN_CLICKED), 0);
    CHECK(*text0.Find(L"サイズ") == "48.00");
    CHECK(*g_objects[0].effects[1]->Find(L"拡大率") == "150.000");
    CHECK(*g_objects[0].effects[1]->Find(L"X") == "120.00");
    // ---- テスト2c: 「位置も適用」もオン → 位置も反映 ----
    CheckDlgButton(g_panel, 1012, BST_CHECKED);
    SendMessageW(g_panel, WM_COMMAND, MAKEWPARAM(1012, BN_CLICKED), 0);
    SendMessageW(g_panel, WM_COMMAND, MAKEWPARAM(1002, BN_CLICKED), 0);
    CHECK(*g_objects[2].effects[1]->Find(L"X") == "0.00" && *g_objects[2].effects[1]->Find(L"Y") == "400.00");
    CHECK(*g_objects[2].effects[1]->Find(L"透明度") == "0.00");
    std::printf("status2c: %s\n", StatusText().c_str());
    CHECK(StatusText().find("設定できない") == std::string::npos);  // エラー無し
    {
        FILE* fp = _wfopen((workdir + L"\\Plugin\\TextTemplate\\settings.ini").c_str(), L"rb");
        char buf[64] = {};
        if (fp) { fread(buf, 1, sizeof(buf) - 1, fp); fclose(fp); }
        CHECK(std::string(buf).find("size=1") != std::string::npos && std::string(buf).find("position=1") != std::string::npos);
    }

    // ---- テスト3: 選択なし ----
    g_selected.clear();
    SendMessageW(g_panel, WM_COMMAND, MAKEWPARAM(1002, BN_CLICKED), 0);
    CHECK(StatusText().find("選択されていません") != std::string::npos);

    // ---- テスト4: 「選択から登録」→ テキスト設定+フィルタを丸ごと取り込み ----
    *g_objects[2].effects[0]->Find(L"文字色") = "123456";
    g_objects[2].effects[4]->enabled = false;  // 外側の縁を無効化しておく
    g_selected = {1, 2};  // 先頭は画像。テキストの[2]から取り込まれるはず
    g_dialog_shot = argc >= 4 ? (new std::wstring(std::wstring(argv[3]) + L".dialog.bmp"))->c_str() : nullptr;
    ArmDialogOk();
    SendMessageW(g_panel, WM_COMMAND, MAKEWPARAM(1003, BN_CLICKED), 0);  // ダイアログが閉じるまで戻らない
    CHECK(SendMessageW(list, LB_GETCOUNT, 0, 0) == 3);
    {
        FILE* fp = _wfopen((workdir + L"\\Plugin\\TextTemplate\\templates.ini").c_str(), L"rb");
        std::string data(16384, '\0');
        data.resize(fread(data.data(), 1, data.size(), fp));
        fclose(fp);
        const auto pos = data.find("name=テンプレ3");
        CHECK(pos != std::string::npos);
        const std::string t3 = pos == std::string::npos ? "" : data.substr(pos);
        CHECK(t3.find("text.文字色=123456") != std::string::npos);
        CHECK(t3.find("text.字間=0.00") != std::string::npos);          // 画面に無い項目も取り込む
        CHECK(t3.find("text.テキスト=") == std::string::npos);           // 本文は取り込まない
        CHECK(t3.find("text.オブジェクトの長さ") == std::string::npos);
        CHECK(t3.find("effect.name=標準描画") == std::string::npos);     // 位置は取り込まない
        CHECK(t3.find("effect.disable=1") != std::string::npos);         // 無効状態も保持
        CHECK(t3.find("replace_filters=1") != std::string::npos);
    }

    // ---- テスト5: 「上書き」→ 先頭テンプレートを選択中テキストの見た目で更新(名前は維持) ----
    *g_objects[2].effects[0]->Find(L"フォント") = "Overwritten Font";
    *g_objects[2].effects[1]->Find(L"拡大率") = "222.000";
    g_selected = {2};
    SendMessageW(list, LB_SETCURSEL, 0, 0);
    SetTimer(nullptr, 0, 300, [](HWND, UINT, UINT_PTR id, DWORD) {
        KillTimer(nullptr, id);
        HWND box = FindWindowW(L"#32770", nullptr);  // 確認メッセージボックス
        if (!box) { std::printf("NG: confirm box not found\n"); ++g_fail; return; }
        PostMessageW(box, WM_COMMAND, MAKEWPARAM(IDYES, BN_CLICKED), 0);
    });
    SendMessageW(g_panel, WM_COMMAND, MAKEWPARAM(1010, BN_CLICKED), 0);
    std::printf("status5: %s\n", StatusText().c_str());
    CHECK(SendMessageW(list, LB_GETCOUNT, 0, 0) == 3);
    {
        FILE* fp = _wfopen((workdir + L"\\Plugin\\TextTemplate\\templates.ini").c_str(), L"rb");
        std::string data(16384, '\0');
        data.resize(fread(data.data(), 1, data.size(), fp));
        fclose(fp);
        const auto first = data.find("[Template]");
        const auto second = data.find("[Template]", first + 1);
        const std::string t1 = data.substr(first, second - first);
        CHECK(t1.find("name=見出し(旧形式)") != std::string::npos);   // 名前は維持
        CHECK(t1.find("text.フォント=Overwritten Font") != std::string::npos);
        CHECK(t1.find("draw.拡大率=222.000") != std::string::npos);
        CHECK(t1.find("draw.透明度") == std::string::npos && t1.find("draw.Group") == std::string::npos);
        CHECK(t1.find("replace_filters=0") != std::string::npos);  // 置き換え設定は維持
    }

    // 画面画像
    SendMessageW(list, LB_SETCURSEL, 1, 0);
    InvalidateRect(g_panel, nullptr, TRUE);
    Pump();
    if (argc >= 4) SaveBmp(g_panel, argv[3]);

    std::printf(g_fail == 0 ? "FAKE HOST: ALL OK\n" : "FAKE HOST: FAILED %d\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
