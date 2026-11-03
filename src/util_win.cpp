//----------------------------------------------------------------------------------
//  Windows 用の小物関数
//----------------------------------------------------------------------------------
#include "util_win.h"

#include <vector>

#include "template_store.h"

HINSTANCE ThisModule() {
    // この関数自体のアドレスが属するモジュール(=このDLL)を問い合わせる
    HMODULE mod = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&ThisModule), &mod);
    return mod;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    // 1回目: 必要な長さを問い合わせ / 2回目: 実際に変換(Windows API の定番パターン)
    const int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len);
    return out;
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return std::string();
    const int len = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len, nullptr, nullptr);
    return out;
}

bool ReadWholeFile(const std::wstring& path, std::string* out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(h, &size) && size.QuadPart < 16 * 1024 * 1024;  // 16MB超は異常とみなす
    if (ok) {
        out->resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        ok = out->empty() || (ReadFile(h, out->data(), static_cast<DWORD>(out->size()), &read, nullptr) &&
                              read == out->size());
    }
    CloseHandle(h);
    return ok;
}

bool WriteWholeFile(const std::wstring& path, const std::string& data) {
    const std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) &&
                    written == data.size();
    CloseHandle(h);
    if (!ok) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    // 一時ファイルを本番ファイルへ置き換え(既存があれば上書き)
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
}

void EnsureDirectory(const std::wstring& dir) {
    // "C:\a\b\c" を先頭から順に作る(CreateDirectoryW は1階層ずつしか作れないため)
    for (size_t pos = dir.find(L'\\', 3); ; pos = dir.find(L'\\', pos + 1)) {
        CreateDirectoryW(dir.substr(0, pos).c_str(), nullptr);  // 既に存在するエラーは無視
        if (pos == std::wstring::npos) break;
    }
}

COLORREF HexToColorRef(const std::string& hex, COLORREF fallback) {
    unsigned char r, g, b;
    if (!ParseHexColor(hex, &r, &g, &b)) return fallback;
    return RGB(r, g, b);
}

std::string ColorRefToHex(COLORREF c) {
    return FormatHexColor(GetRValue(c), GetGValue(c), GetBValue(c));
}

std::string GetWindowTextUtf8(HWND hwnd) {
    const int len = GetWindowTextLengthW(hwnd);
    std::wstring buf(len + 1, L'\0');
    GetWindowTextW(hwnd, buf.data(), len + 1);
    buf.resize(len);
    return WideToUtf8(buf);
}
