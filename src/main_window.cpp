//----------------------------------------------------------------------------------
//  「テキストテンプレ」パネル
//
//  画面構成:
//   [選択中に適用] [選択から登録] [上書き] [新規] [編集] [削除] [↑] [↓]   ← ボタン(幅に応じて折り返し)
//   [ ] サイズ・拡大率も適用   [ ] 位置も適用                          ← 適用オプション(都度切り替え)
//   ┌───────────────────────────────┐
//   │ 見出し        │  あAa漢 (プレビュー)          │ ← テンプレート一覧
//   │ 字幕          │  あAa漢                       │
//   └───────────────────────────────┘
//   状態表示(「3個のテキストに適用しました」など)
//----------------------------------------------------------------------------------
#include "main_window.h"

#include <commctrl.h>

#include <vector>

#include "edit_dialog.h"
#include "preview.h"
#include "template_store.h"
#include "text_object.h"
#include "util_win.h"

namespace {

constexpr LPCWSTR kClassName = L"TextTemplatePanel";

enum : int {
    CID_LIST = 1001,
    CID_APPLY,
    CID_CAPTURE,
    CID_NEW,
    CID_EDIT,
    CID_DELETE,
    CID_UP,
    CID_DOWN,
    CID_STATUS,
    CID_OVERWRITE,
    CID_OPT_SIZE,
    CID_OPT_POS,
};

// ボタン定義(ID と表示名)。並び順 = 画面の並び順
struct ButtonDef {
    int id;
    LPCWSTR text;
};
constexpr ButtonDef kButtons[] = {
    {CID_APPLY, L"選択中に適用"}, {CID_CAPTURE, L"選択から登録"}, {CID_OVERWRITE, L"上書き"},
    {CID_NEW, L"新規"},           {CID_EDIT, L"編集"},            {CID_DELETE, L"削除"},
    {CID_UP, L"↑"},               {CID_DOWN, L"↓"},
};

// 適用オプションのチェックボックス
constexpr ButtonDef kOptions[] = {
    {CID_OPT_SIZE, L"サイズ・拡大率も適用"},
    {CID_OPT_POS, L"位置も適用"},
};

// ---- パネル全体の状態(パネルは1つだけなのでファイル内グローバルで保持) ----
struct PanelState {
    EDIT_HANDLE* edit = nullptr;
    LOG_HANDLE* logger = nullptr;
    CONFIG_HANDLE* config = nullptr;
    std::wstring store_path;
    std::wstring settings_path;       // 適用オプションの保存先
    std::vector<TextTemplate> templates;
    std::vector<std::wstring> fonts;  // フォント一覧(初回の編集時に取得してキャッシュ)
    EffectTypeMap effect_types;       // エフェクト名 -> 種別(初回の適用/登録時に取得してキャッシュ)
    HWND hwnd = nullptr;
    HWND list = nullptr;
    HWND status = nullptr;
    HFONT ui_font = nullptr;
    double scale = 1.0;  // 画面拡大率(DPI)
};
PanelState g;

int S(int v) { return static_cast<int>(v * g.scale); }

// ---------------- 保存・読込 ----------------

void LoadTemplates() {
    std::string data;
    if (ReadWholeFile(g.store_path, &data)) g.templates = ParseTemplates(data);
}

void SaveTemplates() {
    if (!WriteWholeFile(g.store_path, SerializeTemplates(g.templates))) {
        if (g.logger) g.logger->error(g.logger, (L"テンプレートの保存に失敗: " + g.store_path).c_str());
    }
}

// 適用オプション(チェックボックスの状態)の保存・読込。書式: "size=0\r\nposition=1\r\n"
ApplyOptions ReadOptionsFromUi() {
    ApplyOptions o;
    o.size = IsDlgButtonChecked(g.hwnd, CID_OPT_SIZE) == BST_CHECKED;
    o.position = IsDlgButtonChecked(g.hwnd, CID_OPT_POS) == BST_CHECKED;
    return o;
}

void LoadOptions() {
    std::string data;
    if (!ReadWholeFile(g.settings_path, &data)) return;
    CheckDlgButton(g.hwnd, CID_OPT_SIZE, data.find("size=1") != std::string::npos ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(g.hwnd, CID_OPT_POS, data.find("position=1") != std::string::npos ? BST_CHECKED : BST_UNCHECKED);
}

void SaveOptions() {
    const ApplyOptions o = ReadOptionsFromUi();
    WriteWholeFile(g.settings_path, std::string("size=") + (o.size ? "1" : "0") + "\r\nposition=" +
                                        (o.position ? "1" : "0") + "\r\n");
}

// ---------------- 画面更新 ----------------

void SetStatus(const std::wstring& text) {
    SetWindowTextW(g.status, text.c_str());
}

// 一覧を templates の内容で作り直し、select 番目を選択状態にする
void RebuildList(int select) {
    SendMessageW(g.list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g.list, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < g.templates.size(); ++i) {
        // オーナードロー(LBS_HASSTRINGS 無し)なので、文字列の代わりに番号を項目データとして持たせる
        SendMessageW(g.list, LB_ADDSTRING, 0, static_cast<LPARAM>(i));
    }
    if (select >= 0 && select < static_cast<int>(g.templates.size())) SendMessageW(g.list, LB_SETCURSEL, select, 0);
    SendMessageW(g.list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g.list, nullptr, TRUE);
}

int CurrentIndex() {
    const LRESULT i = SendMessageW(g.list, LB_GETCURSEL, 0, 0);
    return (i == LB_ERR) ? -1 : static_cast<int>(i);
}

// ---------------- 各ボタンの処理 ----------------

// フォント一覧を AviUtl2 から取得(初回のみ)
const std::vector<std::wstring>& Fonts() {
    if (g.fonts.empty()) {
        g.edit->enum_font_name(&g.fonts, [](void* param, LPCWSTR name) {
            static_cast<std::vector<std::wstring>*>(param)->push_back(name);
        });
    }
    return g.fonts;
}

// エフェクト種別表を AviUtl2 から取得(初回のみ)
// ※編集セクションの外で呼ぶこと
const EffectTypeMap& EffectTypes() {
    if (g.effect_types.empty()) g.effect_types = BuildEffectTypeMap(g.edit);
    return g.effect_types;
}

// 選択中のテンプレートを、タイムラインで選択中のテキストへ適用
void OnApply() {
    const int idx = CurrentIndex();
    if (idx < 0) {
        SetStatus(L"テンプレートを選んでください");
        return;
    }
    // コールバックに渡す引数をまとめた構造体
    struct Ctx {
        const TextTemplate* tmpl;
        const EffectTypeMap* types;
        ApplyOptions opt;
        ApplyResult result;
    } ctx{&g.templates[idx], &EffectTypes(), ReadOptionsFromUi(), {}};

    // call_edit_section: AviUtl2 が編集ロックを取った状態でコールバックを呼ぶ。
    // コールバック内の変更はまとめて1回分の「元に戻す(Undo)」になる。
    const bool ok = g.edit->call_edit_section_param(&ctx, [](void* p, EDIT_SECTION* edit) {
        auto* c = static_cast<Ctx*>(p);
        c->result = ApplyTemplateToSelection(edit, *c->tmpl, *c->types, c->opt);
    });
    if (!ok) {
        SetStatus(L"今は編集できません(再生中・出力中など)");
        return;
    }
    const ApplyResult& r = ctx.result;
    std::wstring msg;
    if (r.applied == 0 && r.skipped == 0) {
        msg = L"オブジェクトが選択されていません";
    } else {
        msg = std::to_wstring(r.applied) + L"個のテキストに適用";
        if (r.skipped > 0) msg += L" / テキスト以外" + std::to_wstring(r.skipped) + L"個はスキップ";
        if (r.item_errors > 0) msg += L" / 設定できない項目" + std::to_wstring(r.item_errors) + L"件";
        if (r.filter_errors > 0) msg += L" / 追加できないフィルタ" + std::to_wstring(r.filter_errors) + L"件";
    }
    SetStatus(msg);
    if (g.logger) g.logger->log(g.logger, msg.c_str());
}

// テンプレートを編集ダイアログで編集し、OKなら index 番目に反映(index<0 なら末尾に追加)
void EditAndStore(TextTemplate tmpl, int index, LPCWSTR title) {
    if (!ShowTemplateEditDialog(g.edit->get_host_app_window(), Fonts(), title, &tmpl)) return;
    if (index < 0) {
        g.templates.push_back(tmpl);
        index = static_cast<int>(g.templates.size()) - 1;
    } else {
        g.templates[index] = tmpl;
    }
    SaveTemplates();
    RebuildList(index);
    SetStatus(L"保存しました");
}

// 選択中のテキストオブジェクトの見た目を読み取る(見つからなければ状態表示して false)
bool CaptureSelection(TextTemplate* out) {
    struct Ctx {
        const EffectTypeMap* types;
        TextTemplate* tmpl;
        bool found;
    } ctx{&EffectTypes(), out, false};
    // 読み取りだけなので call_read_section(参照ロック)を使う
    g.edit->call_read_section_param(&ctx, [](void* p, EDIT_SECTION* edit) {
        auto* c = static_cast<Ctx*>(p);
        c->found = CaptureTemplateFromSelection(edit, *c->types, c->tmpl);
    });
    if (!ctx.found) SetStatus(L"テキストオブジェクトを選択してから押してください");
    return ctx.found;
}

// 選択中のテキストオブジェクトの見た目を取り込んで新規テンプレートにする
void OnCapture() {
    TextTemplate t;
    if (!CaptureSelection(&t)) return;
    t.name = WideToUtf8(L"テンプレ" + std::to_wstring(g.templates.size() + 1));
    EditAndStore(t, -1, L"選択から登録");
}

// 一覧で選んだテンプレートを、選択中テキストの今の見た目で上書き保存する
// (名前と「既存フィルタを置き換える」の設定は引き継ぐ)
void OnOverwrite() {
    const int idx = CurrentIndex();
    if (idx < 0) {
        SetStatus(L"上書きするテンプレートを選んでください");
        return;
    }
    TextTemplate t;
    if (!CaptureSelection(&t)) return;
    const std::wstring msg = L"「" + Utf8ToWide(g.templates[idx].name) + L"」を、選択中テキストの見た目で上書きしますか？";
    if (MessageBoxW(g.hwnd, msg.c_str(), L"テキストテンプレ", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    t.name = g.templates[idx].name;
    t.replace_filters = g.templates[idx].replace_filters;
    g.templates[idx] = t;
    SaveTemplates();
    RebuildList(idx);
    SetStatus(L"上書き保存しました");
}

void OnNew() {
    TextTemplate t;
    t.name = WideToUtf8(L"テンプレ" + std::to_wstring(g.templates.size() + 1));
    // 初期値。フィルタは空(フィルタ付きのテンプレートは「選択から登録」で作る)
    t.replace_filters = false;  // フィルタ無しのテンプレートで既存フィルタを消さないように
    SetItem(&t.text_items, text_key::kFont, "Yu Gothic UI");
    SetItem(&t.text_items, text_key::kColor, "ffffff");
    SetItem(&t.text_items, text_key::kEdge, "000000");
    SetItem(&t.text_items, text_key::kDeco, DecorationCandidates().front());
    EditAndStore(t, -1, L"新規テンプレート");
}

void OnEdit() {
    const int idx = CurrentIndex();
    if (idx >= 0) EditAndStore(g.templates[idx], idx, L"テンプレートの編集");
}

void OnDelete() {
    const int idx = CurrentIndex();
    if (idx < 0) return;
    const std::wstring msg = L"「" + Utf8ToWide(g.templates[idx].name) + L"」を削除しますか？";
    if (MessageBoxW(g.hwnd, msg.c_str(), L"テキストテンプレ", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    g.templates.erase(g.templates.begin() + idx);
    SaveTemplates();
    RebuildList(idx < static_cast<int>(g.templates.size()) ? idx : idx - 1);
}

// 並べ替え(delta = -1:上へ / +1:下へ)
void OnMove(int delta) {
    const int idx = CurrentIndex();
    const int to = idx + delta;
    if (idx < 0 || to < 0 || to >= static_cast<int>(g.templates.size())) return;
    std::swap(g.templates[idx], g.templates[to]);
    SaveTemplates();
    RebuildList(to);
}

// ---------------- 描画・レイアウト ----------------

// 一覧の1行を描く(左: 名前 / 右: プレビュー)
void DrawListItem(const DRAWITEMSTRUCT* dis) {
    if (dis->itemID == static_cast<UINT>(-1)) return;  // 空の一覧
    const auto idx = static_cast<size_t>(dis->itemData);
    if (idx >= g.templates.size()) return;
    const TextTemplate& t = g.templates[idx];
    const bool selected = (dis->itemState & ODS_SELECTED) != 0;

    // 背景(選択中は強調色)
    HBRUSH bg = GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW);
    FillRect(dis->hDC, &dis->rcItem, bg);

    // 名前(左側 40%)
    RECT name_rc = dis->rcItem;
    name_rc.left += S(6);
    name_rc.right = dis->rcItem.left + (dis->rcItem.right - dis->rcItem.left) * 4 / 10;
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
    HGDIOBJ old = SelectObject(dis->hDC, g.ui_font);
    // 上段: テンプレート名 / 下段: フィルタ構成(例 "縁取り×2 / グラデーション")
    const std::wstring name = Utf8ToWide(t.name);
    const std::wstring summary = FilterSummary(t);
    RECT top = name_rc, bottom = name_rc;
    top.bottom = summary.empty() ? name_rc.bottom : (name_rc.top + name_rc.bottom) / 2 + S(2);
    bottom.top = top.bottom;
    const UINT fmt = DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX;
    DrawTextW(dis->hDC, name.c_str(), -1, &top, fmt | (summary.empty() ? DT_VCENTER : DT_BOTTOM));
    if (!summary.empty()) {
        SetTextColor(dis->hDC, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_GRAYTEXT));
        DrawTextW(dis->hDC, summary.c_str(), -1, &bottom, fmt | DT_TOP);
    }
    SelectObject(dis->hDC, old);

    // プレビュー(右側 60%、上下左右に少し余白)
    RECT pv = dis->rcItem;
    pv.left = name_rc.right + S(4);
    pv.right -= S(4);
    pv.top += S(3);
    pv.bottom -= S(3);
    DrawTemplatePreview(dis->hDC, pv, t);

    if (dis->itemState & ODS_FOCUS) DrawFocusRect(dis->hDC, &dis->rcItem);
}

// ウィンドウサイズに合わせて部品を並べ直す
void Layout(int width, int height) {
    const int margin = S(6), gap = S(4);
    int btn_h = g.config ? g.config->get_layout_size(g.config, "SettingItemHeight") : 0;
    if (btn_h <= 0) btn_h = S(24);

    // ボタンを左から並べ、右端を超えたら次の行へ折り返す
    int x = margin, y = margin;
    HDC hdc = GetDC(g.hwnd);
    HGDIOBJ old = SelectObject(hdc, g.ui_font);
    for (const auto& b : kButtons) {
        SIZE sz;
        GetTextExtentPoint32W(hdc, b.text, lstrlenW(b.text), &sz);
        const int w = sz.cx + S(16);
        if (x + w > width - margin && x > margin) {
            x = margin;
            y += btn_h + gap;
        }
        MoveWindow(GetDlgItem(g.hwnd, b.id), x, y, w, btn_h, TRUE);
        x += w + gap;
    }
    SelectObject(hdc, old);
    ReleaseDC(g.hwnd, hdc);
    y += btn_h + gap;

    // 適用オプションのチェックボックス(こちらも折り返し)
    x = margin;
    hdc = GetDC(g.hwnd);
    old = SelectObject(hdc, g.ui_font);
    for (const auto& o : kOptions) {
        SIZE sz;
        GetTextExtentPoint32W(hdc, o.text, lstrlenW(o.text), &sz);
        const int w = sz.cx + S(28);  // チェック枠の分を足す
        if (x + w > width - margin && x > margin) {
            x = margin;
            y += btn_h + gap;
        }
        MoveWindow(GetDlgItem(g.hwnd, o.id), x, y, w, btn_h, TRUE);
        x += w + S(12);
    }
    SelectObject(hdc, old);
    ReleaseDC(g.hwnd, hdc);
    y += btn_h + gap;

    const int status_h = S(20);
    const int list_h = height - y - status_h - margin - gap;
    MoveWindow(g.list, margin, y, width - margin * 2, list_h > 0 ? list_h : 0, TRUE);
    MoveWindow(g.status, margin, height - margin - status_h, width - margin * 2, status_h, TRUE);
}

LRESULT CALLBACK PanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            Layout(LOWORD(lp), HIWORD(lp));
            return 0;
        case WM_MEASUREITEM: {
            // 一覧の1行の高さ
            auto* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
            if (mis->CtlID == CID_LIST) {
                mis->itemHeight = S(52);
                return TRUE;
            }
            break;
        }
        case WM_DRAWITEM: {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if (dis->CtlID == CID_LIST) {
                DrawListItem(dis);
                return TRUE;
            }
            break;
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case CID_APPLY:   OnApply();   break;
                case CID_CAPTURE: OnCapture(); break;
                case CID_OVERWRITE: OnOverwrite(); break;
                case CID_OPT_SIZE:
                case CID_OPT_POS:   SaveOptions(); break;
                case CID_NEW:     OnNew();     break;
                case CID_EDIT:    OnEdit();    break;
                case CID_DELETE:  OnDelete();  break;
                case CID_UP:      OnMove(-1);  break;
                case CID_DOWN:    OnMove(+1);  break;
                case CID_LIST:
                    // 一覧のダブルクリック = 適用
                    if (HIWORD(wp) == LBN_DBLCLK) OnApply();
                    return 0;
                default:
                    return 0;
            }
            // ボタンにフォーカスが残ると AviUtl2 のショートカットキーが効かなくなるので外す(SDKサンプルと同じ対処)
            SetFocus(nullptr);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

HWND CreateTemplatePanel(EDIT_HANDLE* edit, LOG_HANDLE* logger, CONFIG_HANDLE* config, const std::wstring& store_path) {
    g.edit = edit;
    g.logger = logger;
    g.config = config;
    g.store_path = store_path;
    // 保存先と同じフォルダに settings.ini を置く
    g.settings_path = store_path.substr(0, store_path.find_last_of(L'\\') + 1) + L"settings.ini";

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = PanelProc;
    wc.hInstance = ThisModule();
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) return nullptr;

    // 親無しでは WS_CHILD を作れないので一旦 WS_POPUP で作る(register_window_client で子ウィンドウ化される)
    g.hwnd = CreateWindowExW(0, kClassName, L"テキストテンプレ", WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, 400, 300,
                             nullptr, nullptr, ThisModule(), nullptr);
    if (!g.hwnd) return nullptr;

    // DPI(画面拡大率)と UI フォント
    HDC hdc = GetDC(g.hwnd);
    g.scale = GetDeviceCaps(hdc, LOGPIXELSY) / 96.0;
    ReleaseDC(g.hwnd, hdc);
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    g.ui_font = CreateFontIndirectW(&ncm.lfMessageFont);

    // 部品作成(位置は Layout() で決めるので仮の 0)
    auto make = [](LPCWSTR cls, LPCWSTR text, DWORD style, int id) {
        HWND h = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, g.hwnd,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), ThisModule(), nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g.ui_font), FALSE);
        return h;
    };
    for (const auto& b : kButtons) make(WC_BUTTONW, b.text, BS_PUSHBUTTON, b.id);
    for (const auto& o : kOptions) make(WC_BUTTONW, o.text, BS_AUTOCHECKBOX, o.id);
    // LBS_OWNERDRAWFIXED: 各行を自分で描く / LBS_NOINTEGRALHEIGHT: 高さを行単位に丸めない
    g.list = make(WC_LISTBOXW, L"", WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_NOINTEGRALHEIGHT,
                  CID_LIST);
    g.status = make(WC_STATICW, L"", SS_LEFT | SS_ENDELLIPSIS, CID_STATUS);

    LoadOptions();
    LoadTemplates();
    RebuildList(g.templates.empty() ? -1 : 0);
    SetStatus(g.templates.empty() ? L"「新規」か「選択から登録」でテンプレートを作成" : L"");
    return g.hwnd;
}
