//----------------------------------------------------------------------------------
//  テキストテンプレ 汎用プラグイン(.aux2) for AviUtl ExEdit2
//
//  AviUtl2 は起動時に Plugin フォルダの .aux2(中身は Windows の DLL)を読み込み、
//  下記の「外部公開(dllexport)された関数」を決まった順番で呼び出す。
//    InitializeLogger -> InitializeConfig -> InitializePlugin -> RegisterPlugin
//----------------------------------------------------------------------------------
#include <windows.h>

#include "config2.h"
#include "logger2.h"
#include "main_window.h"
#include "plugin2.h"
#include "util_win.h"

namespace {
LOG_HANDLE* g_logger = nullptr;
CONFIG_HANDLE* g_config = nullptr;

COMMON_PLUGIN_TABLE g_plugin_table = {
    L"テキストテンプレ",                      // プラグインの名前
    L"TextTemplate version 0.3.0",   // プラグインの情報
};
}  // namespace

// 必要とする本体バージョン(SDK のサンプルと同じ値)。これより古い本体では読み込まれない。
EXTERN_C __declspec(dllexport) DWORD RequiredVersion() {
    return 2003300;
}

EXTERN_C __declspec(dllexport) void InitializeLogger(LOG_HANDLE* handle) {
    g_logger = handle;
}

EXTERN_C __declspec(dllexport) void InitializeConfig(CONFIG_HANDLE* handle) {
    g_config = handle;
}

EXTERN_C __declspec(dllexport) bool InitializePlugin(DWORD /*version*/) {
    return true;
}

EXTERN_C __declspec(dllexport) void UninitializePlugin() {
}

EXTERN_C __declspec(dllexport) COMMON_PLUGIN_TABLE* GetCommonPluginTable(void) {
    return &g_plugin_table;
}

// プラグイン登録(必須)。ここでパネルを作って AviUtl2 に登録する
EXTERN_C __declspec(dllexport) void RegisterPlugin(HOST_APP_TABLE* host) {
    // 保存先: <AviUtl2のアプリケーションデータフォルダ>\Plugin\TextTemplate\templates.ini
    std::wstring dir = (g_config && g_config->app_data_path) ? g_config->app_data_path : L".";
    dir += L"\\Plugin\\TextTemplate";
    EnsureDirectory(dir);

    EDIT_HANDLE* edit = host->create_edit_handle();
    HWND panel = CreateTemplatePanel(edit, g_logger, g_config, dir + L"\\templates.ini");
    if (!panel) {
        if (g_logger) g_logger->error(g_logger, L"テキストテンプレ: ウィンドウ作成に失敗");
        return;
    }
    // AviUtl2 の「表示」メニュー等から開けるウィンドウとして登録
    host->register_window_client(L"テキストテンプレ", panel);
}
