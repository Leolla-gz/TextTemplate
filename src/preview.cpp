//----------------------------------------------------------------------------------
//  テンプレートのプレビュー描画
//
//  描画の考え方(下から順に重ねる):
//    ① シャドー(フィルタ)  … 一番外側の輪郭を、ずらして影色で塗る
//    ② 縁取り(フィルタ)    … 後に追加した縁ほど外側。外側から順に「太いペンで輪郭をなぞる」
//    ③ 文字装飾の影・縁     … テキスト自体の装飾(縁取り文字/影付き文字)
//    ④ 文字本体             … 文字色、またはグラデーション
//  文字の輪郭は GDI の「パス」(文字の形をした図形)として作り、
//  StrokeAndFillPath(輪郭を太いペンでなぞる+中を塗る)で縁を表現する。
//----------------------------------------------------------------------------------
#include "preview.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

#include "util_win.h"

namespace {

// 背景は白文字・黒縁のどちらも見えるよう中間の暗いグレーにする
constexpr COLORREF kPreviewBack = RGB(0x40, 0x40, 0x48);
constexpr double kDefaultTextSize = 34.0;  // サイズ未指定時に仮定する文字サイズ(推測値)

// 1本の縁(半径と色)
struct Ring {
    double radius;  // 文字の輪郭からの距離(プレビュー上のピクセル)
    COLORREF color;
};

// プレビュー描画に必要な情報をテンプレートから取り出したもの
struct Look {
    std::wstring face;
    bool bold = false, italic = false;
    double text_size = kDefaultTextSize;  // AviUtl2 上の文字サイズ(縁の太さの換算に使う)
    COLORREF text_color = RGB(255, 255, 255);
    COLORREF edge_color = RGB(0, 0, 0);
    std::wstring decoration;
    // フィルタ由来
    std::vector<std::pair<double, COLORREF>> edges;  // 縁取り(AviUtl2上のサイズ, 色)を追加順に
    bool gradient = false;
    COLORREF grad_start = RGB(255, 255, 255), grad_end = RGB(0, 0, 0);
    double grad_angle = 0.0, grad_width = 100.0;
    bool shadow = false;
    double shadow_x = 0, shadow_y = 0;
    COLORREF shadow_color = RGB(0, 0, 0);
};

std::string ItemOr(const ItemList& items, const char* key, const char* fallback) {
    const std::string* v = FindItem(items, key);
    return (v && !v->empty()) ? *v : std::string(fallback);
}

Look ResolveLook(const TextTemplate& t) {
    Look k;
    k.face = Utf8ToWide(ItemOr(t.text_items, text_key::kFont, "Yu Gothic UI"));
    k.bold = ItemOr(t.text_items, text_key::kBold, "0") == "1";
    k.italic = ItemOr(t.text_items, text_key::kItalic, "0") == "1";
    k.text_size = LeadingNumber(ItemOr(t.text_items, text_key::kSize, ""), kDefaultTextSize);
    if (k.text_size <= 1) k.text_size = kDefaultTextSize;
    k.text_color = HexToColorRef(ItemOr(t.text_items, text_key::kColor, "ffffff"), RGB(255, 255, 255));
    k.edge_color = HexToColorRef(ItemOr(t.text_items, text_key::kEdge, "000000"), RGB(0, 0, 0));
    k.decoration = Utf8ToWide(ItemOr(t.text_items, text_key::kDeco, ""));

    for (const auto& f : t.filters) {
        if (!f.enabled) continue;  // 無効なフィルタはプレビューにも出さない
        if (f.name == u8"縁取り") {
            k.edges.emplace_back(LeadingNumber(ItemOr(f.items, u8"サイズ", "3"), 3),
                                 HexToColorRef(ItemOr(f.items, u8"縁色", "000000"), RGB(0, 0, 0)));
        } else if (f.name == u8"グラデーション") {
            k.gradient = true;
            k.grad_start = HexToColorRef(ItemOr(f.items, u8"開始色", "ffffff"), RGB(255, 255, 255));
            k.grad_end = HexToColorRef(ItemOr(f.items, u8"終了色", "000000"), RGB(0, 0, 0));
            k.grad_angle = LeadingNumber(ItemOr(f.items, u8"角度", "0"), 0);
            k.grad_width = LeadingNumber(ItemOr(f.items, u8"幅", "100"), 100);
        } else if (f.name == u8"シャドー") {
            // 項目名は AviUtl(初代)からの推測
            k.shadow = true;
            k.shadow_x = LeadingNumber(ItemOr(f.items, "X", "5"), 5);
            k.shadow_y = LeadingNumber(ItemOr(f.items, "Y", "5"), 5);
            k.shadow_color = HexToColorRef(ItemOr(f.items, u8"影色", "000000"), RGB(0, 0, 0));
        }
    }
    return k;
}

// 現在のフォントで kPreviewSample の形をパスとして登録する((x,y) は文字列の左上)
void BuildTextPath(HDC hdc, int x, int y) {
    BeginPath(hdc);
    TextOutW(hdc, x, y, kPreviewSample, lstrlenW(kPreviewSample));
    EndPath(hdc);
}

// パスを「幅 2*radius のペンでなぞる + 中を塗る」= 輪郭から radius だけ太らせた形を描く
void FillWidened(HDC hdc, int x, int y, double radius, COLORREF color) {
    BuildTextPath(hdc, x, y);
    LOGBRUSH lb = {BS_SOLID, color, 0};
    const DWORD width = static_cast<DWORD>(std::max(1.0, radius * 2.0));
    HPEN pen = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_JOIN_ROUND | PS_ENDCAP_ROUND, width, &lb, 0, nullptr);
    HBRUSH brush = CreateSolidBrush(color);
    HGDIOBJ old_pen = SelectObject(hdc, pen);
    HGDIOBJ old_brush = SelectObject(hdc, brush);
    if (radius > 0) StrokeAndFillPath(hdc); else FillPath(hdc);
    SelectObject(hdc, old_pen);
    SelectObject(hdc, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}

// 文字の形の内側だけにグラデーションを描く
void FillGradient(HDC hdc, int x, int y, const RECT& area, const Look& k, double scale) {
    const int w = area.right - area.left, h = area.bottom - area.top;
    if (w <= 0 || h <= 0) return;

    // 文字の形で切り抜き(クリップ)を設定
    BuildTextPath(hdc, x, y);
    SelectClipPath(hdc, RGN_AND);

    // 32bit の画像を作り、1画素ずつ色を計算
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;  // 負 = 上から下へ並ぶ画像
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    std::vector<DWORD> pixels(static_cast<size_t>(w) * h);

    // 角度 0 で「上=開始色 → 下=終了色」と仮定(AviUtl2 の向きの定義は未確認)
    const double rad = k.grad_angle * 3.14159265358979 / 180.0;
    const double dx = -std::sin(rad), dy = std::cos(rad);
    const double band = std::max(1.0, k.grad_width * scale);  // 色が変化する幅(ピクセル)
    const double cx = w / 2.0, cy = h / 2.0;
    for (int py = 0; py < h; ++py) {
        for (int px = 0; px < w; ++px) {
            double t = ((px - cx) * dx + (py - cy) * dy) / band + 0.5;
            t = std::min(1.0, std::max(0.0, t));
            auto mix = [t](int a, int b) { return static_cast<DWORD>(a + (b - a) * t + 0.5); };
            const DWORD r = mix(GetRValue(k.grad_start), GetRValue(k.grad_end));
            const DWORD g = mix(GetGValue(k.grad_start), GetGValue(k.grad_end));
            const DWORD b = mix(GetBValue(k.grad_start), GetBValue(k.grad_end));
            pixels[static_cast<size_t>(py) * w + px] = (r << 16) | (g << 8) | b;  // BGRA の並び
        }
    }
    // クリップ(文字の形)の内側だけに転送される
    SetDIBitsToDevice(hdc, area.left, area.top, w, h, 0, 0, 0, h, pixels.data(), &bi, DIB_RGB_COLORS);
}

}  // namespace

void DrawTemplatePreview(HDC hdc, const RECT& rect, const TextTemplate& t) {
    const int saved = SaveDC(hdc);  // 最後に描画設定(クリップ・選択中の部品)を丸ごと元に戻す
    IntersectClipRect(hdc, rect.left, rect.top, rect.right, rect.bottom);

    HBRUSH back = CreateSolidBrush(kPreviewBack);
    FillRect(hdc, &rect, back);
    DeleteObject(back);

    const Look k = ResolveLook(t);
    const int rect_w = rect.right - rect.left, rect_h = rect.bottom - rect.top;

    // 文字装飾(テキスト自体の影/縁)の太さ。AviUtl2 の実寸は未確認のため文字サイズ比で近似
    const bool deco_edge = k.decoration.find(L"縁取り") != std::wstring::npos;
    const bool deco_shadow = k.decoration.find(L"影") != std::wstring::npos;
    double deco_ratio = 0.05;
    if (k.decoration.find(L"細") != std::wstring::npos || k.decoration.find(L"薄") != std::wstring::npos) deco_ratio = 0.03;
    if (k.decoration.find(L"太") != std::wstring::npos) deco_ratio = 0.08;

    // AviUtl2 上の「縁の合計の太さ」(文字サイズ単位)。文字が枠に収まる倍率を決めるのに使う
    double total_edge = deco_edge ? k.text_size * deco_ratio : 0;
    for (const auto& e : k.edges) total_edge += e.first;

    // 縮小率 scale = プレビュー上のピクセル / AviUtl2 上のピクセル
    // まず高さ基準で決め、横にはみ出すなら縮める
    double scale = rect_h * 0.55 / (k.text_size + total_edge * 2);
    auto make_font = [&](double s) {
        return CreateFontW(-static_cast<int>(std::max(4.0, k.text_size * s)), 0, 0, 0, k.bold ? FW_BOLD : FW_NORMAL,
                           k.italic, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                           ANTIALIASED_QUALITY, DEFAULT_PITCH, k.face.c_str());
    };
    HFONT font = make_font(scale);
    HGDIOBJ old_font = SelectObject(hdc, font);
    SIZE ext;
    GetTextExtentPoint32W(hdc, kPreviewSample, lstrlenW(kPreviewSample), &ext);
    const double need_w = ext.cx + total_edge * 2 * scale;
    if (need_w > rect_w * 0.92 && need_w > 0) {
        scale *= rect_w * 0.92 / need_w;
        SelectObject(hdc, old_font);
        DeleteObject(font);
        font = make_font(scale);
        SelectObject(hdc, font);
        GetTextExtentPoint32W(hdc, kPreviewSample, lstrlenW(kPreviewSample), &ext);
    }
    SetBkMode(hdc, TRANSPARENT);
    // 文字列の左上座標(中央寄せ)
    const int x = rect.left + (rect_w - ext.cx) / 2;
    const int y = rect.top + (rect_h - ext.cy) / 2;

    // 縁の一覧(外側から描く)。文字装飾の縁が最も内側、その外にフィルタの縁が追加順に重なる
    std::vector<Ring> rings;
    double r = 0;
    if (deco_edge) {
        r += std::max(1.0, k.text_size * deco_ratio * scale);
        rings.push_back({r, k.edge_color});
    }
    for (const auto& e : k.edges) {
        r += std::max(1.0, e.first * scale);
        rings.push_back({r, e.second});
    }
    const double outer = r;

    // ① シャドー(一番外側の輪郭をずらして描く)
    if (k.shadow) {
        FillWidened(hdc, x + static_cast<int>(k.shadow_x * scale), y + static_cast<int>(k.shadow_y * scale), outer,
                    k.shadow_color);
    }
    // ② 縁(外側から内側へ)
    for (auto it = rings.rbegin(); it != rings.rend(); ++it) FillWidened(hdc, x, y, it->radius, it->color);
    // ③ 文字装飾の影
    if (deco_shadow && !deco_edge) {
        const int off = static_cast<int>(std::max(1.0, k.text_size * deco_ratio * scale));
        FillWidened(hdc, x + off, y + off, 0, k.edge_color);
    }
    // ④ 文字本体
    if (k.gradient) {
        FillGradient(hdc, x, y, rect, k, scale);
    } else {
        FillWidened(hdc, x, y, 0, k.text_color);
    }

    SelectObject(hdc, old_font);
    DeleteObject(font);
    RestoreDC(hdc, saved);
}

std::wstring FilterSummary(const TextTemplate& t) {
    // 同名フィルタは「×個数」にまとめる(登場順は保つ)
    std::vector<std::wstring> order;
    std::map<std::wstring, int> count;
    for (const auto& f : t.filters) {
        const std::wstring n = Utf8ToWide(f.name);
        if (count[n]++ == 0) order.push_back(n);
    }
    std::wstring s;
    for (const auto& n : order) {
        if (!s.empty()) s += L" / ";
        s += n;
        if (count[n] > 1) s += L"×" + std::to_wstring(count[n]);
    }
    return s;
}
