//----------------------------------------------------------------------------------
//  テンプレート編集ダイアログ
//
//  画面構成:
//   名前 / フォント / 文字色 / 影・縁色 / 文字装飾 / B・I / サイズ   ← テキスト設定(空欄=適用しない)
//   その他のテキスト設定: 字間, 行間 …                              ← 取り込んだ項目(表示のみ・保持される)
//   フィルタ一覧(チェックを外すとテンプレートから除外)              ← 縁取り×2、グラデーション 等
//   [x] 適用時に既存のフィルタを置き換える
//   プレビュー「あAa漢」
//----------------------------------------------------------------------------------
#include "edit_dialog.h"

#include <commctrl.h>
#include <commdlg.h>

#include "preview.h"
#include "util_win.h"

namespace {

constexpr LPCWSTR kClassName = L"TextTemplateEditDialog";

// 部品(コントロール)ID ※IDC_ で始めると Windows の定義(IDC_SIZE=カーソル等)と衝突するため CID_ にする
enum : int {
    CID_NAME = 2001,
    CID_FONT,
    CID_COLOR,
    CID_COLOR_BTN,
    CID_EDGE,
    CID_EDGE_BTN,
    CID_DECO,
    CID_BOLD,
    CID_ITALIC,
    CID_SIZE,
    CID_OTHERS,
    CID_FILTERS,
    CID_REPLACE,
    CID_PREVIEW,
};

// コンボボックス選択変更後にプレビューを更新するための遅延メッセージ
// (選択変更の通知時点では、まだ入力欄の文字が切り替わっていないため)
constexpr UINT WM_APP_REFRESH = WM_APP + 1;

// ダイアログで直接編集するテキスト項目(入力欄ID と 項目名)
struct TextField {
    int id;
    const char* key;
};
constexpr TextField kTextFields[] = {
    {CID_FONT, text_key::kFont}, {CID_COLOR, text_key::kColor}, {CID_EDGE, text_key::kEdge},
    {CID_DECO, text_key::kDeco}, {CID_SIZE, text_key::kSize},
};

// ダイアログ1回分の状態(ウィンドウの USERDATA に保持)
struct DialogState {
    TextTemplate base;                 // 開いた時点の値(画面に無い項目・フィルタの元データ)
    TextTemplate tmpl;                 // 現在の入力内容を反映した値(プレビュー・OK時の結果)
    const std::vector<std::wstring>* fonts = nullptr;
    bool done = false;                 // ダイアログを閉じたか
    bool ok = false;                   // OK で閉じたか
    bool ready = false;                // 部品の作成が完了したか(作成途中の通知でプレビュー更新しないため)
    HFONT ui_font = nullptr;           // 部品の表示用フォント
    COLORREF custom_colors[16] = {};   // 色選択ダイアログの「作成した色」欄
};

DialogState* GetState(HWND hwnd) {
    return reinterpret_cast<DialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

// 部品を1つ作る小関数
HWND AddControl(HWND parent, LPCWSTR cls, LPCWSTR text, DWORD style, int x, int y, int w, int h, int id) {
    return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), ThisModule(), nullptr);
}

// 3状態チェックボックス(オン/オフ/■=変更しない) <-> 項目値 "1"/"0"/なし
void SetTriState(HWND hwnd, int id, const ItemList& items, const char* key) {
    const std::string* v = FindItem(items, key);
    CheckDlgButton(hwnd, id, !v ? BST_INDETERMINATE : (*v == "1" ? BST_CHECKED : BST_UNCHECKED));
}
void ReadTriState(HWND hwnd, int id, ItemList* items, const char* key) {
    const UINT s = IsDlgButtonChecked(hwnd, id);
    if (s == BST_INDETERMINATE) EraseItem(items, key);
    else SetItem(items, key, s == BST_CHECKED ? "1" : "0");
}

// 入力欄の内容 -> state.tmpl に反映
void ReadControls(HWND hwnd, DialogState* st) {
    TextTemplate t = st->base;  // 画面に無い項目(字間・行間など)は元の値を引き継ぐ
    t.name = GetWindowTextUtf8(GetDlgItem(hwnd, CID_NAME));
    for (const auto& f : kTextFields) {
        const std::string v = GetWindowTextUtf8(GetDlgItem(hwnd, f.id));
        if (v.empty()) EraseItem(&t.text_items, f.key);  // 空欄 = 適用しない
        else SetItem(&t.text_items, f.key, v);
    }
    ReadTriState(hwnd, CID_BOLD, &t.text_items, text_key::kBold);
    ReadTriState(hwnd, CID_ITALIC, &t.text_items, text_key::kItalic);

    // フィルタ: チェックが付いているものだけ残す(一覧の行番号 = base.filters の番号)
    HWND lv = GetDlgItem(hwnd, CID_FILTERS);
    t.filters.clear();
    for (size_t i = 0; i < st->base.filters.size(); ++i) {
        if (ListView_GetCheckState(lv, static_cast<int>(i))) t.filters.push_back(st->base.filters[i]);
    }
    t.replace_filters = IsDlgButtonChecked(hwnd, CID_REPLACE) == BST_CHECKED;
    st->tmpl = t;
}

void RefreshPreview(HWND hwnd) {
    DialogState* st = GetState(hwnd);
    if (!st->ready) return;
    ReadControls(hwnd, st);
    InvalidateRect(GetDlgItem(hwnd, CID_PREVIEW), nullptr, FALSE);
}

// 色選択ダイアログ(Windows 標準)を開き、結果を入力欄に書く
void PickColor(HWND hwnd, DialogState* st, int edit_id) {
    HWND edit = GetDlgItem(hwnd, edit_id);
    CHOOSECOLORW cc = {};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = hwnd;
    cc.rgbResult = HexToColorRef(GetWindowTextUtf8(edit), RGB(255, 255, 255));
    cc.lpCustColors = st->custom_colors;
    cc.Flags = CC_RGBINIT | CC_FULLOPEN;
    if (ChooseColorW(&cc)) {
        SetWindowTextW(edit, Utf8ToWide(ColorRefToHex(cc.rgbResult)).c_str());  // EN_CHANGE でプレビューも更新される
    }
}

// OK 押下時の入力チェック。問題があればメッセージを出して false
bool Validate(HWND hwnd, const TextTemplate& t) {
    unsigned char r, g, b;
    auto bad_color = [&](const char* key) {
        const std::string* v = FindItem(t.text_items, key);
        return v && !ParseHexColor(*v, &r, &g, &b);
    };
    LPCWSTR error = nullptr;
    if (t.name.empty()) error = L"テンプレート名を入力してください。";
    else if (bad_color(text_key::kColor)) error = L"文字色は 16進6桁(例: ffffff)で入力してください。";
    else if (bad_color(text_key::kEdge)) error = L"影・縁色は 16進6桁(例: 000000)で入力してください。";
    if (error) {
        MessageBoxW(hwnd, error, L"テキストテンプレ", MB_OK | MB_ICONWARNING);
        return false;
    }
    return true;
}

// 画面で直接編集しないテキスト項目の名前を列挙 例: "字間, 行間, 文字揃え"
std::wstring OtherTextKeys(const TextTemplate& t) {
    std::wstring s;
    for (const auto& kv : t.text_items) {
        bool shown = kv.first == text_key::kBold || kv.first == text_key::kItalic;
        for (const auto& f : kTextFields) shown = shown || kv.first == f.key;
        if (shown) continue;
        if (!s.empty()) s += L", ";
        s += Utf8ToWide(kv.first);
    }
    return s.empty() ? L"(なし)" : s;
}

// 標準描画の取り込み内容 例: "拡大率=120.000, X=0.00, Y=400.00"
std::wstring DrawSummary(const TextTemplate& t) {
    std::wstring s;
    for (const auto& kv : t.draw_items) {
        if (!s.empty()) s += L", ";
        s += Utf8ToWide(kv.first + "=" + kv.second);
    }
    return s.empty() ? L"(なし。「選択から登録」で取り込み)" : s;
}

// フィルタ1件の表示文字列 例: "縁取り  サイズ=4, 縁色=ffffff"
std::wstring FilterLabel(const EffectSetting& f) {
    std::wstring s = Utf8ToWide(f.name);
    if (!f.enabled) s += L"(無効)";
    s += L"  ";
    for (size_t i = 0; i < f.items.size(); ++i) {
        if (i) s += L", ";
        s += Utf8ToWide(f.items[i].first + "=" + f.items[i].second);
    }
    return s;
}

// 部品を配置して初期値を入れる
void CreateControls(HWND hwnd, DialogState* st) {
    // 画面の拡大率(125% 表示なら 1.25)に合わせて寸法を拡大する
    HDC hdc = GetDC(hwnd);
    const double s = GetDeviceCaps(hdc, LOGPIXELSY) / 96.0;
    ReleaseDC(hwnd, hdc);
    auto S = [s](int v) { return static_cast<int>(v * s); };

    const int lx = S(10), lw = S(90);    // ラベル列
    const int cx = S(105), cw = S(300);  // 入力列
    const int full_w = cx + cw - lx;     // 横幅いっぱいの部品の幅
    const int rh = S(24), gap = S(30);   // 行の高さ・行間
    int y = S(10);

    auto label = [&](LPCWSTR text) { AddControl(hwnd, WC_STATICW, text, SS_LEFT, lx, y + S(3), lw, rh, -1); };
    auto item = [&](const char* key) {
        const std::string* v = FindItem(st->base.text_items, key);
        return v ? Utf8ToWide(*v) : std::wstring();
    };

    label(L"名前");
    AddControl(hwnd, WC_EDITW, Utf8ToWide(st->base.name).c_str(), WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, cx, y, cw,
               rh, CID_NAME);
    y += gap;

    label(L"フォント");
    HWND font_combo = AddControl(hwnd, WC_COMBOBOXW, L"", CBS_DROPDOWN | CBS_SORT | WS_VSCROLL | WS_TABSTOP, cx, y, cw,
                                 S(300), CID_FONT);
    for (const auto& f : *st->fonts) SendMessageW(font_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(f.c_str()));
    SetWindowTextW(font_combo, item(text_key::kFont).c_str());
    y += gap;

    label(L"文字色");
    AddControl(hwnd, WC_EDITW, item(text_key::kColor).c_str(), WS_BORDER | WS_TABSTOP, cx, y, S(100), rh, CID_COLOR);
    AddControl(hwnd, WC_BUTTONW, L"選択...", BS_PUSHBUTTON | WS_TABSTOP, cx + S(105), y, S(70), rh, CID_COLOR_BTN);
    y += gap;

    label(L"影・縁色");
    AddControl(hwnd, WC_EDITW, item(text_key::kEdge).c_str(), WS_BORDER | WS_TABSTOP, cx, y, S(100), rh, CID_EDGE);
    AddControl(hwnd, WC_BUTTONW, L"選択...", BS_PUSHBUTTON | WS_TABSTOP, cx + S(105), y, S(70), rh, CID_EDGE_BTN);
    y += gap;

    label(L"文字装飾");
    HWND deco_combo = AddControl(hwnd, WC_COMBOBOXW, L"", CBS_DROPDOWN | WS_VSCROLL | WS_TABSTOP, cx, y, cw, S(200),
                                 CID_DECO);
    for (const auto& d : DecorationCandidates())
        SendMessageW(deco_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Utf8ToWide(d).c_str()));
    SetWindowTextW(deco_combo, item(text_key::kDeco).c_str());
    y += gap;

    // BS_AUTO3STATE: クリックで オン → オフ → ■(変更しない) と切り替わる
    AddControl(hwnd, WC_BUTTONW, L"太字(B)", BS_AUTO3STATE | WS_TABSTOP, cx, y, S(90), rh, CID_BOLD);
    AddControl(hwnd, WC_BUTTONW, L"斜体(I)", BS_AUTO3STATE | WS_TABSTOP, cx + S(100), y, S(90), rh, CID_ITALIC);
    SetTriState(hwnd, CID_BOLD, st->base.text_items, text_key::kBold);
    SetTriState(hwnd, CID_ITALIC, st->base.text_items, text_key::kItalic);
    y += gap;

    label(L"サイズ");
    AddControl(hwnd, WC_EDITW, item(text_key::kSize).c_str(), WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, cx, y, S(140),
               rh, CID_SIZE);
    AddControl(hwnd, WC_STATICW, L"※「サイズ・拡大率も適用」オン時のみ", SS_LEFT, cx + S(150), y + S(3), S(160), rh, -1);
    y += gap;

    AddControl(hwnd, WC_STATICW, L"※空欄・■の項目は適用時に変更しません", SS_LEFT, lx, y, full_w, S(18), -1);
    y += S(20);
    AddControl(hwnd, WC_STATICW, (L"その他のテキスト設定: " + OtherTextKeys(st->base)).c_str(),
               SS_LEFT | SS_ENDELLIPSIS, lx, y, full_w, S(18), CID_OTHERS);
    y += S(20);
    AddControl(hwnd, WC_STATICW, (L"拡大率・位置: " + DrawSummary(st->base)).c_str(), SS_LEFT | SS_ENDELLIPSIS, lx, y,
               full_w, S(18), -1);
    y += S(26);

    // フィルタ一覧(リストビュー + チェックボックス)
    AddControl(hwnd, WC_STATICW, L"フィルタ(チェックを外すとテンプレートから除外)", SS_LEFT, lx, y, full_w, S(18), -1);
    y += S(20);
    HWND lv = AddControl(hwnd, WC_LISTVIEWW, L"", WS_BORDER | WS_TABSTOP | LVS_REPORT | LVS_NOCOLUMNHEADER |
                         LVS_SINGLESEL | LVS_SHOWSELALWAYS, lx, y, full_w, S(100), CID_FILTERS);
    ListView_SetExtendedListViewStyle(lv, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
    LVCOLUMNW col = {};
    col.mask = LVCF_WIDTH;
    col.cx = full_w - S(24);
    ListView_InsertColumn(lv, 0, &col);
    for (size_t i = 0; i < st->base.filters.size(); ++i) {
        std::wstring text = FilterLabel(st->base.filters[i]);
        LVITEMW it = {};
        it.mask = LVIF_TEXT;
        it.iItem = static_cast<int>(i);
        it.pszText = text.data();
        ListView_InsertItem(lv, &it);
        ListView_SetCheckState(lv, static_cast<int>(i), TRUE);
    }
    y += S(104);
    if (st->base.filters.empty()) {
        AddControl(hwnd, WC_STATICW, L"(フィルタは「選択から登録」で、実物のテキストから取り込めます)", SS_LEFT, lx, y,
                   full_w, S(18), -1);
        y += S(20);
    }
    AddControl(hwnd, WC_BUTTONW, L"適用時に、対象の既存フィルタを置き換える(オフ=残して追加)",
               BS_AUTOCHECKBOX | WS_TABSTOP, lx, y, full_w, rh, CID_REPLACE);
    CheckDlgButton(hwnd, CID_REPLACE, st->base.replace_filters ? BST_CHECKED : BST_UNCHECKED);
    y += gap;

    // プレビュー(SS_OWNERDRAW = 中身を自分で描く。WM_DRAWITEM で描画)
    AddControl(hwnd, WC_STATICW, L"", SS_OWNERDRAW, lx, y, full_w, S(80), CID_PREVIEW);
    y += S(90);

    AddControl(hwnd, WC_BUTTONW, L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, cx + cw - S(170), y, S(80), S(28), IDOK);
    AddControl(hwnd, WC_BUTTONW, L"キャンセル", BS_PUSHBUTTON | WS_TABSTOP, cx + cw - S(80), y, S(80), S(28), IDCANCEL);
    y += S(40);

    // 全部品に UI フォントを設定
    EnumChildWindows(hwnd, [](HWND child, LPARAM font) -> BOOL {
        SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(font), TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(st->ui_font));

    // 中身(クライアント領域)が収まるようにウィンドウサイズを調整
    RECT rc = {0, 0, cx + cw + S(10), y};
    AdjustWindowRectEx(&rc, static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE)), FALSE,
                       static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE)));
    SetWindowPos(hwnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER);
}

LRESULT CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCCREATE: {
            // CreateWindowEx の最後の引数(DialogState*)をウィンドウに紐付ける
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            break;
        }
        case WM_CREATE: {
            DialogState* st = GetState(hwnd);
            CreateControls(hwnd, st);
            st->ready = true;
            ReadControls(hwnd, st);  // 初期表示のプレビュー用
            return 0;
        }
        case WM_COMMAND: {
            DialogState* st = GetState(hwnd);
            const int id = LOWORD(wp);
            const int code = HIWORD(wp);
            if (id == IDOK) {
                ReadControls(hwnd, st);
                if (Validate(hwnd, st->tmpl)) {
                    st->ok = true;
                    DestroyWindow(hwnd);
                }
            } else if (id == IDCANCEL) {
                DestroyWindow(hwnd);
            } else if (id == CID_COLOR_BTN) {
                PickColor(hwnd, st, CID_COLOR);
            } else if (id == CID_EDGE_BTN) {
                PickColor(hwnd, st, CID_EDGE);
            } else if (code == EN_CHANGE || code == CBN_EDITCHANGE || code == BN_CLICKED) {
                RefreshPreview(hwnd);  // 入力が変わったらプレビューを更新
            } else if (code == CBN_SELCHANGE) {
                PostMessageW(hwnd, WM_APP_REFRESH, 0, 0);
            }
            return 0;
        }
        case WM_NOTIFY: {
            // フィルタ一覧のチェックが切り替わったらプレビュー更新
            auto* nm = reinterpret_cast<NMLISTVIEW*>(lp);
            if (nm->hdr.idFrom == CID_FILTERS && nm->hdr.code == LVN_ITEMCHANGED &&
                ((nm->uNewState ^ nm->uOldState) & LVIS_STATEIMAGEMASK)) {
                RefreshPreview(hwnd);
            }
            return 0;
        }
        case WM_APP_REFRESH:
            RefreshPreview(hwnd);
            return 0;

        case WM_DRAWITEM: {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if (dis->CtlID == CID_PREVIEW) {
                DrawTemplatePreview(dis->hDC, dis->rcItem, GetState(hwnd)->tmpl);
                return TRUE;
            }
            break;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            GetState(hwnd)->done = true;  // モーダルループを抜ける合図
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// 一般的な UI フォント(メッセージ用フォント)を作成
HFONT CreateUiFont() {
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    return CreateFontIndirectW(&ncm.lfMessageFont);
}

}  // namespace

bool ShowTemplateEditDialog(HWND owner, const std::vector<std::wstring>& fonts, LPCWSTR title, TextTemplate* tmpl) {
    // リストビュー部品を使えるようにする(Windows のコモンコントロールの初期化)
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&icc);

    // ウィンドウクラスの登録(2回目以降は既に登録済みエラーになるが問題なし)
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DialogProc;
    wc.hInstance = ThisModule();
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    DialogState st;
    st.base = *tmpl;
    st.tmpl = *tmpl;
    st.fonts = &fonts;
    st.ui_font = CreateUiFont();

    // マウス位置の近くに表示
    POINT pt;
    GetCursorPos(&pt);
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kClassName, title, WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                pt.x - 100, pt.y - 100, 400, 400, owner, nullptr, ThisModule(), &st);
    if (!hwnd) {
        DeleteObject(st.ui_font);
        return false;
    }
    ShowWindow(hwnd, SW_SHOW);

    // ---- モーダルループ ----
    // 親を無効化し、ダイアログが閉じるまで自前でメッセージを処理する。
    // IsDialogMessage は Tab キーでの部品移動や Enter=OK / Esc=キャンセル を処理してくれる。
    if (owner) EnableWindow(owner, FALSE);
    MSG msg = {};
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (owner) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    if (msg.message == WM_QUIT) PostQuitMessage(static_cast<int>(msg.wParam));  // アプリ終了要求は本体に返す

    DeleteObject(st.ui_font);
    if (st.ok) *tmpl = st.tmpl;
    return st.ok;
}
