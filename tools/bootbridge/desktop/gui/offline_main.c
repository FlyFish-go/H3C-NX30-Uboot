#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>
#include <winver.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include <process.h>
#include "../catalog/bb_catalog.h"
#include "../src/bridge_sha256.h"
#include "../offline/bb_offline_rules.h"
#include "../src/bb_fetch.h"
#include "bb_ui_text.h"
#include "bb_ui_strings_generated.h"
#include "bb_guide_models_generated.h"

#define WM_JOB_DONE (WM_APP + 1)
#define WM_APP_TOAST (WM_APP + 2)
#define WM_APP_FETCH (WM_APP + 3)
#define SN_EDIT_ID 101
#define PATH_CAP 32768
#define MAX_ELEMS 160
#define SCROLL_STEP 46
#define MAX_CONTENT_WIDTH 760
#define CONTENT_PAD_X 48
#define CONTENT_PAD_BOTTOM 28
#define SIDEBAR_WIDTH 248
#define APP_HEIGHT 56
#define FOOTER_HEIGHT 64

typedef enum { JOB_SELECT=1, JOB_EXPORT=2, JOB_FETCH=3 } job_kind;
typedef enum { FILE_EMPTY, FILE_HASHING, FILE_MATCHED, FILE_REJECTED } file_state;
typedef enum { REJECT_UNSUPPORTED, REJECT_AMBIGUOUS, REJECT_READ, REJECT_PATH } reject_kind;
typedef enum { GEN_IDLE, GEN_RUNNING, GEN_DONE, GEN_FAILED } gen_state;
typedef enum { EL_APP, EL_NONE, EL_LANG_ZH, EL_LANG_EN, EL_SETTINGS, EL_STEP, EL_MODEL, EL_PICK,
               EL_CANCEL, EL_ANOTHER, EL_COPY, EL_OPEN, EL_CHANGE_INPUT, EL_CHANGE_OUTPUT,
               EL_GENERATE, EL_CHECK_UPGRADE, EL_NEXT, EL_PREV, EL_MODAL_CLOSE, EL_THEME,
               EL_COPY_DIAG, EL_FAQI, EL_FETCH, EL_FETCH_GO, EL_FETCH_CLOSE,
               EL_FETCH_SN } el_kind;

typedef struct {
    el_kind kind;
    RECT rect;
    int index;
    int aux;
    int disabled;
    int layer;
} element;

typedef struct {
    HWND owner;
    wchar_t input[PATH_CAP], output[PATH_CAP];
    unsigned long selection_epoch;
    const bb_bundle_catalog *catalog;
    const bb_bundle_rule *bundle;
    job_kind kind;
    volatile LONG cancel;
    int cancelled;
    DWORD error;
    uint64_t size;
    unsigned char digest[32];
    bb_select_status selection;
    bb_result result;
    /* JOB_FETCH: input is the destination chosen in the save dialog. */
    bbf_route route;
    char url[512];
    char version[64];
    char serial[BBN_SERIAL_MAX + 1];
    unsigned char expected[32];
    uint64_t expected_bytes;
    volatile LONG fetch_phase;
    volatile LONG fetch_kib;
    bbf_result fetch;
} job;

typedef struct {
    int step;
    int lang; /* 0 zh, 1 en */
    int theme; /* 0 auto, 1 light, 2 dark */
    int guide_model;
    file_state file;
    reject_kind reject;
    gen_state gen;
    int upgraded;
    int faq_open;
    int modal;
    int rules_ok;
    int hashing_percent;
    int building_percent;
    int left_step_one;
    int left_step_five;
    int scroll;
    int modal_scroll;
    int modal_viewport;
    int content_height;
    int hover;          /* element index from the last paint, for change detection */
    el_kind hover_kind; /* what is hovered; compare with hover_is(), never hover == EL_* */
    int pressed;
    el_kind focus_kind;
    int focus_index;
    int keyboard_focus;
    int toast_until;
    int auto_switched;
    uint64_t hash_bytes;
    uint64_t hash_total;
    wchar_t toast[256];
    wchar_t rules_message[512];
    wchar_t input[PATH_CAP];
    wchar_t output[PATH_CAP];
    uint64_t input_size;
    unsigned char input_digest[32];
    const bb_bundle_rule *bundle;
    wchar_t last_error[512];
    int fetching;          /* a JOB_FETCH is running */
    int fetch_open;        /* query form expanded (pinned version without a public URL) */
    int fetch_phase;
    int fetch_error;       /* bbf_status of the last failed download, 0 none */
    int fetch_route;
    int sn_focus;
    unsigned fetch_http;
    long long fetch_code;
    uint64_t fetch_done;
    uint64_t fetch_total;
    wchar_t fetch_offered[128];
    wchar_t fetch_path[PATH_CAP];
} app_state;

typedef struct {
    COLORREF bg, panel, soft, ink, secondary, muted, line, line_strong;
    COLORREF teal, teal_ink, teal_hover, on_teal, tint, tint_line;
    COLORREF green, green_bg, green_line, amber, amber_bg, amber_line;
    COLORREF red, red_bg, red_line, track;
    int dark;
} theme;

static HWND main_window;
static HANDLE worker;
static job *active;
static bb_bundle_catalog offline_catalog;
static unsigned long selection_epoch;
static int closing;
static int dpi = 96;
static int fonts_dpi;
static HFONT font_small, font_body, font_small_bold, font_heading, font_brand, font_mono, font_big, font_logo;
static HFONT font_body_strong, font_small_strong;
static theme colors;
static app_state state;
static element elements[MAX_ELEMS];
static int element_count;
static wchar_t exe_dir[PATH_CAP];
static wchar_t self_version[32];
static unsigned char projected_output_digest[32];
static ULONG_PTR gdiplus_token;
static int tracking_mouse;
static int focus_visible;
/* The router SN field is the only native child control (caret, IME, paste). */
static HWND sn_edit;
static WNDPROC sn_edit_proc;
static HBRUSH sn_brush;
static COLORREF sn_brush_color, sn_painted_color;
static RECT sn_want, sn_view, sn_placed, sn_region;
static int sn_want_visible;
static unsigned fonts_generation, sn_fonts_generation;

static int S(int px) { return MulDiv(px, dpi, 96); }
/* Fractional logical pixels in hundredths, e.g. S100(175) == 1.75px at 96 DPI. */
static int S100(int hundredths) { int v = MulDiv(hundredths, dpi, 9600); return v < 1 ? 1 : v; }
static const wchar_t *T(bb_string_id id) { return state.lang ? bb_strings_en[id] : bb_strings_zh[id]; }
/* Same text as T() but keeps <b>/<u> as BB_UI_MARK_* codes, in source order.
 * Only for the bb_ui_wrap paragraph renderer; never for DrawTextW/clipboard. */
static const wchar_t *TM(bb_string_id id)
{
    const wchar_t *marked = state.lang ? bb_strings_en_marked[id] : bb_strings_zh_marked[id];
    return marked ? marked : T(id);
}
static const wchar_t *step_title(int step) { return state.lang ? bb_strings_en_steps[step - 1][0] : bb_strings_zh_steps[step - 1][0]; }
static const wchar_t *step_subtitle(int step) { return state.lang ? bb_strings_en_steps[step - 1][1] : bb_strings_zh_steps[step - 1][1]; }

static void text_copy(wchar_t *out, size_t cap, const wchar_t *text) {
    if (!cap) return;
    wcsncpy(out, text ? text : L"", cap - 1);
    out[cap - 1] = 0;
}

static int format_checked(wchar_t *out, size_t cap, const wchar_t *source,
                          const wchar_t *model, const wchar_t *version,
                          int step, const wchar_t *filename)
{
    bb_ui_format_result result = bb_ui_format(source, out, cap, model, version,
                                               step, filename, self_version);
    if (!result.ok) {
        out[0] = 0;
    }
    return result.ok;
}

static void digest_hex(wchar_t out[65], const unsigned char digest[32]) {
    static const wchar_t h[] = L"0123456789abcdef";
    unsigned i;
    for (i = 0; i < 32; i++) {
        out[i * 2] = h[digest[i] >> 4];
        out[i * 2 + 1] = h[digest[i] & 15];
    }
    out[64] = 0;
}

static const bb_guide_model *guide(void) {
    if (state.guide_model < 0 || state.guide_model >= (int)BB_GUIDE_MODEL_COUNT)
        state.guide_model = 0;
    return &bb_guide_models[state.guide_model];
}

static const wchar_t *base_name(const wchar_t *path)
{
    const wchar_t *slash;
    if (!path) return L"";
    slash = wcsrchr(path, L'\\');
    if (!slash) slash = wcsrchr(path, L'/');
    return slash ? slash + 1 : path;
}

static void model_display(wchar_t *out, size_t cap, const bb_guide_model *model) {
    if (!model->alias[0]) {
        text_copy(out, cap, model->primary);
    } else {
        swprintf(out, cap, state.lang ? L"%ls (%ls)" : L"%ls（%ls）", model->primary, model->alias);
    }
}

static int bundle_matches_guide(const bb_bundle_rule *bundle, const bb_guide_model *model) {
    char expected[64], *space;
    if (!bundle) return 0;
    WideCharToMultiByte(CP_UTF8, 0, model->primary, -1, expected, sizeof(expected), NULL, NULL);
    space = strchr(expected, ' ');
    if (space) *space = 0;
    return _stricmp(bundle->display_models, expected) == 0;
}

static int bundles_include_model(const bb_bundle_rule *bundle, const bb_guide_model *model) {
    char labels[512], expected[64], *cursor;
    if (!bundle || !bundle->display_models) return 0;
    WideCharToMultiByte(CP_UTF8, 0, model->primary, -1, expected, sizeof(expected), NULL, NULL);
    strncpy(labels, bundle->display_models, sizeof(labels) - 1);
    labels[sizeof(labels) - 1] = 0;
    cursor = labels;
    while (cursor && *cursor) {
        char *separator = strstr(cursor, " / ");
        if (separator) *separator = 0;
        while (*cursor == ' ') cursor++;
        if (_stricmp(cursor, expected) == 0) return 1;
        cursor = separator ? separator + 3 : NULL;
    }
    return 0;
}

static int guide_available(const bb_guide_model *model) {
    size_t i;
    if (!state.rules_ok) return 0;
    for (i = 0; i < offline_catalog.bundle_count; i++) {
        if (bundles_include_model(&offline_catalog.bundles[i], model)) return 1;
    }
    return 0;
}

static void copy_clipboard(HWND hwnd, const wchar_t *text) {
    HGLOBAL memory;
    size_t bytes;
    if (!text || !OpenClipboard(hwnd)) return;
    EmptyClipboard();
    bytes = (wcslen(text) + 1) * sizeof(wchar_t);
    memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        void *target = GlobalLock(memory);
        if (target) {
            memcpy(target, text, bytes);
            GlobalUnlock(memory);
            if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
        } else GlobalFree(memory);
    }
    CloseClipboard();
}

static int valid_url(const wchar_t *url) {
    static const wchar_t http_scheme[] = {'h','t','t','p',':','/','/',0};
    static const wchar_t https_scheme[] = {'h','t','t','p','s',':','/','/',0};
    return url && (_wcsnicmp(url, http_scheme, 7) == 0 || _wcsnicmp(url, https_scheme, 8) == 0);
}

static void bb_open_url(HWND hwnd, const wchar_t *url) {
    if (!valid_url(url)) return;
    ShellExecuteW(hwnd, L"open", url, NULL, NULL, SW_SHOWNORMAL);
}

static void toast(const wchar_t *message) {
    text_copy(state.toast, 256, message);
    state.toast_until = GetTickCount() + 1400;
    SetTimer(main_window, 2, 1500, NULL);
    InvalidateRect(main_window, NULL, FALSE);
}

static void save_ini(void) {
    wchar_t path[PATH_CAP];
    if (swprintf(path, PATH_CAP, L"%ls\\BootBridge.ini", exe_dir) < 0) return;
    WritePrivateProfileStringW(L"UI", L"Language", state.lang ? L"en" : L"zh", path);
    {
        const wchar_t *themes[] = {L"auto", L"light", L"dark"};
        WritePrivateProfileStringW(L"UI", L"Theme", themes[state.theme], path);
    }
    if (BB_GUIDE_MODEL_COUNT)
        WritePrivateProfileStringW(L"UI", L"Model", guide()->id, path);
}

static void load_ini(void) {
    wchar_t path[PATH_CAP], value[32];
    if (swprintf(path, PATH_CAP, L"%ls\\BootBridge.ini", exe_dir) < 0) return;
    GetPrivateProfileStringW(L"UI", L"Language", L"", value, 32, path);
    if (value[0]) state.lang = _wcsicmp(value, L"en") == 0;
    GetPrivateProfileStringW(L"UI", L"Theme", L"", value, 32, path);
    if (!_wcsicmp(value, L"light")) state.theme = 1;
    else if (!_wcsicmp(value, L"dark")) state.theme = 2;
    else state.theme = 0;
    GetPrivateProfileStringW(L"UI", L"Model", L"", value, 32, path);
    if (value[0]) {
        unsigned i;
        for (i = 0; i < BB_GUIDE_MODEL_COUNT; i++) if (!_wcsicmp(bb_guide_models[i].id, value)) state.guide_model = (int)i;
    }
}

static int system_language_is_chinese(void) {
    LANGID lang = GetUserDefaultUILanguage();
    return PRIMARYLANGID(lang) == LANG_CHINESE;
}

static int system_light_theme(void) {
    DWORD value = 1, size = sizeof(value);
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        RegGetValueW(key, NULL, L"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &value, &size);
        RegCloseKey(key);
    }
    return value != 0;
}

static void get_windows_version(wchar_t *out, size_t cap) {
    typedef LONG (WINAPI *rtl_get_version)(PRTL_OSVERSIONINFOW);
    HMODULE module = GetModuleHandleW(L"ntdll.dll");
    rtl_get_version getter = module
        ? (rtl_get_version)(void *)GetProcAddress(module, "RtlGetVersion")
        : NULL;
    RTL_OSVERSIONINFOW info;
    ZeroMemory(&info, sizeof(info));
    info.dwOSVersionInfoSize = sizeof(info);
    if (getter && getter(&info) == 0) {
        swprintf(out, cap, L"%lu.%lu.%lu", (unsigned long)info.dwMajorVersion,
                 (unsigned long)info.dwMinorVersion, (unsigned long)info.dwBuildNumber);
    } else {
        text_copy(out, cap, L"unknown");
    }
}

static void redact_username(wchar_t *text, size_t cap) {
    wchar_t user[256], *match;
    DWORD length;
    if (!text || !cap) return;
    length = GetEnvironmentVariableW(L"USERNAME", user, sizeof(user)/sizeof(user[0]));
    if (!length || length >= sizeof(user)/sizeof(user[0]) || wcslen(user) < 3) return;
    while ((match = wcsstr(text, user)) != NULL) {
        size_t before = (size_t)(match - text), after = wcslen(match + length);
        static const wchar_t replacement[] = L"%USERNAME%";
        size_t replacement_length = sizeof(replacement)/sizeof(replacement[0]) - 1;
        if (before + replacement_length + after + 1 > cap) break;
        memmove(match + replacement_length, match + length, (after + 1) * sizeof(wchar_t));
        wmemcpy(match, replacement, replacement_length);
    }
}

static const wchar_t *export_failure_message(bb_status status, int system_error) {
    static wchar_t formatted[512];
    const wchar_t *message;
    switch (status) {
    case BB_UNKNOWN_RECIPE: message = L"可信生成规则不存在。"; break;
    case BB_INVALID_RECIPE: message = L"离线生成规则无效。"; break;
    case BB_INVALID_PATH: message = L"路径无效；输出必须是普通文件路径并使用 .bin 扩展名。"; break;
    case BB_INPUT_ERROR: message = L"无法读取输入原件。"; break;
    case BB_INPUT_MISMATCH: message = L"输入原件摘要不匹配或读取期间发生变化。"; break;
    case BB_OUTPUT_EXISTS: message = L"输出文件已存在，未覆盖；请选择新文件名。"; break;
    case BB_OUTPUT_MISMATCH: message = L"输出完整性核验失败，未提交成品。"; break;
    default: message = L"文件操作失败，未提交成品；请检查磁盘空间和权限。"; break;
    }
    if (system_error) {
        swprintf(formatted, 512, L"%ls（系统错误 %d）", message, system_error);
        message = formatted;
    }
    return message;
}

static void update_titlebar(void) {
    BOOL dark = colors.dark ? TRUE : FALSE;
    if (FAILED(DwmSetWindowAttribute(main_window, 20, &dark, sizeof(dark))))
        DwmSetWindowAttribute(main_window, 19, &dark, sizeof(dark));
}

static void update_theme(void) {
    int dark = state.theme == 2 || (state.theme == 0 && !system_light_theme());
    colors.dark = dark;
    if (dark) {
        theme value = {RGB(0x0e,0x14,0x19),RGB(0x16,0x1f,0x26),RGB(0x1b,0x26,0x2e),RGB(0xe7,0xed,0xf0),
            RGB(0xa7,0xb5,0xbd),RGB(0x87,0x96,0xa0),RGB(0x2a,0x39,0x42),RGB(0x35,0x45,0x4e),RGB(0x4f,0xc3,0xbf),
            RGB(0x67,0xd0,0xcb),RGB(0x67,0xd0,0xcb),RGB(0x07,0x21,0x1f),RGB(0x15,0x32,0x2f),RGB(0x1f,0x3d,0x3a),
            RGB(0x78,0xd6,0xad),RGB(0x15,0x30,0x2a),RGB(0x21,0x42,0x37),RGB(0xf0,0xce,0x83),RGB(0x2f,0x2a,0x1d),
            RGB(0x43,0x39,0x1f),RGB(0xf1,0xa7,0x9c),RGB(0x35,0x21,0x1f),RGB(0x4a,0x2d,0x28),RGB(0x2a,0x39,0x42),1};
        colors = value;
    } else {
        theme value = {RGB(0xf3,0xf5,0xf7),RGB(0xff,0xff,0xff),RGB(0xf6,0xf8,0xf9),RGB(0x1d,0x2a,0x33),
            RGB(0x53,0x63,0x6e),RGB(0x6f,0x7d,0x88),RGB(0xe4,0xe9,0xed),RGB(0xd4,0xdb,0xe0),RGB(0x14,0x8a,0x8a),
            RGB(0x0f,0x6d,0x6d),RGB(0x0f,0x73,0x73),RGB(0xff,0xff,0xff),RGB(0xe6,0xf4,0xf2),RGB(0xcf,0xe9,0xe5),
            RGB(0x1f,0x7a,0x55),RGB(0xe9,0xf6,0xee),RGB(0xcf,0xe9,0xdb),RGB(0x8a,0x64,0x16),RGB(0xfd,0xf6,0xe7),
            RGB(0xf0,0xe2,0xbf),RGB(0xb2,0x4a,0x3f),RGB(0xfd,0xf1,0xee),RGB(0xf3,0xd7,0xcf),RGB(0xe4,0xe9,0xed),0};
        colors = value;
    }
    update_titlebar();
}

static void set_window_dpi_awareness(void) {
    typedef BOOL (WINAPI *dpi_awareness)(HANDLE);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    dpi_awareness enable = user32
        ? (dpi_awareness)(void *)GetProcAddress(user32, "SetProcessDpiAwarenessContext")
        : NULL;
    if (enable && (enable((HANDLE)-4) || enable((HANDLE)-3))) return;
    SetProcessDPIAware();
}

typedef HRESULT (WINAPI *get_dpi_for_monitor_fn)(HMONITOR, int, UINT *, UINT *);
typedef UINT (WINAPI *get_dpi_for_window_fn)(HWND);
typedef UINT (WINAPI *get_dpi_for_system_fn)(void);
typedef BOOL (WINAPI *adjust_window_rect_for_dpi_fn)(LPRECT, DWORD, BOOL, DWORD, UINT);

static HMODULE dpi_user32(void)
{
    return GetModuleHandleW(L"user32.dll");
}

static UINT window_dpi(HWND hwnd)
{
    HMODULE user32 = dpi_user32();
    get_dpi_for_window_fn getter = user32
        ? (get_dpi_for_window_fn)(void *)GetProcAddress(user32, "GetDpiForWindow") : NULL;
    if (getter && hwnd) return getter(hwnd);
    return (UINT)dpi;
}

static UINT primary_monitor_dpi(void)
{
    HMODULE user32 = dpi_user32();
    get_dpi_for_system_fn system_getter = user32
        ? (get_dpi_for_system_fn)(void *)GetProcAddress(user32, "GetDpiForSystem") : NULL;
    if (system_getter) return system_getter();
    {
        POINT origin = {0, 0};
        HMONITOR monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
        HMODULE shcore = LoadLibraryW(L"shcore.dll");
        get_dpi_for_monitor_fn getter = shcore
            ? (get_dpi_for_monitor_fn)(void *)GetProcAddress(shcore, "GetDpiForMonitor") : NULL;
        UINT x = 96, y = 96;
        if (getter && getter(monitor, 0, &x, &y) == S_OK && x) {
            FreeLibrary(shcore);
            return x;
        }
        if (shcore) FreeLibrary(shcore);
    }
    return 96;
}

static void adjust_window_rect_dpi(RECT *rect, DWORD style, BOOL menu, DWORD exstyle, UINT dpi_value)
{
    HMODULE user32 = dpi_user32();
    adjust_window_rect_for_dpi_fn adjust = user32
        ? (adjust_window_rect_for_dpi_fn)(void *)GetProcAddress(user32, "AdjustWindowRectExForDpi") : NULL;
    if (adjust && adjust(rect, style, menu, exstyle, dpi_value)) return;
    AdjustWindowRectEx(rect, style, menu, exstyle);
}

static void work_area_for_point(POINT point, RECT *work)
{
    MONITORINFO info;
    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(MonitorFromPoint(point, MONITOR_DEFAULTTOPRIMARY), &info)) {
        *work = info.rcWork;
    } else {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, work, 0);
    }
}

static void delete_fonts(void) {
    HFONT *fonts[] = {&font_small,&font_body,&font_small_bold,&font_heading,&font_brand,&font_mono,&font_big,&font_logo,
                      &font_body_strong,&font_small_strong};
    unsigned i;
    for (i = 0; i < sizeof(fonts)/sizeof(fonts[0]); i++) if (*fonts[i]) { DeleteObject(*fonts[i]); *fonts[i] = NULL; }
}

static HFONT make_font(int px, int weight, const wchar_t *face) {
    return CreateFontW(-MulDiv(px, dpi, 96), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH, face);
}

static void make_fonts(void) {
    /* Microsoft YaHei UI ships only Light/Regular/Bold faces. A FW_SEMIBOLD
       request leaves the weight to the GDI font mapper and can produce a
       synthesized, smeared face for small CJK glyphs, so Chinese uses the
       real Bold face. Latin text keeps Segoe UI Semibold. Sizes are
       unchanged so single-line layouts keep fitting at the minimum width. */
    const wchar_t *ui = state.lang ? L"Segoe UI" : L"Microsoft YaHei UI";
    const int strong = state.lang ? FW_SEMIBOLD : FW_BOLD;
    delete_fonts();
    font_small = make_font(13, FW_NORMAL, ui);
    font_body = make_font(14, FW_NORMAL, ui);
    font_small_bold = make_font(12, strong, ui);
    font_heading = make_font(23, strong, ui);
    font_brand = make_font(17, strong, ui);
    font_logo = make_font(18, FW_SEMIBOLD, L"Segoe UI");
    font_mono = make_font(12, FW_NORMAL, L"Consolas");
    font_big = make_font(15, strong, ui);
    font_body_strong = make_font(14, strong, ui);
    font_small_strong = make_font(13, strong, ui);
    fonts_dpi = dpi;
    fonts_generation++;
}

static int hover_is(el_kind kind)
{
    return state.hover >= 0 && state.hover_kind == kind;
}

static int element_is(const element *e, el_kind kind, int index)
{
    return e->kind == kind && e->index == index;
}

static int find_focus(void)
{
    int i;
    if (state.focus_kind == EL_NONE) return -1;
    for (i = 0; i < element_count; i++) {
        if (element_is(&elements[i], state.focus_kind, state.focus_index) && !elements[i].disabled)
            return i;
    }
    return -1;
}

static void move_focus(int backwards, int include_modal_close)
{
    int i, start = find_focus();
    int visible = 0;
    for (i = 0; i < element_count; i++) {
        if (elements[i].kind == EL_NONE) continue;
        if (state.modal && elements[i].kind != EL_LANG_ZH && elements[i].kind != EL_LANG_EN &&
            elements[i].kind != EL_THEME && elements[i].kind != EL_COPY_DIAG &&
            elements[i].kind != EL_MODAL_CLOSE) continue;
        visible++;
    }
    if (!visible) return;
    if (start < 0) start = backwards ? element_count : -1;
    for (i = 1; i <= element_count; i++) {
        int candidate = backwards ? (start - i + element_count * 2) % element_count
                                  : (start + i) % element_count;
        element *e = &elements[candidate];
        if (e->kind == EL_NONE || e->disabled || IsRectEmpty(&e->rect)) continue;
        if (state.modal && e->kind != EL_LANG_ZH && e->kind != EL_LANG_EN &&
            e->kind != EL_THEME && e->kind != EL_COPY_DIAG && e->kind != EL_MODAL_CLOSE) continue;
        state.focus_kind = e->kind; state.focus_index = e->index;
        state.keyboard_focus = 1; focus_visible = 1;
        return;
    }
    (void)include_modal_close;
}

static int clamp_scroll_now(void)
{
    RECT client;
    int viewport;
    if (!GetClientRect(main_window, &client)) return state.scroll;
    viewport = client.bottom - S(APP_HEIGHT + FOOTER_HEIGHT);
    state.scroll = bb_ui_clamp_scroll(state.scroll, state.content_height, viewport);
    return state.scroll;
}

static void fill(HDC dc, RECT r, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &r, brush);
    DeleteObject(brush);
}

static void rounded(HDC dc, RECT r, int radius, COLORREF fill_color, COLORREF line_color);

/* Plain GDI rounded rectangle. Used when GDI+ is unavailable or a GDI+
 * graphics object cannot be created for this DC (never recurse back). */
static void gdi_round(HDC dc, RECT r, int radius, COLORREF fill_color, COLORREF line_color)
{
    HPEN pen = CreatePen(PS_SOLID, 1, line_color);
    HBRUSH brush = CreateSolidBrush(fill_color);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    HGDIOBJ old_brush = SelectObject(dc, brush);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, S(radius) * 2, S(radius) * 2);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(brush);
    DeleteObject(pen);
}

static void gdiplus_round(HDC dc, RECT r, int radius, COLORREF fill_color, COLORREF line_color)
{
    GpGraphics *graphics = NULL;
    GpSolidFill *fill_brush = NULL;
    GpPen *pen = NULL;
    if (!gdiplus_token || GdipCreateFromHDC(dc, &graphics) != Ok || !graphics) {
        gdi_round(dc, r, radius, fill_color, line_color);
        return;
    }
    GdipSetSmoothingMode(graphics, SmoothingModeAntiAlias);
    GdipSetPixelOffsetMode(graphics, PixelOffsetModeHalf);
    GdipCreateSolidFill((ARGB)(0xff000000u | ((ULONG)GetRValue(fill_color) << 16) |
                              ((ULONG)GetGValue(fill_color) << 8) | GetBValue(fill_color)),
                        &fill_brush);
    GdipCreatePen1((ARGB)(0xff000000u | ((ULONG)GetRValue(line_color) << 16) |
                         ((ULONG)GetGValue(line_color) << 8) | GetBValue(line_color)),
                   (REAL)S(1), UnitPixel, &pen);
    {
        GpPath *path = NULL;
        if (GdipCreatePath(FillModeWinding, &path) == Ok && path) {
            REAL x = (REAL)r.left, y = (REAL)r.top;
            REAL width = (REAL)(r.right - r.left), height = (REAL)(r.bottom - r.top);
            REAL d = (REAL)(S(radius) * 2);
            GdipAddPathArc(path, x, y, d, d, 180.0f, 90.0f);
            GdipAddPathArc(path, x + width - d, y, d, d, 270.0f, 90.0f);
            GdipAddPathArc(path, x + width - d, y + height - d, d, d, 0.0f, 90.0f);
            GdipAddPathArc(path, x, y + height - d, d, d, 90.0f, 90.0f);
            GdipClosePathFigure(path);
            if (fill_brush) GdipFillPath(graphics, (GpBrush *)fill_brush, path);
            if (pen) GdipDrawPath(graphics, pen, path);
            GdipDeletePath(path);
        }
    }
    if (pen) GdipDeletePen(pen);
    if (fill_brush) GdipDeleteBrush((GpBrush *)fill_brush);
    GdipDeleteGraphics(graphics);
}

static void round_outline(HDC dc, RECT r, int radius, COLORREF color, int width)
{
    GpGraphics *graphics = NULL;
    if (gdiplus_token && GdipCreateFromHDC(dc, &graphics) == Ok && graphics) {
        GpPen *pen = NULL;
        GpPath *path = NULL;
        REAL w = (REAL)S(width);
        REAL inset = w / 2.0f;
        REAL x = (REAL)r.left + inset, y = (REAL)r.top + inset;
        REAL rw = (REAL)(r.right - r.left) - w, rh = (REAL)(r.bottom - r.top) - w;
        REAL d = (REAL)(S(radius) * 2);
        if (d > rw) d = rw;
        if (d > rh) d = rh;
        GdipSetSmoothingMode(graphics, SmoothingModeAntiAlias);
        GdipSetPixelOffsetMode(graphics, PixelOffsetModeHalf);
        GdipCreatePen1((ARGB)(0xff000000u | ((ULONG)GetRValue(color) << 16) |
                             ((ULONG)GetGValue(color) << 8) | GetBValue(color)),
                       w, UnitPixel, &pen);
        if (pen && d > 0.0f && GdipCreatePath(FillModeWinding, &path) == Ok && path) {
            GdipAddPathArc(path, x, y, d, d, 180.0f, 90.0f);
            GdipAddPathArc(path, x + rw - d, y, d, d, 270.0f, 90.0f);
            GdipAddPathArc(path, x + rw - d, y + rh - d, d, d, 0.0f, 90.0f);
            GdipAddPathArc(path, x, y + rh - d, d, d, 90.0f, 90.0f);
            GdipClosePathFigure(path);
            GdipDrawPath(graphics, pen, path);
            GdipDeletePath(path);
        }
        if (pen) GdipDeletePen(pen);
        GdipDeleteGraphics(graphics);
        return;
    }
    {
        HPEN pen = CreatePen(PS_SOLID, S(width), color);
        HGDIOBJ old_pen = SelectObject(dc, pen);
        HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        RoundRect(dc, r.left, r.top, r.right, r.bottom, S(radius) * 2, S(radius) * 2);
        SelectObject(dc, old_brush);
        SelectObject(dc, old_pen);
        DeleteObject(pen);
    }
}

static void rounded(HDC dc, RECT r, int radius, COLORREF fill_color, COLORREF line_color) {
    if (gdiplus_token) gdiplus_round(dc, r, radius, fill_color, line_color);
    else gdi_round(dc, r, radius, fill_color, line_color);
}

static void draw_text(HDC dc, RECT r, const wchar_t *text, HFONT font, COLORREF color, UINT format, int line_height) {
    HGDIOBJ old = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    if (line_height > 0) {
        RECT line = r;
        const wchar_t *p = text;
        while (*p && line.top < r.bottom) {
            const wchar_t *end = wcschr(p, L'\n');
            size_t len = end ? (size_t)(end - p) : wcslen(p);
            wchar_t temp[2048];
            if (len >= sizeof(temp)/sizeof(temp[0])) len = sizeof(temp)/sizeof(temp[0]) - 1;
            wmemcpy(temp, p, len); temp[len] = 0;
            line.bottom = line.top + line_height;
            DrawTextW(dc, temp, -1, &line, format | DT_LEFT | DT_NOPREFIX);
            if (!end) break;
            line.top += line_height;
            p = end + 1;
        }
    } else {
        DrawTextW(dc, text, -1, &r, format | DT_NOPREFIX);
    }
    SelectObject(dc, old);
}

static void dashed_rect(HDC dc, RECT r, COLORREF color) {
    if (gdiplus_token) {
        GpGraphics *graphics = NULL;
        GpPen *pen = NULL;
        GpPath *path = NULL;
        if (GdipCreateFromHDC(dc, &graphics) == Ok && graphics) {
            REAL x = (REAL)r.left, y = (REAL)r.top;
            REAL width = (REAL)(r.right - r.left), height = (REAL)(r.bottom - r.top);
            REAL d = (REAL)S(20);
            GdipSetSmoothingMode(graphics, SmoothingModeAntiAlias);
            GdipCreatePen1((ARGB)(0xff000000u | ((ULONG)GetRValue(color) << 16) |
                                 ((ULONG)GetGValue(color) << 8) | GetBValue(color)),
                           (REAL)dpi * 1.5f / 96.0f, UnitPixel, &pen);
            if (pen) GdipSetPenDashStyle(pen, DashStyleDash);
            if (GdipCreatePath(FillModeWinding, &path) == Ok && path) {
                GdipAddPathArc(path, x, y, d, d, 180.0f, 90.0f);
                GdipAddPathArc(path, x + width - d, y, d, d, 270.0f, 90.0f);
                GdipAddPathArc(path, x + width - d, y + height - d, d, d, 0.0f, 90.0f);
                GdipAddPathArc(path, x, y + height - d, d, d, 90.0f, 90.0f);
                GdipClosePathFigure(path);
                if (pen) GdipDrawPath(graphics, pen, path);
                GdipDeletePath(path);
            }
            if (pen) GdipDeletePen(pen);
            GdipDeleteGraphics(graphics);
        }
        return;
    }
    {
        HPEN pen = CreatePen(PS_DOT, 1, color);
        HGDIOBJ old = SelectObject(dc, pen);
        HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        RoundRect(dc, r.left, r.top, r.right, r.bottom, S(10), S(10));
        SelectObject(dc, old_brush);
        SelectObject(dc, old);
        DeleteObject(pen);
    }
}

/* Translucent modal scrim. One GDI+ fill per paint (the R1 version issued one
 * per-pixel GDI call per pixel pair, ~800k calls per frame at 1080p). */
static void dim_rect(HDC dc, RECT r, COLORREF color, BYTE alpha)
{
    if (gdiplus_token) {
        GpGraphics *graphics = NULL;
        if (GdipCreateFromHDC(dc, &graphics) == Ok && graphics) {
            GpSolidFill *brush = NULL;
            int ok = 0;
            if (GdipCreateSolidFill(((ARGB)alpha << 24) | ((ULONG)GetRValue(color) << 16) |
                                    ((ULONG)GetGValue(color) << 8) | GetBValue(color),
                                    &brush) == Ok && brush) {
                ok = GdipFillRectangleI(graphics, (GpBrush *)brush, r.left, r.top,
                                        r.right - r.left, r.bottom - r.top) == Ok;
                GdipDeleteBrush((GpBrush *)brush);
            }
            GdipDeleteGraphics(graphics);
            if (ok) return;
        }
    }
    /* No GDI+: an opaque scrim still isolates the modal; the page is hidden. */
    fill(dc, r, color);
}

static void line(HDC dc, int x1, int y1, int x2, int y2, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ old = SelectObject(dc, pen);
    MoveToEx(dc, x1, y1, NULL);
    LineTo(dc, x2, y2);
    SelectObject(dc, old);
    DeleteObject(pen);
}

static void dot(HDC dc, int cx, int cy, int size, COLORREF color) {
    int half = size / 2;
    fill(dc, (RECT){cx - half, cy - half, cx - half + size, cy - half + size}, color);
}

static void glyph(HDC dc, int kind, RECT r, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, S100(175), color);
    int dot_size = S100(175);
    HGDIOBJ old = SelectObject(dc, pen);
    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    if (kind == 0) {
        MoveToEx(dc, r.left + S(5), r.top + S(12), NULL); LineTo(dc, r.left + S(9), r.top + S(16));
        LineTo(dc, r.left + S(19), r.top + S(6));
    } else if (kind == 1) {
        Rectangle(dc, r.left + S(3), r.top + S(13), r.left + S(21), r.top + S(20));
        Arc(dc, r.left + S(7), r.top + S(7), r.left + S(19), r.top + S(16), r.left + S(18), r.top + S(10), r.left + S(8), r.top + S(10));
        dot(dc, r.left + S(8), r.top + S(16), dot_size, color);
        dot(dc, r.left + S(12), r.top + S(16), dot_size, color);
    } else if (kind == 2) {
        MoveToEx(dc, r.left + S(12), r.top + S(4), NULL); LineTo(dc, r.left + S(12), r.top + S(15));
        MoveToEx(dc, r.left + S(7), r.top + S(10), NULL); LineTo(dc, r.left + S(12), r.top + S(15)); LineTo(dc, r.left + S(17), r.top + S(10));
        MoveToEx(dc, r.left + S(5), r.top + S(19), NULL); LineTo(dc, r.left + S(19), r.top + S(19));
    } else if (kind == 3) {
        MoveToEx(dc, r.left + S(12), r.top + S(15), NULL); LineTo(dc, r.left + S(12), r.top + S(4));
        MoveToEx(dc, r.left + S(7), r.top + S(9), NULL); LineTo(dc, r.left + S(12), r.top + S(4)); LineTo(dc, r.left + S(17), r.top + S(9));
        MoveToEx(dc, r.left + S(5), r.top + S(19), NULL); LineTo(dc, r.left + S(19), r.top + S(19));
    } else if (kind == 4) {
        MoveToEx(dc, r.left + S(14), r.top + S(3), NULL); LineTo(dc, r.left + S(7), r.top + S(3));
        LineTo(dc, r.left + S(5), r.top + S(5)); LineTo(dc, r.left + S(5), r.top + S(20));
        LineTo(dc, r.left + S(7), r.top + S(22)); LineTo(dc, r.left + S(18), r.top + S(22));
        LineTo(dc, r.left + S(20), r.top + S(20)); LineTo(dc, r.left + S(20), r.top + S(8));
        LineTo(dc, r.left + S(14), r.top + S(3));
    } else if (kind == 5) {
        Rectangle(dc, r.left + S(9), r.top + S(9), r.left + S(20), r.top + S(20));
        MoveToEx(dc, r.left + S(5), r.top + S(15), NULL); LineTo(dc, r.left + S(5), r.top + S(6)); LineTo(dc, r.left + S(14), r.top + S(6));
    } else if (kind == 6) {
        Ellipse(dc, r.left + S(3), r.top + S(3), r.left + S(21), r.top + S(21));
        MoveToEx(dc, r.left + S(3), r.top + S(12), NULL); LineTo(dc, r.left + S(21), r.top + S(12));
        Arc(dc, r.left + S(8), r.top + S(3), r.left + S(16), r.top + S(21), r.left + S(12), r.top + S(3), r.left + S(12), r.top + S(21));
    } else if (kind == 7) {
        MoveToEx(dc, r.left + S(10), r.top + S(14), NULL); LineTo(dc, r.left + S(15), r.top + S(14));
        LineTo(dc, r.left + S(19), r.top + S(10)); LineTo(dc, r.left + S(19), r.top + S(5));
        LineTo(dc, r.left + S(14), r.top + S(5)); LineTo(dc, r.left + S(10), r.top + S(9));
        MoveToEx(dc, r.left + S(14), r.top + S(10), NULL); LineTo(dc, r.left + S(9), r.top + S(10));
        LineTo(dc, r.left + S(5), r.top + S(14)); LineTo(dc, r.left + S(5), r.top + S(19));
        LineTo(dc, r.left + S(10), r.top + S(19)); LineTo(dc, r.left + S(14), r.top + S(15));
    } else if (kind == 8) {
        Ellipse(dc, r.left + S(3), r.top + S(3), r.left + S(21), r.top + S(21));
        MoveToEx(dc, r.left + S(12), r.top + S(10), NULL); LineTo(dc, r.left + S(12), r.top + S(16));
        dot(dc, r.left + S(12), r.top + S(7), dot_size, color);
    } else if (kind == 9) {
        MoveToEx(dc, r.left + S(12), r.top + S(3), NULL); LineTo(dc, r.left + S(20), r.top + S(6));
        LineTo(dc, r.left + S(20), r.top + S(12)); LineTo(dc, r.left + S(18), r.top + S(16));
        LineTo(dc, r.left + S(15), r.top + S(19)); LineTo(dc, r.left + S(12), r.top + S(21));
        LineTo(dc, r.left + S(9), r.top + S(19)); LineTo(dc, r.left + S(6), r.top + S(16));
        LineTo(dc, r.left + S(4), r.top + S(12)); LineTo(dc, r.left + S(4), r.top + S(6)); LineTo(dc, r.left + S(12), r.top + S(3));
        MoveToEx(dc, r.left + S(9), r.top + S(12), NULL); LineTo(dc, r.left + S(11), r.top + S(14)); LineTo(dc, r.left + S(15), r.top + S(9));
    } else if (kind == 10) {
        MoveToEx(dc, r.left + S(9), r.top + S(6), NULL); LineTo(dc, r.left + S(15), r.top + S(12));
        LineTo(dc, r.left + S(9), r.top + S(18));
    } else if (kind == 11) {
        MoveToEx(dc, r.left + S(6), r.top + S(6), NULL); LineTo(dc, r.left + S(18), r.top + S(18));
        MoveToEx(dc, r.left + S(18), r.top + S(6), NULL); LineTo(dc, r.left + S(6), r.top + S(18));
    } else if (kind == 12) {
        MoveToEx(dc, r.left + S(5), r.top + S(12), NULL); LineTo(dc, r.left + S(19), r.top + S(12));
        MoveToEx(dc, r.left + S(13), r.top + S(6), NULL); LineTo(dc, r.left + S(19), r.top + S(12)); LineTo(dc, r.left + S(13), r.top + S(18));
    } else if (kind == 13) {
        MoveToEx(dc, r.left + S(19), r.top + S(12), NULL); LineTo(dc, r.left + S(5), r.top + S(12));
        MoveToEx(dc, r.left + S(11), r.top + S(6), NULL); LineTo(dc, r.left + S(5), r.top + S(12)); LineTo(dc, r.left + S(11), r.top + S(18));
    } else if (kind == 14) {
        Ellipse(dc, r.left + S(3), r.top + S(3), r.left + S(21), r.top + S(21));
        MoveToEx(dc, r.left + S(12), r.top + S(11), NULL); LineTo(dc, r.left + S(12), r.top + S(16));
        dot(dc, r.left + S(12), r.top + S(8), dot_size, color);
    }
    SelectObject(dc, old);
    DeleteObject(pen);
}

static void button_draw(HDC dc, RECT r, const wchar_t *label, int primary, int disabled, int hover, int large) {
    RECT text = r;
    if (disabled) {
        rounded(dc, r, 6, primary ? colors.line : colors.soft, primary ? colors.line : colors.line);
        draw_text(dc, text, label, large ? font_big : font_body, colors.muted, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 0);
    } else {
        rounded(dc, r, 6, primary ? (hover ? colors.teal_hover : colors.teal) : (hover ? colors.soft : colors.panel),
                primary ? colors.teal : colors.line_strong);
        draw_text(dc, text, label, large ? font_big : font_body, primary ? colors.on_teal : colors.ink,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE, 0);
    }
}

static int text_px(HDC dc, HFONT font, const wchar_t *text)
{
    SIZE size = {0, 0};
    HGDIOBJ old = SelectObject(dc, font);
    if (text && *text) GetTextExtentPoint32W(dc, text, (int)wcslen(text), &size);
    SelectObject(dc, old);
    return size.cx < 0 ? 0 : (int)size.cx;
}

/* Prototype .btn: padding 0 14px (0 20px for .lg), never narrower than the
 * caller's design width, so translated labels keep their inner padding. */
static int button_width(HDC dc, const wchar_t *label, int large, int minimum)
{
    int width = text_px(dc, large ? font_big : font_body, label) + S(large ? 40 : 28);
    return width > minimum ? width : minimum;
}

/* Footer button: label plus a 14px arrow with a 6px gap, centred as a group. */
static int icon_button_width(HDC dc, const wchar_t *label, int has_icon)
{
    return text_px(dc, font_body, label) + S(28) + (has_icon ? S(20) : 0);
}

static void icon_button_draw(HDC dc, RECT r, const wchar_t *label, int primary, int disabled,
                             int hover, int icon, int icon_after)
{
    COLORREF ink = disabled ? colors.muted : primary ? colors.on_teal : colors.ink;
    int label_w = text_px(dc, font_body, label);
    int group = label_w + (icon >= 0 ? S(20) : 0);
    int x = r.left + ((r.right - r.left) - group) / 2;
    int cy = (r.top + r.bottom) / 2;
    button_draw(dc, r, L"", primary, disabled, hover, 0);
    if (icon >= 0 && !icon_after) {
        glyph(dc, icon, (RECT){x - S(5), cy - S(12), x + S(19), cy + S(12)}, ink);
        x += S(20);
    }
    draw_text(dc, (RECT){x, r.top, x + label_w + S(2), r.bottom}, label, font_body, ink,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE, 0);
    x += label_w;
    if (icon >= 0 && icon_after)
        glyph(dc, icon, (RECT){x + S(1), cy - S(12), x + S(25), cy + S(12)}, ink);
}

static void draw_logo(HDC dc, RECT r)
{
    /* prototype LOGO svg (viewBox 0 0 26 26): rect rx=7 teal;
     * path "M5.5 17.5h15 M8 17.5v-4.2a5 5 0 0 1 10 0v4.2", stroke 2, round caps;
     * circle cx=13 cy=8.2 r=1.4. No GDI pen/brush is used, so no black outline. */
    GpGraphics *graphics = NULL;
    rounded(dc, r, 7, colors.teal, colors.teal);
    if (!gdiplus_token || GdipCreateFromHDC(dc, &graphics) != Ok || !graphics) return;
    {
        REAL x = (REAL)r.left, y = (REAL)r.top;
        REAL sx = (REAL)(r.right - r.left) / 26.0f, sy = (REAL)(r.bottom - r.top) / 26.0f;
        ARGB ink = (ARGB)(0xff000000u | ((ULONG)GetRValue(colors.on_teal) << 16) |
                          ((ULONG)GetGValue(colors.on_teal) << 8) | GetBValue(colors.on_teal));
        GpPen *pen = NULL;
        GpPath *path = NULL;
        GpSolidFill *knob = NULL;
        GdipSetSmoothingMode(graphics, SmoothingModeAntiAlias);
        GdipSetPixelOffsetMode(graphics, PixelOffsetModeHalf);
        GdipCreatePen1(ink, 2.0f * sx, UnitPixel, &pen);
        if (pen) {
            GdipSetPenStartCap(pen, LineCapRound);
            GdipSetPenEndCap(pen, LineCapRound);
            GdipSetPenLineJoin(pen, LineJoinRound);
            GdipDrawLine(graphics, pen, x + 5.5f * sx, y + 17.5f * sy, x + 20.5f * sx, y + 17.5f * sy);
            if (GdipCreatePath(FillModeWinding, &path) == Ok && path) {
                GdipAddPathLine(path, x + 8.0f * sx, y + 17.5f * sy, x + 8.0f * sx, y + 13.3f * sy);
                GdipAddPathArc(path, x + 8.0f * sx, y + 8.3f * sy, 10.0f * sx, 10.0f * sy, 180.0f, 180.0f);
                GdipAddPathLine(path, x + 18.0f * sx, y + 13.3f * sy, x + 18.0f * sx, y + 17.5f * sy);
                GdipDrawPath(graphics, pen, path);
                GdipDeletePath(path);
            }
            GdipDeletePen(pen);
        }
        GdipCreateSolidFill(ink, &knob);
        if (knob) {
            GdipFillEllipse(graphics, (GpBrush *)knob, x + 11.6f * sx, y + 6.8f * sy, 2.8f * sx, 2.8f * sy);
            GdipDeleteBrush((GpBrush *)knob);
        }
    }
    GdipDeleteGraphics(graphics);
}

/* While the scrolling content column is drawn, hit rectangles are clipped to
 * the visible viewport so off-screen content can never steal clicks from the
 * app bar or the footer (r2f). */
static RECT element_clip;
static int element_clip_on;

static int add_element(el_kind kind, int x, int y, int w, int h, int index, int aux, int disabled) {
    element *e;
    if (element_count >= MAX_ELEMS) return -1;
    e = &elements[element_count++];
    e->kind = kind; e->rect.left = x; e->rect.top = y; e->rect.right = x + w; e->rect.bottom = y + h;
    if (element_clip_on) {
        RECT full = e->rect;
        if (!IntersectRect(&e->rect, &full, &element_clip)) SetRectEmpty(&e->rect);
    }
    e->index = index; e->aux = aux; e->disabled = disabled;
    e->layer = state.modal ? BB_UI_LAYER_BASE : BB_UI_LAYER_BASE;
    return element_count - 1;
}

static int add_modal_element(el_kind kind, int x, int y, int w, int h, int index, int aux, int disabled)
{
    int id = add_element(kind, x, y, w, h, index, aux, disabled);
    if (id >= 0) elements[id].layer = BB_UI_LAYER_MODAL;
    return id;
}

static int hit_element(POINT point) {
    bb_ui_hit_rect rects[MAX_ELEMS];
    int i;
    for (i = 0; i < element_count; i++) {
        rects[i].left = elements[i].rect.left; rects[i].top = elements[i].rect.top;
        rects[i].right = elements[i].rect.right; rects[i].bottom = elements[i].rect.bottom;
        rects[i].disabled = elements[i].disabled;
        rects[i].layer = elements[i].layer;
    }
    return bb_ui_hit_element(rects, sizeof(rects[0]), (size_t)element_count, state.modal, point.x, point.y);
}

/* Fit a long download URL into its box without losing the file name:
 * full URL -> host/path -> host/…/file -> …/file (then end ellipsis). */
static void url_display(HDC dc, HFONT font, const wchar_t *url, int width, wchar_t *out, size_t cap)
{
    const wchar_t *host = wcsstr(url, L"://");
    const wchar_t *slash, *base;
    host = host ? host + 3 : url;
    text_copy(out, cap, url);
    if (text_px(dc, font, out) <= width) return;
    text_copy(out, cap, host);
    if (text_px(dc, font, out) <= width) return;
    slash = wcschr(host, L'/');
    base = wcsrchr(host, L'/');
    if (!slash || !base || !base[1]) return;
    if (slash != base) {
        swprintf(out, cap, L"%.*ls/\u2026%ls", (int)(slash - host), host, base);
        if (text_px(dc, font, out) <= width) return;
    }
    swprintf(out, cap, L"\u2026%ls", base);
}

static void begin_content(RECT content, int y, int *x, int *w) {
    /* prototype .scroll { padding: 34px 48px 28px } + 760px content column */
    int available = content.right - content.left - S(CONTENT_PAD_X) * 2;
    if (available < S(320)) available = S(320);
    *w = available > S(MAX_CONTENT_WIDTH) ? S(MAX_CONTENT_WIDTH) : available;
    *x = content.left + S(CONTENT_PAD_X);
    (void)y;
}

typedef struct {
    HDC dc;
    HFONT normal;
    HFONT strong;
} bb_measure_ctx;

/* Bold run at the paragraph's own size (R2-5: was always 14px body bold). */
static HFONT strong_font_for(HFONT font)
{
    if (font == font_small) return font_small_strong;
    if (font == font_body) return font_body_strong;
    return font;
}

static unsigned ui_measure(void *opaque, const wchar_t *text, size_t length, bb_ui_style style)
{
    bb_measure_ctx *ctx = (bb_measure_ctx *)opaque;
    SIZE extent = {0, 0};
    HFONT font = style == BB_UI_STYLE_STRONG ? ctx->strong : ctx->normal;
    HGDIOBJ old = SelectObject(ctx->dc, font);
    GetTextExtentPoint32W(ctx->dc, text, (int)length, &extent);
    SelectObject(ctx->dc, old);
    return (unsigned)(extent.cx < 0 ? 0 : extent.cx);
}

/* Used only if bb_ui_wrap rejects the text (capacity/markup): plain GDI word
 * wrap so the user still sees every word instead of a blank line. */
static int fallback_paragraph(HDC dc, int x, int y, int width, const wchar_t *text,
                              HFONT font, COLORREF color, int line_height,
                              int max_height, int align, int draw)
{
    wchar_t plain[1024];
    size_t used = 0;
    const wchar_t *p = text ? text : L"";
    UINT flags = DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL |
                 (align == BB_UI_ALIGN_CENTER ? DT_CENTER : align == BB_UI_ALIGN_RIGHT ? DT_RIGHT : DT_LEFT);
    RECT r = {x, y, x + width, y + line_height};
    HGDIOBJ old;
    int height;
    for (; *p && used + 1 < sizeof(plain) / sizeof(plain[0]); p++) {
        if (*p == BB_UI_MARK_END || *p == BB_UI_MARK_STRONG || *p == BB_UI_MARK_UNDERLINE) continue;
        plain[used++] = *p;
    }
    plain[used] = 0;
    old = SelectObject(dc, font);
    DrawTextW(dc, plain, -1, &r, flags | DT_CALCRECT);
    height = r.bottom - r.top;
    if (height < line_height) height = line_height;
    if (draw) {
        r = (RECT){x, y, x + width, y + (max_height > 0 ? max_height : height)};
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, color);
        DrawTextW(dc, plain, -1, &r, flags);
    }
    SelectObject(dc, old);
    return y + height;
}

/* Returns the bottom of the paragraph. Strong runs use the bold face of the
 * same size; underline runs use accent colour plus a drawn underline that
 * also spans the gaps between consecutive underlined words. */
static int layout_paragraph(HDC dc, int x, int y, int width, const wchar_t *text,
                            HFONT font, COLORREF color, COLORREF accent, int line_height,
                            int max_height, int align, int draw)
{
    bb_ui_span spans[512];
    bb_ui_line lines[256];
    HFONT strong_font = strong_font_for(font);
    bb_measure_ctx ctx = {dc, font, strong_font};
    int span_count = 0, line_count = 0, i, j, error = -1;
    int rc = bb_ui_wrap(text, width, ui_measure, &ctx, line_height, width,
                        spans, sizeof(spans)/sizeof(spans[0]),
                        &span_count, lines, sizeof(lines)/sizeof(lines[0]),
                        &line_count, NULL, NULL, &error);
    if (rc != 0)
        return fallback_paragraph(dc, x, y, width, text, font, color, line_height,
                                  max_height, align, draw);
    if (line_count < 1) return y + line_height;
    if (draw) {
        TEXTMETRICW metrics;
        int underline_y, thickness = S100(100);
        HGDIOBJ old = SelectObject(dc, font);
        GetTextMetricsW(dc, &metrics);
        underline_y = metrics.tmAscent + S100(150);
        SetBkMode(dc, TRANSPARENT);
        for (i = 0; i < line_count; i++) {
            int line_y = y + lines[i].y;
            int line_x = x + bb_ui_line_offset(&lines[i], width, align);
            if (max_height > 0 && line_y + line_height > y + max_height) break;
            for (j = 0; j < lines[i].span_count; j++) {
                bb_ui_span *span = &spans[lines[i].first_span + j];
                int underline = span->run.style == BB_UI_STYLE_UNDERLINE;
                SelectObject(dc, span->run.style == BB_UI_STYLE_STRONG ? strong_font : font);
                SetTextColor(dc, underline ? accent : color);
                TextOutW(dc, line_x + span->x, line_y, span->run.text, (int)span->run.length);
                if (underline) {
                    int right = line_x + span->x + span->advance;
                    if (j + 1 < lines[i].span_count && span[1].run.style == BB_UI_STYLE_UNDERLINE)
                        right = line_x + span[1].x;
                    fill(dc, (RECT){line_x + span->x, line_y + underline_y,
                                    right, line_y + underline_y + thickness}, accent);
                }
            }
        }
        SelectObject(dc, old);
    }
    return y + line_count * line_height;
}

static int paragraph_ex(HDC dc, int x, int y, int width, const wchar_t *text,
                        HFONT font, COLORREF color, COLORREF accent, int line_height,
                        int bottom, int align)
{
    int max_height = bottom > y ? bottom - y : 0;
    return layout_paragraph(dc, x, y, width, text, font, color, accent,
                            line_height, max_height, align, 1);
}

static int paragraph(HDC dc, int x, int y, int width, const wchar_t *text,
                     HFONT font, COLORREF color, int line_height, int bottom)
{
    return paragraph_ex(dc, x, y, width, text, font, color, colors.teal_ink,
                        line_height, bottom, BB_UI_ALIGN_LEFT);
}

/* Same engine as paragraph(), so measured and drawn heights cannot diverge
 * (R2-4: blank lines are counted). */
static int measured_paragraph_height(HDC dc, int width, const wchar_t *text,
                                     HFONT font, int line_height)
{
    return layout_paragraph(dc, 0, 0, width, text, font, colors.ink, colors.ink,
                            line_height, 0, BB_UI_ALIGN_LEFT, 0);
}

static int modal_content_height(HDC dc)
{
    wchar_t formatted[1024];
    format_checked(formatted, 1024, T(BB_STR_ABOUT), NULL, NULL, state.step, NULL);
    return S(378) + measured_paragraph_height(dc, S(470), formatted, font_small, S(21)) + S(20);
}

static int ordered_step_card_height(HDC dc, int width, const wchar_t *const (*items)[2],
                                    int count, HFONT title_font, HFONT body_font,
                                    int body_width, int step, const wchar_t *version,
                                    const wchar_t *filename)
{
    int i, height = S(20);
    for (i = 0; i < count; i++) {
        const wchar_t *body = items[i][1];
        wchar_t formatted[1024];
        if (!format_checked(formatted, 1024, body, NULL, version, step, filename))
            text_copy(formatted, 1024, body);
        height += S(24) + S(8);
        height += measured_paragraph_height(dc, body_width, formatted, body_font, S(21));
        height += S(22);
        if (i + 1 < count) height += S(12);
    }
    (void)title_font;
    (void)width;
    return height;
}

static int eyebrow(HDC dc, int x, int y, int width, const wchar_t *text) {
    wchar_t buf[128];
    if (!format_checked(buf, 128, text, NULL, NULL, state.step, NULL)) return y;
    draw_text(dc, (RECT){x, y, x + width, y + S(20)}, buf, font_small_bold, colors.muted, DT_LEFT | DT_SINGLELINE, 0);
    return y + S(23);
}

static int heading(HDC dc, int x, int y, int width, const wchar_t *text) {
    draw_text(dc, (RECT){x, y, x + width, y + S(38)}, text, font_heading, colors.ink, DT_LEFT | DT_SINGLELINE, 0);
    return y + S(43);
}

static int lead(HDC dc, int x, int y, int width, const wchar_t *text) {
    int w = width > S(600) ? S(600) : width;
    int end = layout_paragraph(dc, x, y, w, text, font_body, colors.secondary,
                               colors.teal_ink, S(24), 0, BB_UI_ALIGN_LEFT, 1);
    return end + S(22);
}

static int draw_meta(HDC dc, int x, int y, int w, const bb_guide_model *model, int draw_titles) {
    int third = (w - S(48)) / 3;
    wchar_t size[96], short_sha[32];
    const wchar_t *keys[3] = {T(BB_STR_KVER), T(BB_STR_KSIZE), T(BB_STR_KSHA)};
    const wchar_t *values[3];
    if (draw_titles) {
        int i;
        for (i = 0; i < 3; i++) draw_text(dc, (RECT){x + i * (third + S(12)), y, x + i * (third + S(12)) + third, y + S(20)},
                                          keys[i], font_small, colors.muted, DT_LEFT | DT_SINGLELINE, 0);
        y += S(23);
    }
    swprintf(size, 96, L"%.1f MB", (double)model->bytes / 1048576.0);
    wcscpy(short_sha, model->sha256); wcscpy(short_sha + 16, L"…"); wcscpy(short_sha + 17, model->sha256 + 56);
    values[0] = model->version; values[1] = size; values[2] = short_sha;
    {
        int i;
        for (i = 0; i < 3; i++) draw_text(dc, (RECT){x + i * (third + S(12)), y, x + i * (third + S(12)) + third - S(26), y + S(24)},
                                          values[i], i == 2 ? font_mono : font_small_bold, colors.ink, DT_LEFT | DT_SINGLELINE, 0);
    }
    return y + S(28);
}

/* ASCII-only copy for values that leave the process (URLs, versions). */
static int ascii_from(const wchar_t *text, char *out, size_t cap)
{
    size_t i;
    if (!out || !cap) return 0;
    out[0] = 0;
    if (!text) return 0;
    for (i = 0; text[i]; i++) {
        if (i + 1 >= cap || text[i] < 0x20 || text[i] > 0x7e) { out[0] = 0; return 0; }
        out[i] = (char)text[i];
    }
    out[i] = 0;
    return 1;
}

static int hex_digest(const wchar_t *hex, unsigned char out[32])
{
    int i;
    for (i = 0; i < 64; i++) {
        wchar_t c = hex[i];
        int v = c >= L'0' && c <= L'9' ? c - L'0' : c >= L'a' && c <= L'f' ? c - L'a' + 10 :
                c >= L'A' && c <= L'F' ? c - L'A' + 10 : -1;
        if (v < 0) return 0;
        if (i & 1) out[i / 2] = (unsigned char)(out[i / 2] | v);
        else out[i / 2] = (unsigned char)(v << 4);
    }
    return hex[64] == 0;
}

static bbf_route model_route(const bb_guide_model *m)
{
    char url[512], version[64];
    url[0] = 0;
    if (!ascii_from(m->version, version, sizeof(version))) return BBF_ROUTE_NONE;
    if (m->url[0] && !ascii_from(m->url, url, sizeof(url))) return BBF_ROUTE_NONE;
    return bbf_route_for(url, version);
}

static void fetch_reason(wchar_t *out, size_t cap)
{
    wchar_t detail[128];
    const bb_guide_model *m = guide();
    bb_string_id id;
    detail[0] = 0;
    switch (state.fetch_error) {
    case BBF_NETWORK: id = BB_STR_ENET; break;
    case BBF_TLS: id = BB_STR_ETLS; break;
    case BBF_TIMEOUT: id = BB_STR_ETIMEOUT; break;
    case BBF_HTTP: id = BB_STR_EHTTP; swprintf(detail, 128, L"HTTP %u", state.fetch_http); break;
    case BBF_SERVER_REJECTED: id = BB_STR_EREJECTED; swprintf(detail, 128, L"%lld", state.fetch_code); break;
    case BBF_OTHER_VERSION: id = BB_STR_EOTHER; text_copy(detail, 128, state.fetch_offered); break;
    case BBF_URL: id = BB_STR_EURL; break;
    case BBF_SIZE: id = BB_STR_ESIZE; break;
    case BBF_HASH: id = BB_STR_EHASH; break;
    case BBF_FILE: id = BB_STR_EFILE; break;
    case BBF_SERIAL: id = BB_STR_NEEDSN; break;
    default: id = BB_STR_EBAD; break;
    }
    if (!format_checked(out, cap, T(id), NULL, m->version, state.step, detail))
        text_copy(out, cap, T(BB_STR_EBAD));
}

static int font_line_height(HDC dc, HFONT font)
{
    TEXTMETRICW metrics;
    HGDIOBJ old = SelectObject(dc, font);
    int height = GetTextMetricsW(dc, &metrics) ? (int)metrics.tmHeight : S(20);
    SelectObject(dc, old);
    return height + S(2);
}

static int hover_element(int id)
{
    return id >= 0 && state.hover == id && state.hover_kind == elements[id].kind;
}

static void layout_and_draw(HDC dc, RECT client) {
    RECT app = {0, 0, client.right, S(APP_HEIGHT)};
    RECT side = {0, S(APP_HEIGHT), S(SIDEBAR_WIDTH), client.bottom - S(FOOTER_HEIGHT)};
    RECT content = {S(SIDEBAR_WIDTH), S(APP_HEIGHT), client.right, client.bottom - S(FOOTER_HEIGHT)};
    RECT footer = {0, client.bottom - S(FOOTER_HEIGHT), client.right, client.bottom};
    int content_x, content_w, y = content.top - state.scroll + S(34), i;
    int content_clip;
    int scrollable = 1;
    wchar_t text[1024], text2[512], model_name[128];
    if (state.step < 1 || state.step > 6) state.step = 1;
    element_count = 0;
    sn_want_visible = 0;
    sn_view = content;
    fill(dc, client, colors.bg);
    fill(dc, app, colors.panel); line(dc, 0, app.bottom - 1, app.right, app.bottom - 1, colors.line);
    fill(dc, side, colors.panel); line(dc, side.right - 1, side.top, side.right - 1, side.bottom, colors.line);
    fill(dc, footer, colors.panel); line(dc, footer.left, footer.top, footer.right, footer.top, colors.line);
    draw_logo(dc, (RECT){S(20), S(15), S(46), S(41)});
    {
        SIZE brand_size = {0, 0}, edition_size = {0, 0};
        const wchar_t *brand = T(BB_STR_TITLE), *edition = T(BB_STR_EDITION);
        HGDIOBJ old_font = SelectObject(dc, font_logo);
        int badge_left, badge_right;
        GetTextExtentPoint32W(dc, brand, (int)wcslen(brand), &brand_size);
        SelectObject(dc, font_small_bold);
        GetTextExtentPoint32W(dc, edition, (int)wcslen(edition), &edition_size);
        SelectObject(dc, old_font);
        badge_left = S(56) + brand_size.cx + S(10);
        badge_right = badge_left + edition_size.cx + S(20);
        draw_text(dc, (RECT){S(56), S(9), badge_left, S(46)}, brand, font_logo, colors.ink, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 0);
        rounded(dc, (RECT){badge_left, S(17), badge_right, S(40)}, 9, colors.soft, colors.line);
        draw_text(dc, (RECT){badge_left, S(17), badge_right, S(40)}, edition, font_small_bold, colors.secondary, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 0);
    }
    add_element(EL_LANG_ZH, app.right - S(183), S(12), S(47), S(32), 0, 0, 0);
    add_element(EL_LANG_EN, app.right - S(136), S(12), S(47), S(32), 1, 0, 0);
    rounded(dc, (RECT){app.right - S(183), S(12), app.right - S(136), S(44)}, 6, state.lang == 0 ? colors.tint : colors.panel, colors.line_strong);
    rounded(dc, (RECT){app.right - S(136), S(12), app.right - S(89), S(44)}, 6, state.lang ? colors.tint : colors.panel, colors.line_strong);
    draw_text(dc, (RECT){app.right - S(183), S(12), app.right - S(136), S(44)}, L"中文", font_small_bold, state.lang == 0 ? colors.teal_ink : colors.secondary, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 0);
    draw_text(dc, (RECT){app.right - S(136), S(12), app.right - S(89), S(44)}, L"EN", font_small_bold, state.lang ? colors.teal_ink : colors.secondary, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 0);
    add_element(EL_SETTINGS, app.right - S(82), S(12), S(32), S(32), 0, 0, 0);
    rounded(dc, (RECT){app.right - S(82), S(12), app.right - S(50), S(44)}, 6, colors.panel, colors.panel);
    glyph(dc, 8, (RECT){app.right - S(79), S(15), app.right - S(55), S(39)}, colors.secondary);

    for (i = 1; i <= 6; i++) {
        int top = S(74) + (i - 1) * S(62);
        int selected = state.step == i;
        int done = (i == 1 && state.left_step_one) || (i == 2 && state.file == FILE_MATCHED) ||
                   (i == 3 && state.upgraded) || (i == 4 && state.gen == GEN_DONE) ||
                   (i == 5 && state.left_step_five);
        add_element(EL_STEP, S(14), top, S(220), S(56), i, 0, 0);
        rounded(dc, (RECT){S(14), top, S(234), top + S(56)}, 8, selected ? colors.tint : colors.panel, selected ? colors.tint_line : colors.panel);
        rounded(dc, (RECT){S(26), top + S(15), S(52), top + S(41)}, 13, done ? colors.teal : colors.panel, done ? colors.teal : colors.line_strong);
        if (done) glyph(dc, 0, (RECT){S(26), top + S(15), S(52), top + S(41)}, colors.on_teal);
        else { swprintf(text, 1024, L"%d", i); draw_text(dc, (RECT){S(26), top + S(15), S(52), top + S(41)}, text, font_small_bold, selected ? colors.teal_ink : colors.muted, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 0); }
        draw_text(dc, (RECT){S(64), top + S(8), S(225), top + S(30)}, step_title(i), font_big, selected ? colors.teal_ink : colors.ink, DT_LEFT | DT_SINGLELINE, 0);
        draw_text(dc, (RECT){S(64), top + S(29), S(225), top + S(49)}, step_subtitle(i), font_small, colors.muted, DT_LEFT | DT_SINGLELINE, 0);
    }
    line(dc, S(14), side.bottom - S(61), S(234), side.bottom - S(61), colors.line);
    {
        COLORREF dot = state.rules_ok ? colors.green : colors.red;
        rounded(dc, (RECT){S(20), side.bottom - S(41), S(28), side.bottom - S(33)}, 4, dot, dot);
    }
    draw_text(dc, (RECT){S(36), side.bottom - S(48), S(232), side.bottom - S(26)}, state.rules_ok ? T(BB_STR_RULESOK) : T(BB_STR_RULESBADT), font_small, colors.secondary, DT_LEFT | DT_SINGLELINE, 0);
    draw_text(dc, (RECT){S(36), side.bottom - S(27), S(232), side.bottom - S(7)}, state.rules_ok ? T(BB_STR_RULESOFF) : state.rules_message, font_small, colors.muted, DT_LEFT | DT_SINGLELINE, 0);
    add_element(scrollable ? EL_NONE : EL_NONE, content.left, content.top, content.right - content.left, content.bottom - content.top, 0, 0, 1);

    /* Everything in the content column is painted (GDI and GDI+, which inherits
     * the HDC clip) only inside the viewport between app bar and footer. */
    content_clip = SaveDC(dc);
    IntersectClipRect(dc, content.left, content.top, content.right, content.bottom);
    element_clip = content; element_clip_on = 1;
    begin_content(content, y, &content_x, &content_w);
    y = eyebrow(dc, content_x, y, content_w, T(BB_STR_STEPOF));
    {
        const wchar_t *titles[] = {T(BB_STR_S1T),T(BB_STR_S2T),T(BB_STR_S3T),T(BB_STR_S4T),T(BB_STR_S5T),T(BB_STR_S6T)};
        y = heading(dc, content_x, y, content_w, titles[state.step - 1]);
    }
    if (state.step == 1) {
        /* prototype .mgrid: repeat(2, minmax(0,1fr)), gap 16px. Text column is
         * card - 72px (icon) - 44px (radio + gaps); a version line that does not
         * fit wraps and the whole row grows instead of being clipped. */
        int columns, col_w, row_count, row_h[16], row_top[16];
        _Static_assert(BB_GUIDE_MODEL_COUNT <= 16, "step 1 grid supports at most 16 models");
        y = lead(dc, content_x, y, content_w, T(BB_STR_S1L));
        columns = (content_w - S(16)) / 2 >= S(250) ? 2 : 1;
        col_w = columns == 2 ? (content_w - S(16)) / 2 : content_w;
        row_count = ((int)BB_GUIDE_MODEL_COUNT + columns - 1) / columns;
        for (i = 0; i < row_count; i++) row_h[i] = S(78);
        for (i = 0; i < (int)BB_GUIDE_MODEL_COUNT; i++) {
            int lines_h;
            if (guide_available(&bb_guide_models[i]))
                format_checked(text, 1024, T(BB_STR_NEEDVER), NULL, bb_guide_models[i].version, state.step, NULL);
            else
                text_copy(text, 1024, T(BB_STR_NOTSUPPORTED));
            lines_h = measured_paragraph_height(dc, col_w - S(116), text, font_small, S(20));
            if (S(58) + lines_h > row_h[i / columns]) row_h[i / columns] = S(58) + lines_h;
        }
        for (i = 0; i < row_count; i++) row_top[i] = i ? row_top[i - 1] + row_h[i - 1] + S(16) : y;
        for (i = 0; i < (int)BB_GUIDE_MODEL_COUNT; i++) {
            int col = i % columns, row = i / columns, left = content_x + col * (col_w + S(16));
            int top = row_top[row], right = left + col_w, card_h = row_h[row];
            int mid = top + card_h / 2;
            int available = guide_available(&bb_guide_models[i]);
            add_element(EL_MODEL, left, top, right - left, card_h, i, 0, !available);
            rounded(dc, (RECT){left, top, right, top + card_h}, 10,
                    !available ? colors.soft : state.guide_model == i ? colors.tint : colors.panel,
                    !available ? colors.line : state.guide_model == i ? colors.teal : colors.line);
            if (state.guide_model == i && available)
                round_outline(dc, (RECT){left, top, right, top + card_h}, 10, colors.teal, 2);
            rounded(dc, (RECT){left + S(18), mid - S(20), left + S(58), mid + S(20)}, 8, colors.soft, colors.line);
            glyph(dc, 1, (RECT){left + S(27), mid - S(14), left + S(51), mid + S(10)}, colors.teal_ink);
            draw_text(dc, (RECT){left + S(72), top + S(14), right - S(44), top + S(36)}, bb_guide_models[i].primary, font_big,
                      available ? colors.ink : colors.muted, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS, 0);
            if (bb_guide_models[i].alias[0]) {
                SIZE primary_size = {0, 0}, alias_size = {0, 0};
                HGDIOBJ old_font = SelectObject(dc, font_big);
                GetTextExtentPoint32W(dc, bb_guide_models[i].primary,
                                      (int)wcslen(bb_guide_models[i].primary), &primary_size);
                SelectObject(dc, font_small);
                GetTextExtentPoint32W(dc, bb_guide_models[i].alias,
                                      (int)wcslen(bb_guide_models[i].alias), &alias_size);
                SelectObject(dc, old_font);
                {
                    int x2 = left + S(72) + primary_size.cx + S(8);
                    int alias_width = alias_size.cx + S(16);
                    rounded(dc, (RECT){x2, top + S(14), x2 + alias_width, top + S(33)}, 9, colors.soft, colors.line);
                    draw_text(dc, (RECT){x2, top + S(14), x2 + alias_width, top + S(33)},
                              bb_guide_models[i].alias, font_small, colors.secondary,
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE, 0);
                }
            }
            if (available)
                format_checked(text, 1024, T(BB_STR_NEEDVER), NULL, bb_guide_models[i].version, state.step, NULL);
            else
                text_copy(text, 1024, T(BB_STR_NOTSUPPORTED));
            paragraph(dc, left + S(72), top + S(41), col_w - S(116), text, font_small, colors.secondary, S(20), top + card_h - S(8));
            rounded(dc, (RECT){right - S(34), mid - S(9), right - S(16), mid + S(9)}, 9, colors.panel, state.guide_model == i ? colors.teal : colors.line_strong);
            if (state.guide_model == i) rounded(dc, (RECT){right - S(30), mid - S(5), right - S(20), mid + S(5)}, 5, colors.teal, colors.teal);
        }
        y = row_top[row_count - 1] + row_h[row_count - 1] + S(32);
        glyph(dc, 14, (RECT){content_x - S(3), y - S(2), content_x + S(21), y + S(22)}, colors.muted);
        paragraph(dc, content_x + S(28), y - S(1), content_w - S(28), T(BB_STR_S1FINE), font_small, colors.muted, S(21), y + S(70));
    } else if (state.step == 2) {
        const bb_guide_model *m = guide();
        model_display(model_name, 128, m);
        format_checked(text, 1024, T(BB_STR_S2L), model_name, NULL, state.step, NULL);
        y = lead(dc, content_x, y, content_w, text);
        {
            int left = content_x, top = y, right = content_x + content_w;
            int x0 = left + S(22), inner_r = right - S(22);
            bbf_route route = model_route(m);
            int busy = active != NULL;
            int form = route == BBF_ROUTE_QUERY && state.fetch_open && !state.fetching;
            int bottom;
            if (route == BBF_ROUTE_STATIC) {
                bottom = top + S(184);
            } else if (form) {
                int privacy_h = measured_paragraph_height(dc, inner_r - x0 - S(28), T(BB_STR_SNPRIVACY), font_small, S(20));
                bottom = top + S(128) + S(26) + S(36) + S(14) + max(S(20), privacy_h) + S(18) + S(34) + S(22);
            } else if (route == BBF_ROUTE_QUERY) {
                int open_w = icon_button_width(dc, T(BB_STR_FETCHOPEN), 1);
                int note_h = measured_paragraph_height(dc, inner_r - open_w - S(16) - x0, T(BB_STR_FETCHNOTE), font_small, S(20));
                bottom = top + S(128) + max(S(34), note_h) + S(12) + S(22) + S(18);
            } else {
                bottom = top + S(210);
            }
            rounded(dc, (RECT){left, top, right, bottom}, 10, colors.panel, colors.line);
            swprintf(text, 1024, L"%ls · %ls", T(BB_STR_TARGET), model_name);
            draw_text(dc, (RECT){x0, top + S(18), inner_r, top + S(42)}, text, font_big, colors.ink, DT_LEFT | DT_SINGLELINE, 0);
            draw_meta(dc, x0, top + S(54), right - left - S(44), m, 1);
            line(dc, x0, top + S(112), inner_r, top + S(112), colors.line);
            if (route == BBF_ROUTE_STATIC) {
                /* Reserved design: [URL  copy] [Download and verify] [Open in browser] */
                int open_w = button_width(dc, T(BB_STR_OPENBROWSER), 0, S(106));
                int dl_w = icon_button_width(dc, T(BB_STR_DIRECTDL), 1);
                int open_l = inner_r - open_w;
                int dl_l = open_l - S(10) - dl_w;
                int box_r = dl_l - S(10);
                int copy_l = box_r - S(33);
                int id;
                rounded(dc, (RECT){x0, top + S(128), box_r, top + S(162)}, 6, colors.soft, colors.line);
                glyph(dc, 6, (RECT){left + S(32), top + S(135), left + S(52), top + S(155)}, colors.secondary);
                url_display(dc, font_mono, m->url, copy_l - S(6) - (left + S(59)), text2, 512);
                draw_text(dc, (RECT){left + S(59), top + S(128), copy_l - S(6), top + S(162)}, text2, font_mono, colors.secondary, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 0);
                id = add_element(EL_COPY, copy_l, top + S(131), S(28), S(28), 0, 2, 0);
                if (hover_element(id)) rounded(dc, (RECT){copy_l, top + S(131), copy_l + S(28), top + S(159)}, 5, colors.panel, colors.line);
                glyph(dc, 5, (RECT){copy_l + S(2), top + S(133), copy_l + S(26), top + S(157)}, colors.secondary);
                id = add_element(EL_FETCH, dl_l, top + S(128), dl_w, S(34), 0, 0, busy);
                icon_button_draw(dc, (RECT){dl_l, top + S(128), dl_l + dl_w, top + S(162)}, T(BB_STR_DIRECTDL), 1, busy, hover_element(id), 2, 0);
                add_element(EL_OPEN, open_l, top + S(128), open_w, S(34), 0, 0, 0);
                button_draw(dc, (RECT){open_l, top + S(128), open_l + open_w, top + S(162)}, T(BB_STR_OPENBROWSER), 0, 0, hover_is(EL_OPEN), 0);
            } else if (form) {
                int yy = top + S(128);
                int privacy_h = measured_paragraph_height(dc, inner_r - x0 - S(28), T(BB_STR_SNPRIVACY), font_small, S(20));
                draw_text(dc, (RECT){x0, yy, inner_r, yy + S(20)}, T(BB_STR_SNLABEL), font_small, colors.secondary, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS, 0);
                yy += S(26);
                {
                    int box_r = min(inner_r, x0 + S(360));
                    int edit_h = font_line_height(dc, font_body);
                    add_element(EL_FETCH_SN, x0, yy, box_r - x0, S(36), 0, 0, 0);
                    rounded(dc, (RECT){x0, yy, box_r, yy + S(36)}, 6, colors.soft, state.sn_focus ? colors.teal : colors.line_strong);
                    sn_want = (RECT){x0 + S(10), yy + (S(36) - edit_h) / 2, box_r - S(10), yy + (S(36) - edit_h) / 2 + edit_h};
                    sn_want_visible = 1;
                }
                yy += S(36) + S(14);
                glyph(dc, 14, (RECT){x0 - S(3), yy - S(2), x0 + S(21), yy + S(22)}, colors.muted);
                paragraph(dc, x0 + S(28), yy, inner_r - x0 - S(28), T(BB_STR_SNPRIVACY), font_small, colors.muted, S(20), yy + privacy_h + S(1));
                yy += max(S(20), privacy_h) + S(18);
                {
                    int go_w = icon_button_width(dc, T(BB_STR_FETCHGO), 1);
                    int close_w = button_width(dc, T(BB_STR_CANCEL), 0, S(68));
                    int id = add_element(EL_FETCH_GO, x0, yy, go_w, S(34), 0, 0, busy);
                    icon_button_draw(dc, (RECT){x0, yy, x0 + go_w, yy + S(34)}, T(BB_STR_FETCHGO), 1, busy, hover_element(id), 2, 0);
                    add_element(EL_FETCH_CLOSE, x0 + go_w + S(10), yy, close_w, S(34), 0, 0, 0);
                    button_draw(dc, (RECT){x0 + go_w + S(10), yy, x0 + go_w + S(10) + close_w, yy + S(34)}, T(BB_STR_CANCEL), 0, 0, hover_is(EL_FETCH_CLOSE), 0);
                }
            } else if (route == BBF_ROUTE_QUERY) {
                int open_w = icon_button_width(dc, T(BB_STR_FETCHOPEN), 1);
                int open_l = inner_r - open_w;
                int note_h = measured_paragraph_height(dc, open_l - S(16) - x0, T(BB_STR_FETCHNOTE), font_small, S(20));
                int row_h = max(S(34), note_h);
                int note_top = top + S(128) + (row_h - note_h) / 2;
                int button_top = top + S(128) + (row_h - S(34)) / 2;
                int id;
                paragraph(dc, x0, note_top, open_l - S(16) - x0, T(BB_STR_FETCHNOTE), font_small, colors.secondary, S(20), note_top + note_h + S(1));
                id = add_element(EL_FETCH, open_l, button_top, open_w, S(34), 0, 0, busy);
                icon_button_draw(dc, (RECT){open_l, button_top, inner_r, button_top + S(34)}, T(BB_STR_FETCHOPEN), 1, busy, hover_element(id), 2, 0);
                format_checked(text, 1024, T(BB_STR_NOURL), model_name, m->version, state.step, NULL);
                draw_text(dc, (RECT){x0, top + S(128) + row_h + S(12), inner_r, top + S(128) + row_h + S(34)}, text, font_small, colors.muted, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS, 0);
            } else {
                rounded(dc, (RECT){x0, top + S(128), inner_r, top + S(162)}, 6, colors.soft, colors.line);
                glyph(dc, 6, (RECT){left + S(32), top + S(135), left + S(52), top + S(155)}, colors.secondary);
                draw_text(dc, (RECT){left + S(59), top + S(128), inner_r - S(10), top + S(162)}, L"—", font_mono, colors.secondary, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 0);
                format_checked(text, 1024, T(BB_STR_NOURL), model_name, m->version, state.step, NULL);
                draw_text(dc, (RECT){x0, top + S(173), inner_r, top + S(199)}, text, font_small, colors.muted, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS, 0);
            }
            y = bottom + S(16);
        }
        if (!state.fetching && state.fetch_error) {
            int retry_w = button_width(dc, T(BB_STR_RETRY), 0, S(80));
            int retry_l = content_x + content_w - S(22) - retry_w;
            int text_x = content_x + S(68), text_w = retry_l - S(16) - text_x;
            int reason_h, hint_h, top = y, bottom, id;
            wchar_t reason[1024];
            fetch_reason(reason, 1024);
            reason_h = measured_paragraph_height(dc, text_w, reason, font_small, S(21));
            hint_h = measured_paragraph_height(dc, text_w, T(BB_STR_FETCHFAILHINT), font_small, S(21));
            bottom = top + max(S(92), S(42) + reason_h + S(6) + hint_h + S(18));
            rounded(dc, (RECT){content_x, top, content_x + content_w, bottom}, 10, colors.amber_bg, colors.amber_line);
            rounded(dc, (RECT){content_x + S(22), top + S(20), content_x + S(54), top + S(52)}, 16, colors.amber, colors.amber);
            glyph(dc, 8, (RECT){content_x + S(25), top + S(23), content_x + S(51), top + S(49)}, colors.panel);
            draw_text(dc, (RECT){text_x, top + S(18), text_x + text_w, top + S(41)}, T(BB_STR_FETCHFAILT), font_big, colors.amber, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS, 0);
            paragraph(dc, text_x, top + S(42), text_w, reason, font_small, colors.secondary, S(21), top + S(42) + reason_h + S(1));
            paragraph(dc, text_x, top + S(48) + reason_h, text_w, T(BB_STR_FETCHFAILHINT), font_small, colors.muted, S(21), top + S(48) + reason_h + hint_h + S(1));
            id = add_element(state.fetch_route == BBF_ROUTE_QUERY ? EL_FETCH_GO : EL_FETCH, retry_l, top + S(18), retry_w, S(34), 1, 0, active != NULL);
            button_draw(dc, (RECT){retry_l, top + S(18), retry_l + retry_w, top + S(52)}, T(BB_STR_RETRY), 0, active != NULL, hover_element(id), 0);
            y = bottom + S(16);
        }
        if (state.fetching) {
            int top = y, bottom = top + S(132);
            int percent = state.fetch_total ? (int)(state.fetch_done * 100 / state.fetch_total) : 0;
            const wchar_t *phase = state.fetch_phase >= BBF_PHASE_FINISH ? T(BB_STR_FETCHSAVE) :
                                   state.fetch_phase == BBF_PHASE_DOWNLOAD ? T(BB_STR_DOWNLOADING) : T(BB_STR_FETCHQUERY);
            if (percent > 100) percent = 100;
            rounded(dc, (RECT){content_x, top, content_x + content_w, bottom}, 10, colors.panel, colors.line);
            rounded(dc, (RECT){content_x + S(20), top + S(22), content_x + S(56), top + S(58)}, 18, colors.tint, colors.tint);
            glyph(dc, 2, (RECT){content_x + S(26), top + S(28), content_x + S(50), top + S(52)}, colors.teal_ink);
            draw_text(dc, (RECT){content_x + S(72), top + S(18), content_x + content_w - S(100), top + S(42)}, base_name(state.fetch_path), font_big, colors.ink, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS, 0);
            draw_text(dc, (RECT){content_x + S(72), top + S(41), content_x + content_w - S(100), top + S(64)}, phase, font_small, colors.muted, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS, 0);
            {
                int cancel_w = button_width(dc, T(BB_STR_CANCEL), 0, S(68));
                int cancel_l = content_x + content_w - S(22) - cancel_w;
                add_element(EL_CANCEL, cancel_l, top + S(22), cancel_w, S(34), 0, 0, 0);
                button_draw(dc, (RECT){cancel_l, top + S(22), cancel_l + cancel_w, top + S(56)}, T(BB_STR_CANCEL), 0, 0, hover_is(EL_CANCEL), 0);
            }
            rounded(dc, (RECT){content_x + S(20), top + S(78), content_x + content_w - S(20), top + S(84)}, 3, colors.track, colors.track);
            if (percent) rounded(dc, (RECT){content_x + S(20), top + S(78), content_x + S(20) + (content_w - S(40)) * percent / 100, top + S(84)}, 3, colors.teal, colors.teal);
            if (state.fetch_phase == BBF_PHASE_DOWNLOAD)
                swprintf(text, 1024, L"%.1f / %.1f MB · %ls", (double)state.fetch_done / 1048576.0,
                         (double)state.fetch_total / 1048576.0, T(BB_STR_DLSRC));
            else
                text_copy(text, 1024, T(BB_STR_DLSRC));
            draw_text(dc, (RECT){content_x + S(20), top + S(93), content_x + content_w - S(80), top + S(116)}, text, font_small, colors.secondary, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS, 0);
            swprintf(text2, 512, L"%d%%", percent);
            draw_text(dc, (RECT){content_x + content_w - S(70), top + S(93), content_x + content_w - S(20), top + S(116)}, text2, font_small, colors.muted, DT_RIGHT | DT_SINGLELINE, 0);
            y = bottom + S(16);
        } else if (!state.rules_ok) {
            rounded(dc, (RECT){content_x, y, content_x + content_w, y + S(100)}, 8, colors.red_bg, colors.red_line);
            glyph(dc, 8, (RECT){content_x + S(16), y + S(16), content_x + S(34), y + S(34)}, colors.red);
            draw_text(dc, (RECT){content_x + S(44), y + S(13), content_x + content_w - S(16), y + S(38)}, T(BB_STR_RULESBADT), font_big, colors.red, DT_LEFT | DT_SINGLELINE, 0);
            paragraph(dc, content_x + S(44), y + S(42), content_w - S(60), T(BB_STR_RULESBADS), font_small, colors.secondary, S(21), y + S(90));
        } else if (state.file == FILE_EMPTY) {
            int top = y, bottom = top + S(174);
            add_element(EL_PICK, content_x, top, content_w, bottom - top, 0, 0, 0);
            int pick_hot = hover_is(EL_PICK);
            /* Fill only; the visible border is the dashed outline (R2-7). */
            COLORREF pick_fill = pick_hot ? colors.tint : colors.panel;
            rounded(dc, (RECT){content_x, top, content_x + content_w, bottom}, 10, pick_fill, pick_fill);
            dashed_rect(dc, (RECT){content_x + 1, top + 1, content_x + content_w - 1, bottom - 1},
                        pick_hot ? colors.teal : colors.line_strong);
            rounded(dc, (RECT){content_x + content_w / 2 - S(20), top + S(18), content_x + content_w / 2 + S(20), top + S(58)}, 20, colors.tint, colors.tint);
            glyph(dc, 3, (RECT){content_x + content_w / 2 - S(11), top + S(27), content_x + content_w / 2 + S(11), top + S(49)}, colors.teal_ink);
            draw_text(dc, (RECT){content_x, top + S(68), content_x + content_w, top + S(91)}, T(BB_STR_DROPB), font_big, colors.ink, DT_CENTER | DT_SINGLELINE, 0);
            paragraph_ex(dc, content_x + S(24), top + S(94), content_w - S(48), TM(BB_STR_DROPS), font_small,
                         colors.secondary, colors.teal_ink, S(21), top + S(126), BB_UI_ALIGN_CENTER);
            draw_text(dc, (RECT){content_x, top + S(132), content_x + content_w, top + S(154)}, T(BB_STR_DROPN), font_small, colors.muted, DT_CENTER | DT_SINGLELINE, 0);
        } else if (state.file == FILE_HASHING) {
            int top = y, bottom = top + S(132);
            rounded(dc, (RECT){content_x, top, content_x + content_w, bottom}, 10, colors.panel, colors.line);
            glyph(dc, 4, (RECT){content_x + S(20), top + S(22), content_x + S(58), top + S(60)}, colors.secondary);
            draw_text(dc, (RECT){content_x + S(72), top + S(18), content_x + content_w - S(100), top + S(42)}, base_name(state.input), font_big, colors.ink, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS, 0);
            draw_text(dc, (RECT){content_x + S(72), top + S(41), content_x + content_w - S(100), top + S(64)}, T(BB_STR_HASHING), font_small, colors.muted, DT_LEFT | DT_SINGLELINE, 0);
            {
                int cancel_w = button_width(dc, T(BB_STR_CANCEL), 0, S(68));
                int cancel_l = content_x + content_w - S(22) - cancel_w;
                add_element(EL_CANCEL, cancel_l, top + S(22), cancel_w, S(34), 0, 0, 0);
                button_draw(dc, (RECT){cancel_l, top + S(22), cancel_l + cancel_w, top + S(56)}, T(BB_STR_CANCEL), 0, 0, hover_is(EL_CANCEL), 0);
            }
            rounded(dc, (RECT){content_x + S(20), top + S(78), content_x + content_w - S(20), top + S(84)}, 3, colors.track, colors.track);
            if (state.hashing_percent) rounded(dc, (RECT){content_x + S(20), top + S(78), content_x + S(20) + (content_w - S(40)) * state.hashing_percent / 100, top + S(84)}, 3, colors.teal, colors.teal);
            draw_text(dc, (RECT){content_x + S(20), top + S(93), content_x + content_w - S(20), top + S(116)}, T(BB_STR_DROPN), font_small, colors.secondary, DT_LEFT | DT_SINGLELINE, 0);
            swprintf(text, 1024, L"%d%%", state.hashing_percent);
            draw_text(dc, (RECT){content_x + content_w - S(70), top + S(93), content_x + content_w - S(20), top + S(116)}, text, font_small, colors.muted, DT_RIGHT | DT_SINGLELINE, 0);
        } else if (state.file == FILE_MATCHED) {
            /* The title wraps (e.g. English "Confirmed: official NX30 (RX3000)
             * firmware NX30V100R012") and the rest of the card moves down. */
            int another_w = button_width(dc, T(BB_STR_ANOTHER), 0, S(108));
            int another_l = content_x + content_w - S(22) - another_w;
            int title_x = content_x + S(68), title_w = another_l - S(12) - title_x;
            int title_h, sub_h, extra, top = y, bottom;
            format_checked(text, 1024, T(BB_STR_OKTITLE), model_name, m->version, state.step, NULL);
            title_h = measured_paragraph_height(dc, title_w, text, font_big, S(24));
            sub_h = measured_paragraph_height(dc, another_l - S(16) - title_x, T(BB_STR_OKSUB), font_small, S(21));
            extra = max(0, title_h - S(24)) + max(0, sub_h - S(42));
            bottom = top + S(156) + extra;
            rounded(dc, (RECT){content_x, top, content_x + content_w, bottom}, 10, colors.green_bg, colors.green_line);
            rounded(dc, (RECT){content_x + S(22), top + S(20), content_x + S(54), top + S(52)}, 16, colors.green, colors.green);
            glyph(dc, 0, (RECT){content_x + S(25), top + S(22), content_x + S(51), top + S(50)}, colors.panel);
            paragraph(dc, title_x, top + S(18), title_w, text, font_big, colors.green, S(24), top + S(18) + title_h + S(1));
            {
                int sub_top = top + S(18) + max(S(24), title_h);
                paragraph(dc, title_x, sub_top, another_l - S(16) - title_x, T(BB_STR_OKSUB), font_small, colors.secondary, S(21), sub_top + sub_h + S(1));
            }
            add_element(EL_ANOTHER, another_l, top + S(18), another_w, S(34), 0, 0, 0);
            button_draw(dc, (RECT){another_l, top + S(18), another_l + another_w, top + S(52)}, T(BB_STR_ANOTHER), 0, 0, hover_is(EL_ANOTHER), 0);
            line(dc, content_x + S(22), top + extra + S(98), content_x + content_w - S(22), top + extra + S(98), colors.green_line);
            glyph(dc, 4, (RECT){content_x + S(22), top + extra + S(113), content_x + S(40), top + extra + S(131)}, colors.secondary);
            swprintf(text2, 512, L"%ls · %ls", base_name(state.input), T(BB_STR_SAMEAS));
            draw_text(dc, (RECT){content_x + S(48), top + extra + S(110), content_x + content_w - S(22), top + extra + S(136)}, text2, font_small, colors.secondary, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS, 0);
            y = bottom + S(16);
        } else if (state.file == FILE_REJECTED) {
            int top = y, bottom = top + S(220);
            const wchar_t *title = T(BB_STR_BADTITLE), *body = T(BB_STR_BADSUB);
            if (state.reject == REJECT_AMBIGUOUS) body = T(BB_STR_AMBIGUOUS);
            else if (state.reject == REJECT_READ) { title = T(BB_STR_READERRT); body = T(BB_STR_READERRS); }
            else if (state.reject == REJECT_PATH) { title = T(BB_STR_PATHERRT); body = T(BB_STR_PATHERRS); }
            rounded(dc, (RECT){content_x, top, content_x + content_w, bottom}, 10, colors.amber_bg, colors.amber_line);
            rounded(dc, (RECT){content_x + S(22), top + S(20), content_x + S(54), top + S(52)}, 16, colors.amber, colors.amber);
            glyph(dc, 8, (RECT){content_x + S(25), top + S(23), content_x + S(51), top + S(49)}, colors.panel);
            draw_text(dc, (RECT){content_x + S(68), top + S(18), content_x + content_w - S(24), top + S(41)}, title, font_big, colors.amber, DT_LEFT | DT_SINGLELINE, 0);
            paragraph(dc, content_x + S(68), top + S(42), content_w - S(92), body, font_small, colors.secondary, S(21), top + S(104));
            line(dc, content_x + S(22), top + S(116), content_x + content_w - S(22), top + S(116), colors.amber_line);
            {
                wchar_t full_sha[65], short_sha[32];
                digest_hex(full_sha, state.input_digest);
                wcscpy(short_sha, full_sha); wcscpy(short_sha + 16, L"…"); wcscpy(short_sha + 17, full_sha + 56);
                swprintf(text, 1024, L"%ls · %.1f MB · %ls", base_name(state.input), (double)state.input_size / 1048576.0, short_sha);
                draw_text(dc, (RECT){content_x + S(22), top + S(129), content_x + content_w - S(22), top + S(153)}, text, font_mono, colors.secondary, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS, 0);
            }
            {
                int another_w = button_width(dc, T(BB_STR_ANOTHER), 0, S(120));
                int diag_l = content_x + S(22) + another_w + S(12);
                int diag_w = button_width(dc, T(BB_STR_COPYDIAG), 0, S(140));
                add_element(EL_ANOTHER, content_x + S(22), top + S(168), another_w, S(34), 0, 0, 0);
                button_draw(dc, (RECT){content_x + S(22), top + S(168), content_x + S(22) + another_w, top + S(202)}, T(BB_STR_ANOTHER), 1, 0, hover_is(EL_ANOTHER), 0);
                add_element(EL_COPY_DIAG, diag_l, top + S(168), diag_w, S(34), 0, 6, 0);
                button_draw(dc, (RECT){diag_l, top + S(168), diag_l + diag_w, top + S(202)}, T(BB_STR_COPYDIAG), 0, 0, hover_is(EL_COPY_DIAG), 0);
            }
            y = bottom + S(18);
            if (state.auto_switched) {
                format_checked(text, 1024, T(BB_STR_AUTOSWITCHED), model_name, NULL, state.step, NULL);
                rounded(dc, (RECT){content_x, y, content_x + content_w, y + S(50)}, 8, colors.soft, colors.line);
                glyph(dc, 14, (RECT){content_x + S(16), y + S(16), content_x + S(34), y + S(34)}, colors.muted);
                draw_text(dc, (RECT){content_x + S(44), y + S(10), content_x + content_w - S(16), y + S(42)}, text, font_small, colors.secondary, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 0);
            }
        }
    } else if (state.step == 3) {
        const bb_guide_model *m = guide();
        y = lead(dc, content_x, y, content_w, T(BB_STR_S3L));
        {
            int top = y;
            int text_height;
            format_checked(text, 1024, TM(BB_STR_S3WARN), NULL, m->version, state.step, NULL);
            text_height = measured_paragraph_height(dc, content_w - S(62), text, font_small, S(23));
            rounded(dc, (RECT){content_x, top, content_x + content_w, top + max(S(52), text_height + S(28))}, 8, colors.amber_bg, colors.amber_line);
            glyph(dc, 8, (RECT){content_x + S(16), top + S(16), content_x + S(34), top + S(34)}, colors.amber);
            paragraph(dc, content_x + S(44), top + S(14), content_w - S(62), text, font_small, colors.ink, S(23), 0);
            y = top + max(S(52), text_height + S(28)) + S(16);
        }
        {
            int top = y, row;
            int body_width = content_w - S(84);
            int card_height = ordered_step_card_height(dc, content_w, state.lang ? bb_strings_en_s3steps : bb_strings_zh_s3steps, 4,
                                                        font_big, font_small, body_width, 3,
                                                        m->version, NULL);
            rounded(dc, (RECT){content_x, top, content_x + content_w, top + card_height}, 10, colors.panel, colors.line);
            {
                int row_y = top + S(20);
                for (row = 0; row < 4; row++) {
                    const wchar_t *title = state.lang ? bb_strings_en_s3steps[row][0] : bb_strings_zh_s3steps[row][0];
                    const wchar_t *body = state.lang ? bb_strings_en_s3steps[row][1] : bb_strings_zh_s3steps[row][1];
                    wchar_t formatted[1024];
                    int body_height;
                    format_checked(formatted, 1024, body, NULL, m->version, 3, NULL);
                    rounded(dc, (RECT){content_x + S(22), row_y, content_x + S(46), row_y + S(24)}, 12, colors.soft, colors.line_strong);
                    swprintf(text2, 512, L"%d", row + 1);
                    draw_text(dc, (RECT){content_x + S(22), row_y, content_x + S(46), row_y + S(24)}, text2, font_small_bold, colors.secondary, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 0);
                    draw_text(dc, (RECT){content_x + S(62), row_y - S(2), content_x + content_w - S(22), row_y + S(22)}, title, font_big, colors.ink, DT_LEFT | DT_SINGLELINE, 0);
                    body_height = measured_paragraph_height(dc, body_width, formatted, font_small, S(21));
                    paragraph(dc, content_x + S(62), row_y + S(23), body_width, formatted, font_small, colors.secondary, S(21), row_y + S(23) + body_height + S(1));
                    row_y += S(24) + S(8) + body_height + S(22);
                    if (row < 3) {
                        line(dc, content_x + S(22), row_y - S(12), content_x + content_w - S(22), row_y - S(12), colors.line);
                        row_y += S(12);
                    }
                }
            }
            y = top + card_height + S(18);
        }
    } else if (state.step == 4) {
        const bb_guide_model *m = guide();
        model_display(model_name, 128, m);
        y = lead(dc, content_x, y, content_w, T(BB_STR_S4L));
        if (state.file != FILE_MATCHED) {
            int top = y;
            int nofile_height = measured_paragraph_height(dc, content_w - S(40), T(BB_STR_NOFILEP), font_small, S(21));
            int card_height = S(82) + nofile_height + S(49) + S(16);
            int button_top = top + S(82) + nofile_height + S(12);
            rounded(dc, (RECT){content_x, top, content_x + content_w, top + card_height}, 10, colors.panel, colors.line);
            rounded(dc, (RECT){content_x + content_w / 2 - S(24), top + S(22), content_x + content_w / 2 + S(24), top + S(70)}, 24, colors.soft, colors.line);
            glyph(dc, 4, (RECT){content_x + content_w / 2 - S(12), top + S(34), content_x + content_w / 2 + S(12), top + S(58)}, colors.muted);
            draw_text(dc, (RECT){content_x + S(20), top + S(82), content_x + content_w - S(20), top + S(107)}, T(BB_STR_NOFILET), font_big, colors.ink, DT_CENTER | DT_SINGLELINE, 0);
            paragraph(dc, content_x + S(20), top + S(108), content_w - S(40),
                      T(BB_STR_NOFILEP), font_small, colors.secondary, S(21), button_top - S(4));
            {
                int go_w = button_width(dc, T(BB_STR_GOSTEP2), 0, S(108));
                int go_l = content_x + (content_w - go_w) / 2;
                add_element(EL_CHANGE_INPUT, go_l, button_top, go_w, S(34), 0, 0, 0);
                button_draw(dc, (RECT){go_l, button_top, go_l + go_w, button_top + S(34)}, T(BB_STR_GOSTEP2), 1, 0, hover_is(EL_CHANGE_INPUT), 0);
            }
            y = top + card_height + S(18);
        } else if (state.gen == GEN_RUNNING) {
            int top = y;
            rounded(dc, (RECT){content_x, top, content_x + content_w, top + S(176)}, 10, colors.panel, colors.line);
            rounded(dc, (RECT){content_x + S(20), top + S(18), content_x + S(58), top + S(56)}, 8, colors.soft, colors.line);
            glyph(dc, 2, (RECT){content_x + S(29), top + S(27), content_x + S(49), top + S(47)}, colors.secondary);
            {
                wchar_t *base = wcsrchr(state.output, L'\\');
                draw_text(dc, (RECT){content_x + S(72), top + S(20), content_x + content_w - S(20), top + S(45)}, base ? base + 1 : state.output, font_big, colors.ink, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS, 0);
            }
            draw_text(dc, (RECT){content_x + S(72), top + S(45), content_x + content_w - S(20), top + S(68)}, T(BB_STR_GENERATING), font_small, colors.secondary, DT_LEFT | DT_SINGLELINE, 0);
            rounded(dc, (RECT){content_x + S(20), top + S(93), content_x + content_w - S(20), top + S(99)}, 3, colors.track, colors.track);
            {
                int segment = (content_w - S(40)) / 4;
                int left = content_x + S(20) + (content_w - S(40) - segment) * state.building_percent / 100;
                rounded(dc, (RECT){left, top + S(93), left + segment, top + S(99)}, 3, colors.teal, colors.teal);
            }
            draw_text(dc, (RECT){content_x + S(20), top + S(118), content_x + content_w - S(80), top + S(146)}, T(BB_STR_NOCLOSE), font_small, colors.secondary, DT_LEFT | DT_SINGLELINE, 0);
            draw_text(dc, (RECT){content_x + content_w - S(74), top + S(118), content_x + content_w - S(20), top + S(146)}, L"…", font_small, colors.muted, DT_RIGHT | DT_SINGLELINE, 0);
            y = top + S(192);
        } else if (state.gen == GEN_DONE) {
            int top = y;
            rounded(dc, (RECT){content_x, top, content_x + content_w, top + S(238)}, 10, colors.green_bg, colors.green_line);
            rounded(dc, (RECT){content_x + S(22), top + S(20), content_x + S(54), top + S(52)}, 16, colors.green, colors.green);
            glyph(dc, 0, (RECT){content_x + S(25), top + S(22), content_x + S(51), top + S(50)}, colors.panel);
            draw_text(dc, (RECT){content_x + S(68), top + S(18), content_x + content_w - S(22), top + S(42)}, T(BB_STR_DONET), font_big, colors.green, DT_LEFT | DT_SINGLELINE, 0);
            paragraph(dc, content_x + S(68), top + S(43), content_w - S(90), T(BB_STR_DONES), font_small, colors.secondary, S(21), top + S(78));
            line(dc, content_x + S(22), top + S(90), content_x + content_w - S(22), top + S(90), colors.green_line);
            draw_text(dc, (RECT){content_x + S(22), top + S(103), content_x + content_w - S(22), top + S(123)}, T(BB_STR_KOUT), font_small, colors.muted, DT_LEFT | DT_SINGLELINE, 0);
            draw_text(dc, (RECT){content_x + S(94), top + S(103), content_x + content_w - S(48), top + S(126)}, state.output, font_mono, colors.ink, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS, 0);
            add_element(EL_COPY, content_x + content_w - S(42), top + S(101), S(26), S(28), 0, 4, 0);
            glyph(dc, 5, (RECT){content_x + content_w - S(36), top + S(105), content_x + content_w - S(20), top + S(121)}, colors.teal_ink);
            {
                wchar_t full[65], short_sha[32];
                digest_hex(full, projected_output_digest);
                wcscpy(short_sha, full); wcscpy(short_sha + 16, L"…"); wcscpy(short_sha + 17, full + 56);
                draw_text(dc, (RECT){content_x + S(22), top + S(137), content_x + content_w - S(22), top + S(157)}, T(BB_STR_KSHA), font_small, colors.muted, DT_LEFT | DT_SINGLELINE, 0);
                draw_text(dc, (RECT){content_x + S(94), top + S(136), content_x + content_w - S(48), top + S(158)}, short_sha, font_mono, colors.ink, DT_LEFT | DT_SINGLELINE, 0);
                add_element(EL_COPY, content_x + content_w - S(42), top + S(136), S(26), S(28), 0, 5, 0);
                glyph(dc, 5, (RECT){content_x + content_w - S(36), top + S(140), content_x + content_w - S(20), top + S(156)}, colors.teal_ink);
            }
            y = top + S(254);
            rounded(dc, (RECT){content_x, y, content_x + content_w, y + S(48)}, 8, colors.soft, colors.line);
            glyph(dc, 14, (RECT){content_x + S(16), y + S(16), content_x + S(34), y + S(34)}, colors.muted);
            draw_text(dc, (RECT){content_x + S(44), y + S(11), content_x + content_w - S(16), y + S(38)}, T(BB_STR_NOUPGRADE), font_small, colors.secondary, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 0);
        } else if (state.gen == GEN_FAILED) {
            int top = y;
            rounded(dc, (RECT){content_x, top, content_x + content_w, top + S(190)}, 10, colors.red_bg, colors.red_line);
            rounded(dc, (RECT){content_x + S(22), top + S(20), content_x + S(54), top + S(52)}, 16, colors.red, colors.red);
            glyph(dc, 8, (RECT){content_x + S(25), top + S(23), content_x + S(51), top + S(49)}, colors.panel);
            draw_text(dc, (RECT){content_x + S(68), top + S(18), content_x + content_w - S(22), top + S(42)}, T(BB_STR_GENFAILT), font_big, colors.red, DT_LEFT | DT_SINGLELINE, 0);
            paragraph(dc, content_x + S(68), top + S(43), content_w - S(90), state.last_error, font_small, colors.secondary, S(21), top + S(91));
            {
                int retry_w = button_width(dc, T(BB_STR_RETRY), 0, S(100));
                int diag_l = content_x + S(22) + retry_w + S(10);
                int diag_w = button_width(dc, T(BB_STR_COPYDIAG), 0, S(140));
                add_element(EL_GENERATE, content_x + S(22), top + S(126), retry_w, S(34), 0, 0, 0);
                button_draw(dc, (RECT){content_x + S(22), top + S(126), content_x + S(22) + retry_w, top + S(160)}, T(BB_STR_RETRY), 1, 0, hover_is(EL_GENERATE), 0);
                add_element(EL_COPY_DIAG, diag_l, top + S(126), diag_w, S(34), 0, 6, 0);
                button_draw(dc, (RECT){diag_l, top + S(126), diag_l + diag_w, top + S(160)}, T(BB_STR_COPYDIAG), 0, 0, hover_is(EL_COPY_DIAG), 0);
            }
        } else {
            int top = y;
            rounded(dc, (RECT){content_x, top, content_x + content_w, top + S(184)}, 10, colors.panel, colors.line);
            glyph(dc, 4, (RECT){content_x + S(22), top + S(17), content_x + S(58), top + S(53)}, colors.secondary);
            draw_text(dc, (RECT){content_x + S(70), top + S(14), content_x + content_w - S(110), top + S(38)}, base_name(state.input), font_big, colors.ink, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS, 0);
            swprintf(text, 1024, L"%ls · %ls · %ls", model_name, m->version, T(BB_STR_VERIFIED));
            draw_text(dc, (RECT){content_x + S(70), top + S(40), content_x + content_w - S(22), top + S(64)}, text, font_small, colors.secondary, DT_LEFT | DT_SINGLELINE, 0);
            add_element(EL_CHANGE_INPUT, content_x + content_w - S(92), top + S(17), S(70), S(28), 0, 0, 0);
            draw_text(dc, (RECT){content_x + content_w - S(92), top + S(17), content_x + content_w - S(22), top + S(45)}, T(BB_STR_CHANGE), font_small, hover_is(EL_CHANGE_INPUT) ? colors.teal_ink : colors.teal, DT_RIGHT | DT_SINGLELINE, 0);
            {
                int change_w = button_width(dc, T(BB_STR_CHANGE), 0, S(64));
                int change_l = content_x + content_w - S(22) - change_w;
                draw_text(dc, (RECT){content_x + S(22), top + S(86), content_x + content_w - S(22), top + S(108)}, T(BB_STR_OUTFILE), font_small, colors.muted, DT_LEFT | DT_SINGLELINE, 0);
                rounded(dc, (RECT){content_x + S(22), top + S(110), change_l - S(10), top + S(146)}, 6, colors.panel, colors.line_strong);
                draw_text(dc, (RECT){content_x + S(32), top + S(110), change_l - S(20), top + S(146)}, state.output, font_mono, colors.ink, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 0);
                add_element(EL_CHANGE_OUTPUT, change_l, top + S(110), change_w, S(36), 0, 0, 0);
                button_draw(dc, (RECT){change_l, top + S(110), change_l + change_w, top + S(146)}, T(BB_STR_CHANGE), 0, 0, hover_is(EL_CHANGE_OUTPUT), 0);
            }
            y = top + S(204);
            {
                int gen_w = button_width(dc, T(BB_STR_GEN), 1, S(150));
                int note_x = gen_w + S(48);
                add_element(EL_GENERATE, content_x, y, gen_w, S(40), 0, 0, 0);
                button_draw(dc, (RECT){content_x, y, content_x + gen_w, y + S(40)}, T(BB_STR_GEN), 1, 0, hover_is(EL_GENERATE), 1);
                glyph(dc, 9, (RECT){content_x + gen_w + S(20), y + S(4), content_x + gen_w + S(40), y + S(24)}, colors.muted);
                paragraph(dc, content_x + note_x, y + S(1), content_w - note_x,
                          T(BB_STR_GENFINE), font_small, colors.muted, S(20), y + S(58));
                y += max(S(58), measured_paragraph_height(dc, content_w - note_x, T(BB_STR_GENFINE), font_small, S(20)) + S(18));
            }
        }
    } else if (state.step == 5) {
        wchar_t file_name[256];
        y = lead(dc, content_x, y, content_w, T(BB_STR_S5L));
        {
            int top = y, row;
            {
                const wchar_t *base = wcsrchr(state.output, L'\\');
                text_copy(file_name, 256, base ? base + 1 : state.output);
                if (!file_name[0])  /* not generated in this session: show the default name */
                    swprintf(file_name, 256, L"%ls-%ls-BootBridge.bin", guide()->primary, guide()->version);
            }
            {
                int body_width = content_w - S(84);
                int card_height = ordered_step_card_height(dc, content_w, state.lang ? bb_strings_en_s5steps : bb_strings_zh_s5steps, 4,
                                                            font_big, font_small, body_width, 5,
                                                            guide()->version, file_name);
                int row_y = top + S(20);
                rounded(dc, (RECT){content_x, top, content_x + content_w, top + card_height}, 10, colors.panel, colors.line);
                for (row = 0; row < 4; row++) {
                    const wchar_t *title = state.lang ? bb_strings_en_s5steps[row][0] : bb_strings_zh_s5steps[row][0];
                    const wchar_t *body = state.lang ? bb_strings_en_s5steps[row][1] : bb_strings_zh_s5steps[row][1];
                    int body_height;
                    format_checked(text, 1024, body, NULL, guide()->version, 5, file_name);
                    rounded(dc, (RECT){content_x + S(22), row_y, content_x + S(46), row_y + S(24)}, 12, colors.soft, colors.line_strong);
                    swprintf(text2, 512, L"%d", row + 1);
                    draw_text(dc, (RECT){content_x + S(22), row_y, content_x + S(46), row_y + S(24)}, text2, font_small_bold, colors.secondary, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 0);
                    draw_text(dc, (RECT){content_x + S(62), row_y - S(2), content_x + content_w - S(22), row_y + S(22)}, title, font_big, colors.ink, DT_LEFT | DT_SINGLELINE, 0);
                    body_height = measured_paragraph_height(dc, body_width, text, font_small, S(21));
                    paragraph(dc, content_x + S(62), row_y + S(23), body_width, text, font_small, colors.secondary, S(21), row_y + S(23) + body_height + S(1));
                    row_y += S(24) + S(8) + body_height + S(22);
                    if (row < 3) {
                        line(dc, content_x + S(22), row_y - S(12), content_x + content_w - S(22), row_y - S(12), colors.line);
                        row_y += S(12);
                    }
                }
                y = top + card_height + S(18);
            }
        }
        rounded(dc, (RECT){content_x, y, content_x + content_w, y + S(48)}, 8, colors.soft, colors.line);
        glyph(dc, 14, (RECT){content_x + S(16), y + S(16), content_x + S(34), y + S(34)}, colors.muted);
        draw_text(dc, (RECT){content_x + S(44), y + S(11), content_x + content_w - S(16), y + S(38)}, T(BB_STR_S5INFO), font_small, colors.secondary, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 0);
    } else {
        const bb_guide_model *m = guide();
        y = lead(dc, content_x, y, content_w, T(BB_STR_S6L));
        {
            int backup_height = measured_paragraph_height(dc, content_w - S(62), TM(BB_STR_S6BACKUP), font_small, S(21));
            RECT backup = {content_x, y, content_x + content_w, y + max(S(64), backup_height + S(24))};
            rounded(dc, backup, 8, colors.tint, colors.tint_line);
            glyph(dc, 9, (RECT){content_x + S(16), y + S(16), content_x + S(36), y + S(36)}, colors.teal_ink);
            paragraph(dc, content_x + S(46), y + S(12), content_w - S(62), TM(BB_STR_S6BACKUP),
                      font_small, colors.ink, S(21), backup.bottom - S(12));
            y = backup.bottom + S(16);
        }
        {
            int top = y, row;
            int body_width = content_w - S(84);
            int card_height = ordered_step_card_height(dc, content_w, state.lang ? bb_strings_en_s6steps : bb_strings_zh_s6steps, 4,
                                                        font_big, font_small, body_width, 6,
                                                        m->version, NULL);
            int row_y = top + S(20);
            card_height += S(46);  /* row 1 link bar: 10px gap + 36px bar */
            rounded(dc, (RECT){content_x, top, content_x + content_w, top + card_height}, 10, colors.panel, colors.line);
            for (row = 0; row < 4; row++) {
                const wchar_t *title = state.lang ? bb_strings_en_s6steps[row][0] : bb_strings_zh_s6steps[row][0];
                const wchar_t *body = state.lang ? bb_strings_en_s6steps[row][1] : bb_strings_zh_s6steps[row][1];
                int body_height = measured_paragraph_height(dc, body_width, body, font_small, S(21));
                rounded(dc, (RECT){content_x + S(22), row_y, content_x + S(46), row_y + S(24)}, 12, colors.soft, colors.line_strong);
                swprintf(text2, 512, L"%d", row + 1);
                draw_text(dc, (RECT){content_x + S(22), row_y, content_x + S(46), row_y + S(24)}, text2, font_small_bold, colors.secondary, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 0);
                draw_text(dc, (RECT){content_x + S(62), row_y - S(2), content_x + content_w - S(22), row_y + S(22)}, title, font_big, colors.ink, DT_LEFT | DT_SINGLELINE, 0);
                paragraph(dc, content_x + S(62), row_y + S(23), body_width, body, font_small, colors.secondary, S(21), row_y + S(23) + body_height + S(1));
                row_y += S(24) + S(8) + body_height + S(22);
                if (row == 0) {
                    /* row_y is now body bottom + 31px; the bar starts 10px under
                     * the body and the divider keeps its usual 19px below it. */
                    int bar = row_y - S(21);
                    int copy_l = content_x + content_w - S(22) - S(36);
                    int open_w = button_width(dc, T(BB_STR_OPENBROWSER), 0, S(106));
                    int open_l = copy_l - S(8) - open_w;
                    rounded(dc, (RECT){content_x + S(62), bar, open_l - S(8), bar + S(36)}, 6, colors.soft, colors.line);
                    glyph(dc, 7, (RECT){content_x + S(72), bar + S(10), content_x + S(88), bar + S(26)}, colors.secondary);
                    draw_text(dc, (RECT){content_x + S(96), bar, open_l - S(16), bar + S(36)}, L"http://192.168.10.1/", font_mono, colors.secondary, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 0);
                    add_element(EL_OPEN, open_l, bar, open_w, S(36), 0, 0, 0);
                    button_draw(dc, (RECT){open_l, bar, open_l + open_w, bar + S(36)}, T(BB_STR_OPENBROWSER), 1, 0, hover_is(EL_OPEN), 0);
                    add_element(EL_COPY, copy_l, bar, S(36), S(36), 0, 3, 0);
                    button_draw(dc, (RECT){copy_l, bar, copy_l + S(36), bar + S(36)}, L"", 0, 0, hover_is(EL_COPY), 0);
                    glyph(dc, 5, (RECT){copy_l + S(6), bar + S(6), copy_l + S(30), bar + S(30)}, colors.teal_ink);
                    row_y += S(46);
                }
                if (row < 3) {
                    line(dc, content_x + S(22), row_y - S(12), content_x + content_w - S(22), row_y - S(12), colors.line);
                    row_y += S(12);
                }
            }
            y = top + card_height + S(16);
        }
        {
            int faq_body_width = content_w - S(62);
            wchar_t faq_intro_text[1024];
            int faq_intro;
            format_checked(faq_intro_text, 1024, T(BB_STR_FAQA), NULL, m->version, state.step, NULL);
            faq_intro = measured_paragraph_height(dc, faq_body_width, faq_intro_text, font_small, S(21));
            int faq_steps = 0, i;
            const wchar_t *const (*faq_items)[2] = state.lang ? bb_strings_en_faqSteps : bb_strings_zh_faqSteps;
            for (i = 0; i < 3; i++) faq_steps += measured_paragraph_height(dc, faq_body_width, faq_items[i][0], font_small, S(21));
            int faq_height = state.faq_open ? S(50) + faq_intro + faq_steps + S(20) : S(50);
        rounded(dc, (RECT){content_x, y, content_x + content_w, y + faq_height}, 8, colors.panel, colors.line);
        add_element(EL_FAQI, content_x, y, content_w, S(50), 0, 0, 0);
        glyph(dc, 8, (RECT){content_x + S(15), y + S(16), content_x + S(33), y + S(34)}, colors.muted);
        draw_text(dc, (RECT){content_x + S(43), y + S(11), content_x + content_w - S(40), y + S(38)}, T(BB_STR_FAQQ), font_small_bold, colors.ink, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 0);
        glyph(dc, 10, (RECT){content_x + content_w - S(34), y + S(17), content_x + content_w - S(18), y + S(33)}, colors.muted);
        if (state.faq_open) {
            format_checked(text, 1024, T(BB_STR_FAQA), NULL, m->version, state.step, NULL);
            int faq_y = y + S(50);
            paragraph(dc, content_x + S(38), faq_y, faq_body_width, text, font_small, colors.secondary, S(21), y + faq_height);
            faq_y += faq_intro;
            for (i = 0; i < 3; i++) {
                wchar_t numbered[1024];
                swprintf(numbered, 1024, L"%d. %ls", i + 1, faq_items[i][0]);
                paragraph(dc, content_x + S(48), faq_y, faq_body_width, numbered, font_small, colors.secondary, S(21), y + faq_height);
                faq_y += measured_paragraph_height(dc, faq_body_width, numbered, font_small, S(21));
            }
        }
        y += faq_height + S(16);
        }
        rounded(dc, (RECT){content_x, y, content_x + content_w, y + S(48)}, 8, colors.soft, colors.line);
        glyph(dc, 14, (RECT){content_x + S(16), y + S(16), content_x + S(34), y + S(34)}, colors.muted);
        draw_text(dc, (RECT){content_x + S(44), y + S(11), content_x + content_w - S(16), y + S(38)}, T(BB_STR_NOUPGRADE), font_small, colors.secondary, DT_LEFT | DT_VCENTER | DT_SINGLELINE, 0);
        y += S(66);
    }
    element_clip_on = 0;
    RestoreDC(dc, content_clip);
    state.content_height = y + state.scroll - content.top + S(CONTENT_PAD_BOTTOM);
    {
        int viewport = content.bottom - content.top;
        int maximum = bb_ui_clamp_scroll(INT_MAX, state.content_height, viewport);
        if (state.scroll > maximum) state.scroll = maximum;
        if (state.scroll < 0) state.scroll = 0;
        if (maximum > 0) {
            int track_top = content.top + S(6);
            int track_bottom = content.bottom - S(6);
            int track_height = track_bottom - track_top;
            int thumb_height = max(S(28), track_height * viewport / state.content_height);
            int thumb_top = track_top + (track_height - thumb_height) * state.scroll / maximum;
            RECT track = {client.right - S(10), track_top, client.right - S(4), track_bottom};
            RECT thumb = {track.left, thumb_top, track.right, thumb_top + thumb_height};
            rounded(dc, track, 3, colors.track, colors.track);
            rounded(dc, thumb, 3, colors.muted, colors.muted);
        }
    }

    {
        /* prototype .footer: padding 0 24px, gap 10px; .btn padding 0 14px, icon gap 6px */
        int show_prev = state.step > 1 && !(state.step == 4 && state.gen == GEN_RUNNING) && !state.fetching;
        int disable_next = (state.step == 3 && !state.upgraded) || (state.step == 4 && state.gen == GEN_RUNNING) ||
                           (state.step == 2 && !state.rules_ok) || state.fetching;
        int primary = !(state.step == 4 && state.gen != GEN_DONE);
        const wchar_t *next_label = state.step == 6 ? T(BB_STR_FINISH) : T(BB_STR_NEXT);
        int next_w = max(S(80), icon_button_width(dc, next_label, state.step != 6));
        int prev_w = max(S(80), icon_button_width(dc, T(BB_STR_PREV), 1));
        RECT next = {footer.right - S(24) - next_w, footer.top + S(15), footer.right - S(24), footer.top + S(49)};
        RECT prev = {next.left - S(10) - prev_w, next.top, next.left - S(10), next.bottom};
        int hint_right = (show_prev ? prev.left : next.left) - S(16);
        if (state.step == 3) {
            wchar_t check_text[512];
            int text_w;
            RECT check;
            format_checked(check_text, 512, T(BB_STR_S3CHECK), NULL, guide()->version, state.step, NULL);
            /* The confirmation sits right next to the Next button it unlocks
             * (it was easy to miss at the far left of the footer). */
            text_w = text_px(dc, font_small_strong, check_text);
            check = (RECT){max(S(24), hint_right + S(4) - (S(39) + text_w + S(14))), next.top,
                           hint_right + S(4), next.bottom};
            add_element(EL_CHECK_UPGRADE, check.left, check.top, check.right - check.left, check.bottom - check.top, 0, 0, 0);
            rounded(dc, check, 8, state.upgraded ? colors.tint : colors.panel,
                    state.upgraded || hover_is(EL_CHECK_UPGRADE) ? colors.teal : colors.line_strong);
            rounded(dc, (RECT){check.left + S(11), check.top + S(8), check.left + S(29), check.top + S(26)}, 4, state.upgraded ? colors.teal : colors.panel, colors.teal);
            if (state.upgraded) glyph(dc, 0, (RECT){check.left + S(12), check.top + S(8), check.left + S(28), check.top + S(26)}, colors.on_teal);
            draw_text(dc, (RECT){check.left + S(39), check.top, check.right - S(10), check.bottom}, check_text, font_small_strong, colors.ink, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 0);
        } else if (state.step == 4 && state.gen == GEN_RUNNING) {
            draw_text(dc, (RECT){S(24), footer.top, hint_right, footer.bottom}, T(BB_STR_NOCLOSE), font_small, colors.muted, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, 0);
        }
        if (show_prev) {
            add_element(EL_PREV, prev.left, prev.top, prev.right - prev.left, prev.bottom - prev.top, 0, 0, 0);
            icon_button_draw(dc, prev, T(BB_STR_PREV), 0, 0, hover_is(EL_PREV), 13, 0);
        }
        add_element(EL_NEXT, next.left, next.top, next.right - next.left, next.bottom - next.top, 0, 0, disable_next);
        icon_button_draw(dc, next, next_label, primary, disable_next, hover_is(EL_NEXT), state.step != 6 ? 12 : -1, 1);
    }

    if (state.step == 2 && state.file == FILE_EMPTY && state.rules_ok) {
        RECT drop = {content.left, content.top, content.right, content.bottom};
        (void)drop;
    }

    if (state.modal) {
        RECT all = client, modal;
        int modal_height;
        int old_clip;
        modal_height = modal_content_height(dc);
        bb_ui_modal_geometry geometry = bb_ui_modal_layout(
            client.right, client.bottom, S(520), S(570), S(16), modal_height);
        state.modal_scroll = bb_ui_clamp_scroll(state.modal_scroll, modal_height,
                                                geometry.bottom - geometry.top);
        state.modal_viewport = geometry.bottom - geometry.top;
        int left = geometry.left;
        int top = geometry.top;
        dim_rect(dc, all, colors.dark ? RGB(6, 10, 14) : RGB(10, 18, 24), colors.dark ? 150 : 115);
        modal = (RECT){geometry.left, geometry.top, geometry.right, geometry.bottom};
        rounded(dc, modal, 12, colors.panel, colors.line_strong);
        if (geometry.max_scroll > 0) {
            int track_top = modal.top + S(56);
            int track_bottom = modal.bottom - S(10);
            int track_height = track_bottom - track_top;
            int viewport = modal.bottom - modal.top;
            int thumb_height = max(S(24), track_height * viewport / modal_height);
            int thumb_top = track_top + (track_height - thumb_height) * state.modal_scroll / geometry.max_scroll;
            rounded(dc, (RECT){modal.right - S(8), track_top, modal.right - S(3), track_bottom},
                    2, colors.track, colors.track);
            rounded(dc, (RECT){modal.right - S(8), thumb_top, modal.right - S(3), thumb_top + thumb_height},
                    2, colors.muted, colors.muted);
        }
        draw_text(dc, (RECT){left + S(20), top + S(14), left + S(470), top + S(42)}, T(BB_STR_SETTINGS), font_brand, colors.ink, DT_LEFT | DT_SINGLELINE, 0);
        add_modal_element(EL_MODAL_CLOSE, left + S(472), top + S(14), S(30), S(30), 0, 0, 0);
        glyph(dc, 11, (RECT){left + S(480), top + S(21), left + S(496), top + S(37)}, colors.secondary);
        line(dc, left + S(20), top + S(52), left + S(500), top + S(52), colors.line);
        top -= state.modal_scroll;
        old_clip = SaveDC(dc);
        IntersectClipRect(dc, modal.left, modal.top + S(53), modal.right, modal.bottom);
        draw_text(dc, (RECT){left + S(20), top + S(66), left + S(220), top + S(86)}, T(BB_STR_MLANG), font_small, colors.muted, DT_LEFT | DT_SINGLELINE, 0);
        draw_text(dc, (RECT){left + S(270), top + S(66), left + S(470), top + S(86)}, T(BB_STR_MTHEME), font_small, colors.muted, DT_LEFT | DT_SINGLELINE, 0);
        add_modal_element(EL_LANG_ZH, left + S(20), top + S(94), S(64), S(32), 0, 0, 0);
        add_modal_element(EL_LANG_EN, left + S(84), top + S(94), S(78), S(32), 1, 0, 0);
        rounded(dc, (RECT){left + S(20), top + S(94), left + S(84), top + S(126)}, 6, state.lang == 0 ? colors.tint : colors.panel, colors.line_strong);
        rounded(dc, (RECT){left + S(84), top + S(94), left + S(162), top + S(126)}, 6, state.lang ? colors.tint : colors.panel, colors.line_strong);
        draw_text(dc, (RECT){left + S(20), top + S(94), left + S(84), top + S(126)}, L"中文", font_small, colors.ink, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 0);
        draw_text(dc, (RECT){left + S(84), top + S(94), left + S(162), top + S(126)}, L"English", font_small, colors.ink, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 0);
        {
            int k;
            const wchar_t *labels[3] = {T(BB_STR_THEMEAUTO),T(BB_STR_THEMELIGHT),T(BB_STR_THEMEDARK)};
            for (k = 0; k < 3; k++) {
                int w = k == 0 ? S(92) : S(62);
                int x = left + S(270) + (k == 0 ? 0 : k == 1 ? S(92) : S(154));
                add_modal_element(EL_THEME, x, top + S(94), w, S(32), k, 0, 0);
                rounded(dc, (RECT){x, top + S(94), x + w, top + S(126)}, 6, state.theme == k ? colors.tint : colors.panel, colors.line_strong);
                draw_text(dc, (RECT){x, top + S(94), x + w, top + S(126)}, labels[k], font_small, colors.ink, DT_CENTER | DT_VCENTER | DT_SINGLELINE, 0);
            }
        }
        line(dc, left + S(20), top + S(142), left + S(500), top + S(142), colors.line);
        draw_text(dc, (RECT){left + S(20), top + S(154), left + S(470), top + S(176)}, T(BB_STR_MUPDATE), font_small, colors.muted, DT_LEFT | DT_SINGLELINE, 0);
        rounded(dc, (RECT){left + S(20), top + S(180), left + S(500), top + S(238)}, 8, colors.panel, colors.line_strong);
        paragraph(dc, left + S(30), top + S(190), S(460), T(BB_STR_OFFLINEUPD), font_small, colors.muted, S(21), top + S(232));
        line(dc, left + S(20), top + S(254), left + S(500), top + S(254), colors.line);
        draw_text(dc, (RECT){left + S(20), top + S(268), left + S(470), top + S(290)}, T(BB_STR_MDIAG), font_small, colors.muted, DT_LEFT | DT_SINGLELINE, 0);
        /* The English hint is wider than the space left of the button; wrap it. */
        paragraph(dc, left + S(20), top + S(292), S(296), T(BB_STR_DIAGS), font_small, colors.secondary, S(19), top + S(334));
        add_modal_element(EL_COPY_DIAG, left + S(330), top + S(286), S(150), S(34), 0, 6, 0);
        button_draw(dc, (RECT){left + S(330), top + S(286), left + S(480), top + S(320)}, T(BB_STR_COPYDIAG), 0, 0, hover_is(EL_COPY_DIAG), 0);
        line(dc, left + S(20), top + S(336), left + S(500), top + S(336), colors.line);
        draw_text(dc, (RECT){left + S(20), top + S(350), left + S(470), top + S(372)}, T(BB_STR_MABOUT), font_small, colors.muted, DT_LEFT | DT_SINGLELINE, 0);
        {
            wchar_t about[1024], about_formatted[1024];
            text_copy(about, 1024, T(BB_STR_ABOUT));
            format_checked(about_formatted, 1024, about, NULL, NULL, state.step, NULL);
            paragraph(dc, left + S(20), top + S(378), S(470), about_formatted, font_small, colors.secondary, S(21), top + modal_height);
        }
        RestoreDC(dc, old_clip);
    }

    if (state.toast_until && GetTickCount() < (DWORD)state.toast_until) {
        SIZE extent = {0, 0};
        HGDIOBJ old_font = SelectObject(dc, font_small);
        int width;
        GetTextExtentPoint32W(dc, state.toast, (int)wcslen(state.toast), &extent);
        SelectObject(dc, old_font);
        width = min(S(420), max(S(120), extent.cx + S(28)));
        {
            int text_height = measured_paragraph_height(dc, width - S(20), state.toast, font_small, S(20));
            int height = max(S(35), text_height + S(12));
            RECT toast_rect = {client.right / 2 - width / 2, footer.top - height - S(10),
                               client.right / 2 + width / 2, footer.top - S(10)};
        rounded(dc, toast_rect, 8, colors.ink, colors.ink);
        paragraph(dc, toast_rect.left + S(10), toast_rect.top + S(6), width - S(20),
                  state.toast, font_small, colors.panel, S(20), toast_rect.bottom - S(6));
        }
    }
    if (focus_visible && state.keyboard_focus) {
        int focus = find_focus();
        if (focus >= 0 && !IsRectEmpty(&elements[focus].rect)) {
            RECT ring = elements[focus].rect;
            InflateRect(&ring, S(3), S(3));
            round_outline(dc, ring, 8, colors.teal, 2);
        }
    }
}

/* The SN field follows its painted box, is clipped to the content viewport
 * and hides under the settings modal or on other steps. */
static void sync_sn_edit(void)
{
    RECT visible, region;
    int show;
    if (!sn_edit) return;
    show = sn_want_visible && !state.modal && state.step == 2 && IntersectRect(&visible, &sn_want, &sn_view);
    if (!show) {
        if (IsWindowVisible(sn_edit)) {
            if (GetFocus() == sn_edit) SetFocus(main_window);
            ShowWindow(sn_edit, SW_HIDE);
        }
        return;
    }
    if (sn_fonts_generation != fonts_generation) {
        SendMessageW(sn_edit, WM_SETFONT, (WPARAM)font_body, FALSE);
        sn_fonts_generation = fonts_generation;
        SetRectEmpty(&sn_placed);
    }
    if (!EqualRect(&sn_placed, &sn_want)) {
        SetWindowPos(sn_edit, NULL, sn_want.left, sn_want.top, sn_want.right - sn_want.left,
                     sn_want.bottom - sn_want.top, SWP_NOZORDER | SWP_NOACTIVATE);
        sn_placed = sn_want;
    }
    region = (RECT){visible.left - sn_want.left, visible.top - sn_want.top,
                    visible.right - sn_want.left, visible.bottom - sn_want.top};
    if (!EqualRect(&region, &sn_region)) {
        HRGN clip = EqualRect(&visible, &sn_want) ? NULL :
                    CreateRectRgn(region.left, region.top, region.right, region.bottom);
        if (!SetWindowRgn(sn_edit, clip, TRUE) && clip) DeleteObject(clip);
        sn_region = region;
    }
    if (sn_painted_color != colors.soft) {
        sn_painted_color = colors.soft;
        InvalidateRect(sn_edit, NULL, TRUE);
    }
    if (!IsWindowVisible(sn_edit)) ShowWindow(sn_edit, SW_SHOWNA);
}

/* Enter submits, Tab/Esc return to the custom focus ring. */
static LRESULT CALLBACK sn_edit_subclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        PostMessageW(main_window, WM_APP_FETCH, 0, 0);
        return 0;
    }
    if (msg == WM_KEYDOWN && (wp == VK_TAB || wp == VK_ESCAPE)) {
        SetFocus(main_window);
        if (wp == VK_TAB) {
            state.focus_kind = EL_FETCH_SN; state.focus_index = 0;
            move_focus((GetKeyState(VK_SHIFT) & 0x8000) != 0, 1);
        }
        InvalidateRect(main_window, NULL, FALSE);
        return 0;
    }
    if (msg == WM_CHAR && (wp == L'\r' || wp == L'\t' || wp == 27)) return 0;
    return CallWindowProcW(sn_edit_proc, hwnd, msg, wp, lp);
}

static void refresh(HWND hwnd) {
    RECT client;
    HDC target, memory;
    HBITMAP bitmap;
    HGDIOBJ old;
    if (!GetClientRect(hwnd, &client)) return;
    target = GetDC(hwnd);
    memory = CreateCompatibleDC(target);
    bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
    old = SelectObject(memory, bitmap);
    if (fonts_dpi != dpi) make_fonts();
    layout_and_draw(memory, client);
    BitBlt(target, 0, 0, client.right, client.bottom, memory, 0, 0, SRCCOPY);
    SelectObject(memory, old);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(hwnd, target);
    sync_sn_edit();
}

static int choose_fresh_output(wchar_t *path, size_t cap)
{
    const bb_guide_model *m = guide();
    wchar_t directory[PATH_CAP];
    unsigned suffix = 0;
    const wchar_t *slash = wcsrchr(state.input, L'\\');
    if (!slash) return 0;
    wcsncpy(directory, state.input, (size_t)(slash - state.input + 1));
    directory[slash - state.input + 1] = 0;
    do {
        if (suffix)
            swprintf(path, cap, L"%ls%ls-%ls-BootBridge (%u).bin",
                     directory, m->primary, m->version, suffix);
        else
            swprintf(path, cap, L"%ls%ls-%ls-BootBridge.bin",
                     directory, m->primary, m->version);
        if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) return 1;
        suffix++;
    } while (suffix < 1000);
    return 0;
}

static void update_output_default(void)
{
    wchar_t candidate[PATH_CAP];
    if (!choose_fresh_output(candidate, PATH_CAP)) {
        state.output[0] = 0;
        return;
    }
    text_copy(state.output, PATH_CAP, candidate);
}

static char *utf8(const wchar_t *text) {
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, NULL, 0, NULL, NULL);
    char *out = count ? (char *)malloc((size_t)count) : NULL;
    if (out && !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, out, count, NULL, NULL)) {
        free(out); out = NULL;
    }
    return out;
}
static int cancelled(job *j) { return InterlockedCompareExchange(&j->cancel, 0, 0) != 0; }
static int fetch_cancelled(void *context) { return cancelled((job *)context); }
static void fetch_progress(void *context, int phase, uint64_t done, uint64_t total)
{
    job *j = (job *)context;
    (void)total;
    InterlockedExchange(&j->fetch_phase, phase);
    InterlockedExchange(&j->fetch_kib, (LONG)(done / 1024U));
}

static unsigned __stdcall run_job(void *arg) {
    job *j=(job *)arg;
    char *input=utf8(j->input);
    if(!input||!bb_windows_path_allowed(input))j->error=ERROR_INVALID_NAME;
    if(j->kind==JOB_SELECT){
        free(input);input=NULL;
        if(j->error)goto done;
        HANDLE file=CreateFileW(j->input,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,
                                FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,NULL);
        if(file==INVALID_HANDLE_VALUE)j->error=GetLastError();
        BY_HANDLE_FILE_INFORMATION info;DWORD got=0;unsigned char buffer[65536];bi_sha256_ctx sha;
        if(!j->error&&(!GetFileInformationByHandle(file,&info)||GetFileType(file)!=FILE_TYPE_DISK||
          (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))))j->error=ERROR_INVALID_DATA;
        bi_sha256_init(&sha);
        while(!j->error&&!cancelled(j)){
            if(!ReadFile(file,buffer,sizeof(buffer),&got,NULL)){j->error=GetLastError();break;}
            if(!got)break;
            j->size += got;
            bi_sha256_update(&sha,buffer,got);
        }
        if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
        j->cancelled=cancelled(j);
        if(!j->error&&!j->cancelled){
            bi_sha256_final(&sha,j->digest);
            j->selection=bb_select_bundle(j->catalog,j->size,j->digest,&j->bundle);
        }
    } else if(j->kind==JOB_EXPORT){
        char *out=utf8(j->output);
        if(j->error){j->result.status=BB_INVALID_PATH;j->result.system_error=(int)j->error;}
        else if(!input){j->result.status=BB_INVALID_PATH;j->result.system_error=ERROR_NO_UNICODE_TRANSLATION;}
        else if(!out){j->result.status=BB_INVALID_PATH;j->result.system_error=ERROR_NO_UNICODE_TRANSLATION;}
        else if(!j->bundle||!j->bundle->plan){j->result.status=BB_INVALID_RECIPE;}
        else j->result=bb_export_plan_bin(input,out,j->bundle->plan,1,j->bundle->plan->id);
        free(out);free(input);input=NULL;
    } else if(j->kind==JOB_FETCH){
        free(input);input=NULL;
        if(j->error){j->fetch.status=BBF_FILE;j->fetch.system_error=j->error;}
        else{
            bbf_request request;
            memset(&request,0,sizeof(request));
            request.route=j->route;request.static_url=j->url;
            request.serial=j->serial;request.target_version=j->version;request.expected_bytes=j->expected_bytes;
            memcpy(request.expected_sha256,j->expected,32);request.destination=j->input;
            request.cancelled=fetch_cancelled;request.progress=fetch_progress;request.context=j;
            j->fetch=bbf_fetch(&request);
            SecureZeroMemory(&request,sizeof(request));
        }
        SecureZeroMemory(j->serial,sizeof(j->serial));
        j->cancelled=j->fetch.status==BBF_CANCELLED;
    }
done:
    PostMessageW(j->owner,WM_JOB_DONE,0,0);
    return 0;
}

static void start_job(HWND hwnd, job *j) {
    j->owner = hwnd; j->catalog = &offline_catalog; active = j;
    worker = (HANDLE)_beginthreadex(NULL, 0, run_job, j, 0, NULL);
    if (!worker) {
        SecureZeroMemory(j, sizeof(*j)); free(j); active = NULL;
        InvalidateRect(hwnd, NULL, FALSE);
        return;
    }
    InvalidateRect(hwnd, NULL, FALSE);
}

static int file_dialog(HWND hwnd, wchar_t *path, int save) {
    OPENFILENAMEW ofn;
    wchar_t title[128];
    if (save) text_copy(title, 128, T(BB_STR_SAVETITLE));
    else swprintf(title, 128, L"%ls · %ls", T(BB_STR_DROPB), guide()->primary);
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd; ofn.lpstrFilter = L"BIN (*.bin)\0*.bin\0\0";
    ofn.lpstrFile = path; ofn.nMaxFile = PATH_CAP; ofn.lpstrDefExt = L"bin";
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST |
                (save ? (OFN_NOTESTFILECREATE | OFN_OVERWRITEPROMPT) : OFN_FILEMUSTEXIST);
    return save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
}

static void clear_selection(const wchar_t *message) {
    state.bundle = NULL; state.input[0] = 0; ++selection_epoch;
    state.file = FILE_EMPTY; state.gen = GEN_IDLE; state.input_size = 0;
    state.auto_switched = 0;
    text_copy(state.last_error, 512, message ? message : L"");
}

static void begin_hash(HWND hwnd, const wchar_t *path) {
    job *j;
    if (!state.rules_ok || active || !path || !path[0]) return;
    j = (job *)calloc(1, sizeof(*j));
    if (!j) return;
    text_copy(j->input, PATH_CAP, path);
    j->kind = JOB_SELECT;
    clear_selection(T(BB_STR_HASHING));
    state.fetch_error = 0;
    text_copy(state.input, PATH_CAP, path);
    state.file = FILE_HASHING; state.hashing_percent = 0;
    state.hash_bytes = 0;
    state.hash_total = 0;
    {
        WIN32_FILE_ATTRIBUTE_DATA attributes;
        if (GetFileAttributesExW(path, GetFileExInfoStandard, &attributes)) {
            state.hash_total = ((uint64_t)attributes.nFileSizeHigh << 32) | attributes.nFileSizeLow;
        }
    }
    j->selection_epoch = selection_epoch;
    start_job(hwnd, j);
}

static void choose_file(HWND hwnd) {
    wchar_t path[PATH_CAP] = L"";
    if (file_dialog(hwnd, path, 0)) begin_hash(hwnd, path);
}

/* The SN goes from the edit control straight into the job; it is never
 * written to settings, diagnostics, the clipboard or a log. */
static int read_serial(char *out, size_t cap)
{
    wchar_t raw[64];
    int n, start = 0, end, i, k = 0, ok = 1;
    if (!sn_edit || cap < BBN_SERIAL_MAX + 1) return 0;
    n = GetWindowTextW(sn_edit, raw, 64);
    if (n < 0) n = 0;
    end = n;
    while (start < end && (raw[start] == L' ' || raw[start] == L'\t' || raw[start] == 0x3000 || raw[start] == 0xa0)) start++;
    while (end > start && (raw[end - 1] == L' ' || raw[end - 1] == L'\t' || raw[end - 1] == 0x3000 || raw[end - 1] == 0xa0)) end--;
    for (i = start; i < end; i++) {
        wchar_t c = raw[i];
        if (c >= L'a' && c <= L'z') c = (wchar_t)(c - 32);
        if (c < 0x21 || c > 0x7e || k + 1 >= (int)cap) { ok = 0; break; }
        out[k++] = (char)c;
    }
    out[k] = 0;
    SecureZeroMemory(raw, sizeof(raw));
    if (!ok || !bbn_serial_ok(out)) { SecureZeroMemory(out, cap); return 0; }
    return 1;
}

static int fetch_default_path(wchar_t *path, size_t cap, const bb_guide_model *m)
{
    wchar_t folder[PATH_CAP];
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", folder, PATH_CAP);
    folder[0] = n && n < PATH_CAP - 16 ? folder[0] : 0;
    if (folder[0]) {
        DWORD attributes;
        wcscat(folder, L"\\Downloads");
        attributes = GetFileAttributesW(folder);
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) folder[0] = 0;
    }
    if (!folder[0]) text_copy(folder, PATH_CAP, exe_dir);
    n = (DWORD)swprintf(path, cap, L"%ls\\%ls.bin", folder, m->version);
    return n > 0 && n < cap;
}

static int fetch_save_dialog(HWND hwnd, wchar_t *path)
{
    OPENFILENAMEW ofn;
    wchar_t title[128];
    text_copy(title, 128, T(BB_STR_FETCHSAVETITLE));
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd; ofn.lpstrFilter = L"BIN (*.bin)\0*.bin\0\0";
    ofn.lpstrFile = path; ofn.nMaxFile = PATH_CAP; ofn.lpstrDefExt = L"bin";
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | OFN_NOTESTFILECREATE | OFN_OVERWRITEPROMPT;
    return GetSaveFileNameW(&ofn);
}

/* Static route: reviewed public object. Query route: ask H3C with the
 * router's real version and SN. Either way the file is kept only when its
 * size and SHA-256 equal the pinned guide model, then it is identified by
 * the offline rules exactly like a dropped file. */
static void start_fetch(HWND hwnd)
{
    const bb_guide_model *m = guide();
    bbf_route route = model_route(m);
    wchar_t path[PATH_CAP];
    job *j;
    if (active || route == BBF_ROUTE_NONE) return;
    if (route == BBF_ROUTE_QUERY && !state.fetch_open) {
        state.fetch_open = 1;
        InvalidateRect(hwnd, NULL, FALSE);
        return;
    }
    j = (job *)calloc(1, sizeof(*j));
    if (!j) return;
    if (!ascii_from(m->version, j->version, sizeof(j->version)) ||
        (route == BBF_ROUTE_STATIC && !ascii_from(m->url, j->url, sizeof(j->url))) ||
        !hex_digest(m->sha256, j->expected)) {
        free(j);
        return;
    }
    if (route == BBF_ROUTE_QUERY && !read_serial(j->serial, sizeof(j->serial))) {
        free(j);
        toast(T(BB_STR_NEEDSN));
        if (sn_edit && IsWindowVisible(sn_edit)) SetFocus(sn_edit);
        return;
    }
    j->expected_bytes = m->bytes;
    if (!fetch_default_path(path, PATH_CAP, m) || !fetch_save_dialog(hwnd, path)) {
        SecureZeroMemory(j, sizeof(*j));
        free(j);
        return;
    }
    text_copy(j->input, PATH_CAP, path);
    j->kind = JOB_FETCH; j->route = route; j->selection_epoch = selection_epoch;
    j->fetch_phase = route == BBF_ROUTE_QUERY ? BBF_PHASE_QUERY : BBF_PHASE_DOWNLOAD;
    if (sn_edit && GetFocus() == sn_edit) SetFocus(hwnd);
    state.fetching = 1; state.fetch_error = 0; state.fetch_route = (int)route;
    state.fetch_phase = j->fetch_phase;
    state.fetch_done = 0; state.fetch_total = m->bytes;
    text_copy(state.fetch_path, PATH_CAP, path);
    start_job(hwnd, j);
    if (!active) state.fetching = 0;
}

static void toggle_language(int lang) {
    state.lang = lang ? 1 : 0;
    make_fonts(); save_ini(); InvalidateRect(main_window, NULL, FALSE);
}

static void toggle_theme(int theme_id) {
    state.theme = theme_id;
    update_theme(); save_ini(); InvalidateRect(main_window, NULL, FALSE);
}

static void toggle_upgrade(void) {
    state.upgraded = !state.upgraded;
    InvalidateRect(main_window, NULL, FALSE);
}

static void set_model(int index) {
    if (index >= 0 && index < (int)BB_GUIDE_MODEL_COUNT &&
        guide_available(&bb_guide_models[index])) {
        state.guide_model = index;
        state.fetch_open = 0; state.fetch_error = 0;
        save_ini();
        if (state.file != FILE_EMPTY) clear_selection(L"");
        InvalidateRect(main_window, NULL, FALSE);
    }
}

static void modal_toggle(void) {
    state.modal = !state.modal;
    state.keyboard_focus = 0;
    InvalidateRect(main_window, NULL, FALSE);
}

static void handle_element(HWND hwnd, int index) {
    element *e;
    if (index < 0 || index >= element_count) return;
    e = &elements[index];
    if (e->disabled) return;
    switch (e->kind) {
    case EL_LANG_ZH: toggle_language(0); break;
    case EL_LANG_EN: toggle_language(1); break;
    case EL_SETTINGS: modal_toggle(); break;
    case EL_MODAL_CLOSE: if (state.modal) modal_toggle(); break;
    case EL_STEP:
        if (!active && !(state.step == 4 && state.gen == GEN_RUNNING)) {
            if (state.step == 1) state.left_step_one = 1;
            if (state.step == 5) state.left_step_five = 1;
            state.step = e->index; state.scroll = 0;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        break;
    case EL_MODEL: set_model(e->index); break;
    case EL_PICK: choose_file(hwnd); break;
    case EL_CANCEL:
        if (active && (active->kind == JOB_SELECT || active->kind == JOB_FETCH)) InterlockedExchange(&active->cancel, 1);
        break;
    case EL_ANOTHER: clear_selection(L""); InvalidateRect(hwnd, NULL, FALSE); break;
    case EL_OPEN: {
        const bb_guide_model *m = guide();
        bb_open_url(hwnd, state.step == 6 ? L"http://192.168.10.1/" : m->url);
        break;
    }
    case EL_COPY: {
        wchar_t value[PATH_CAP];
        if (e->aux == 2) { const bb_guide_model *m = guide(); text_copy(value, PATH_CAP, m->url); }
        else if (e->aux == 3) text_copy(value, PATH_CAP, L"http://192.168.10.1/");
        else if (e->aux == 4) text_copy(value, PATH_CAP, state.output);
        else if (e->aux == 5) digest_hex(value, projected_output_digest);
        else { const bb_guide_model *m = guide(); text_copy(value, PATH_CAP, m->sha256); }
        copy_clipboard(hwnd, value); toast(T(BB_STR_COPIED)); break;
    }
    case EL_CHANGE_INPUT:
        if (state.step == 4 && state.file != FILE_MATCHED) { state.step = 2; state.scroll = 0; InvalidateRect(hwnd, NULL, FALSE); }
        else choose_file(hwnd);
        break;
    case EL_CHANGE_OUTPUT:
        if (state.file == FILE_MATCHED) {
            wchar_t path[PATH_CAP];
            text_copy(path, PATH_CAP, state.output);
            if (file_dialog(hwnd, path, 1)) {
                if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES && _wcsicmp(path, state.output)) {
                    wchar_t fresh[PATH_CAP];
                    if (choose_fresh_output(fresh, PATH_CAP))
                        text_copy(path, PATH_CAP, fresh);
                    else
                        break;
                }
                text_copy(state.output, PATH_CAP, path);
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        break;
    case EL_GENERATE:
        if (state.file == FILE_MATCHED && state.bundle && !active) {
            job *j = (job *)calloc(1, sizeof(*j));
            wchar_t fresh[PATH_CAP];
            if (!j) break;
            if (GetFileAttributesW(state.output) != INVALID_FILE_ATTRIBUTES &&
                choose_fresh_output(fresh, PATH_CAP)) {
                text_copy(state.output, PATH_CAP, fresh);
            }
            text_copy(j->input, PATH_CAP, state.input); text_copy(j->output, PATH_CAP, state.output);
            j->kind = JOB_EXPORT; j->bundle = state.bundle; j->selection_epoch = selection_epoch;
            state.gen = GEN_RUNNING; state.building_percent = 0; start_job(hwnd, j);
        }
        break;
    case EL_CHECK_UPGRADE: toggle_upgrade(); break;
    case EL_THEME: toggle_theme(e->index); break;
    case EL_COPY_DIAG: {
        wchar_t diag[2048], digest[65];
        wchar_t windows_version[64];
        digest_hex(digest, state.input_digest);
        get_windows_version(windows_version, 64);
        swprintf(diag, 2048, L"BootBridge %ls\nRules: %ls\nABI: %u\nWindows: %ls\nDPI: %d\nStep: %d\nFile: %ls\nSize: %llu\nSHA-256: %ls\nError: %ls",
                 self_version, state.rules_ok ? L"R6" : L"FAILED", BB_ENGINE_REVISION,
                 windows_version,
                 dpi, state.step, state.input[0] ? state.input : L"-",
                 (unsigned long long)state.input_size, digest, state.last_error);
        redact_username(diag, 2048);
        copy_clipboard(hwnd, diag); toast(T(BB_STR_COPIED)); break;
    }
    case EL_FAQI: state.faq_open = !state.faq_open; InvalidateRect(hwnd, NULL, FALSE); break;
    case EL_NEXT:
        if (state.fetching) break;
        if (state.step == 5) state.left_step_five = 1;
        if (state.step == 1) state.left_step_one = 1;
        if (state.step == 6) PostMessageW(hwnd, WM_CLOSE, 0, 0);
        else if (!(state.step == 3 && !state.upgraded) && !(state.step == 4 && state.gen == GEN_RUNNING)) {
            state.step++; state.scroll = 0; InvalidateRect(hwnd, NULL, FALSE);
        }
        break;
    case EL_PREV:
        if (state.fetching) break;
        if (state.step > 1 && !(state.step == 4 && state.gen == GEN_RUNNING)) {
            if (state.step == 5) state.left_step_five = 1;
            state.step--; state.scroll = 0; InvalidateRect(hwnd, NULL, FALSE);
        }
        break;
    case EL_FETCH:
    case EL_FETCH_GO: start_fetch(hwnd); break;
    case EL_FETCH_CLOSE:
        state.fetch_open = 0;
        if (sn_edit && GetFocus() == sn_edit) SetFocus(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        break;
    case EL_FETCH_SN: if (sn_edit && IsWindowVisible(sn_edit)) SetFocus(sn_edit); break;
    default: break;
    }
}

static void job_done(HWND hwnd) {
    static wchar_t fetched[PATH_CAP];
    int hash_fetched = 0;
    if (active) {
        int binding_current = active->selection_epoch == selection_epoch;
        if (active->kind == JOB_FETCH) state.fetching = 0;
        WaitForSingleObject(worker, INFINITE); CloseHandle(worker); worker = NULL;
        if (!binding_current) {
            SecureZeroMemory(active, sizeof(*active)); free(active); active = NULL;
            InvalidateRect(hwnd, NULL, FALSE);
            if (closing) DestroyWindow(hwnd);
            return;
        }
        if (active->kind == JOB_SELECT) {
            if (active->cancelled) {
                clear_selection(L"");
            } else if (active->error) {
                state.file = FILE_REJECTED; state.reject = (active->error == ERROR_INVALID_NAME || active->error == ERROR_ACCESS_DENIED) ? REJECT_PATH : REJECT_READ;
                state.input_size = active->size;
                state.bundle = NULL;
            } else if (active->selection == BB_SELECT_OK && active->bundle) {
                int old_model = state.guide_model, new_model = state.guide_model, i;
                const bb_guide_model *m;
                state.bundle = active->bundle; state.input_size = active->size;
                memcpy(state.input_digest, active->digest, 32);
                state.file = FILE_MATCHED;
                for (i = 0; i < (int)BB_GUIDE_MODEL_COUNT; i++) {
                    if (bundle_matches_guide(active->bundle, &bb_guide_models[i])) { new_model = i; break; }
                }
                state.guide_model = new_model;
                state.auto_switched = new_model != old_model;
                if (state.auto_switched) save_ini();
                m = guide();
                update_output_default();
                (void)m;
            } else {
                state.file = FILE_REJECTED;
                state.reject = active->selection == BB_SELECT_AMBIGUOUS_BUNDLE ? REJECT_AMBIGUOUS : REJECT_UNSUPPORTED;
                memcpy(state.input_digest, active->digest, 32);
                state.bundle = NULL;
            }
        } else if (active->kind == JOB_FETCH) {
            if (active->fetch.status == BBF_OK) {
                text_copy(fetched, PATH_CAP, active->input);
                hash_fetched = 1;
                state.fetch_open = 0;
                if (sn_edit) SetWindowTextW(sn_edit, L"");
            } else if (active->fetch.status == BBF_CANCELLED) {
                toast(T(BB_STR_FETCHCANCELLED));
            } else {
                state.fetch_error = (int)active->fetch.status;
                state.fetch_http = active->fetch.http_status;
                state.fetch_code = active->fetch.server_code;
                if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, active->fetch.offered_version, -1,
                                         state.fetch_offered, 128))
                    text_copy(state.fetch_offered, 128, L"?");
                swprintf(state.last_error, 512, L"download %d http %u code %lld win32 %lu",
                         (int)active->fetch.status, active->fetch.http_status,
                         active->fetch.server_code, active->fetch.system_error);
            }
        } else {
            if (active->result.status == BB_OK && active->bundle) {
                state.gen = GEN_DONE;
                memcpy(projected_output_digest, active->bundle->plan->output_sha256, 32);
            } else {
                state.gen = GEN_FAILED;
                text_copy(state.last_error, 512, export_failure_message(active->result.status, active->result.system_error));
            }
        }
        SecureZeroMemory(active, sizeof(*active)); free(active); active = NULL;
        InvalidateRect(hwnd, NULL, FALSE);
        if (closing) DestroyWindow(hwnd);
        else if (hash_fetched) {
            if (state.rules_ok) begin_hash(hwnd, fetched);
            else {
                wchar_t saved[512];
                if (format_checked(saved, 512, T(BB_STR_FETCHDONE), NULL, NULL, state.step, base_name(fetched)))
                    toast(saved);
            }
        }
    }
}

static void set_theme_from_system(void) {
    if (state.theme == 0) { update_theme(); InvalidateRect(main_window, NULL, FALSE); }
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        wchar_t load_message[512] = L"";
        main_window = hwnd;
        dpi = (int)window_dpi(hwnd);
        state.guide_model = 0;
        state.rules_ok = 0;
        state.lang = system_language_is_chinese() ? 0 : 1;
        load_ini();
        if (bb_offline_rules_load_verify(&offline_catalog, load_message, sizeof(load_message)/sizeof(load_message[0])) == 0 &&
            offline_catalog.schema && offline_catalog.bundles && offline_catalog.bundle_count) {
            state.rules_ok = 1;
        } else {
            ZeroMemory(&offline_catalog, sizeof(offline_catalog));
            state.rules_ok = 0;
            text_copy(state.rules_message, 512, load_message[0] ? load_message : L"BootBridge.rules.dat");
        }
        if (!BB_GUIDE_MODEL_COUNT || !guide_available(guide())) {
            size_t model_index;
            for (model_index = 0; model_index < BB_GUIDE_MODEL_COUNT; model_index++) {
                if (guide_available(&bb_guide_models[model_index])) {
                    state.guide_model = (int)model_index;
                    break;
                }
            }
        }
        update_theme();
        make_fonts();
        sn_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL | ES_UPPERCASE,
                                  0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)SN_EDIT_ID, GetModuleHandleW(NULL), NULL);
        if (sn_edit) {
            SendMessageW(sn_edit, EM_LIMITTEXT, BBN_SERIAL_MAX, 0);
            sn_edit_proc = (WNDPROC)SetWindowLongPtrW(sn_edit, GWLP_WNDPROC, (LONG_PTR)sn_edit_subclass);
        }
        DragAcceptFiles(hwnd, TRUE);
        SetTimer(hwnd, 1, 120, NULL);
        refresh(hwnd);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        refresh(hwnd);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: InvalidateRect(hwnd, NULL, FALSE); return 0;
    case WM_GETMINMAXINFO: {
        /* R2-8: the monitor this window is on, at this window's DPI. */
        MINMAXINFO *info = (MINMAXINFO *)lp;
        UINT target_dpi = window_dpi(hwnd);
        RECT work, frame = {0, 0, MulDiv(920, (int)target_dpi, 96), MulDiv(640, (int)target_dpi, 96)};
        MONITORINFO monitor_info;
        ZeroMemory(&monitor_info, sizeof(monitor_info));
        monitor_info.cbSize = sizeof(monitor_info);
        if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor_info))
            work = monitor_info.rcWork;
        else
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        adjust_window_rect_dpi(&frame, WS_OVERLAPPEDWINDOW, FALSE, 0, target_dpi);
        info->ptMinTrackSize.x = min(frame.right - frame.left, work.right - work.left);
        info->ptMinTrackSize.y = min(frame.bottom - frame.top, work.bottom - work.top);
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (!tracking_mouse) {
            TRACKMOUSEEVENT track;
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE;
            track.hwndTrack = hwnd;
            track.dwHoverTime = 0;
            tracking_mouse = TrackMouseEvent(&track) ? 1 : 0;
        }
        int hit = hit_element(p);
        if (hit != state.hover) {
            state.hover = hit;
            state.hover_kind = hit >= 0 ? elements[hit].kind : EL_NONE;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE: tracking_mouse = 0; state.hover = -1; state.hover_kind = EL_NONE; InvalidateRect(hwnd, NULL, FALSE); return 0;
    case WM_LBUTTONDOWN: {
        POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int hit = hit_element(p);
        state.pressed = hit;
        if (sn_edit && GetFocus() == sn_edit && (hit < 0 || elements[hit].kind != EL_FETCH_SN)) SetFocus(hwnd);
        state.keyboard_focus = 0;
        focus_visible = 0;
        if (hit >= 0) {
            state.focus_kind = elements[hit].kind;
            state.focus_index = elements[hit].index;
        }
        SetCapture(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int hit = hit_element(p);
        ReleaseCapture();
        if (state.modal && hit < 0) {
            modal_toggle();
        } else if (hit >= 0 && hit == state.pressed) handle_element(hwnd, hit);
        state.pressed = -1;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        if (state.modal) {
            HDC dc = GetDC(hwnd);
            int content = modal_content_height(dc);
            int viewport = state.modal_viewport ? state.modal_viewport : S(570);
            ReleaseDC(hwnd, dc);
            state.modal_scroll = bb_ui_clamp_scroll(
                state.modal_scroll - delta * S(SCROLL_STEP) / WHEEL_DELTA,
                content, viewport);
        } else {
            state.scroll -= delta * S(SCROLL_STEP) / WHEEL_DELTA;
            clamp_scroll_now();
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_DROPFILES: {
        HDROP drop = (HDROP)wp;
        UINT count = DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0);
        wchar_t path[PATH_CAP];
        if (count == 1 && DragQueryFileW(drop, 0, path, PATH_CAP)) begin_hash(hwnd, path);
        else if (count > 1) toast(T(BB_STR_ONEFILE));
        DragFinish(drop);
        return 0;
    }
    case WM_SETTINGCHANGE:
        if (lp && !wcscmp((const wchar_t *)lp, L"ImmersiveColorSet")) set_theme_from_system();
        return 0;
    case WM_DPICHANGED: {
        RECT *suggested = (RECT *)lp;
        dpi = HIWORD(wp);
        SetWindowPos(hwnd, NULL, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        make_fonts();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_TIMER:
        if (wp == 1) {
            if (state.fetching && active && active->kind == JOB_FETCH) {
                state.fetch_phase = (int)InterlockedCompareExchange(&active->fetch_phase, 0, 0);
                state.fetch_done = (uint64_t)InterlockedCompareExchange(&active->fetch_kib, 0, 0) * 1024U;
                if (state.fetch_done > state.fetch_total) state.fetch_done = state.fetch_total;
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (state.file == FILE_HASHING && active) {
                if (state.hash_total) {
                    uint64_t bytes = active->size;
                    if (bytes > state.hash_total) bytes = state.hash_total;
                    state.hash_bytes = bytes;
                    state.hashing_percent = (int)(bytes * 100 / state.hash_total);
                    if (state.hashing_percent > 99) state.hashing_percent = 99;
                }
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (state.gen == GEN_RUNNING && active) {
                state.building_percent = (state.building_percent + 4) % 100;
                InvalidateRect(hwnd, NULL, FALSE);
            }
        } else if (wp == 2) {
            KillTimer(hwnd, 2);
            state.toast_until = 0;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE && state.modal) modal_toggle();
        else if (wp == VK_TAB) {
            move_focus((GetKeyState(VK_SHIFT) & 0x8000) != 0, 1);
            if (state.focus_kind == EL_FETCH_SN && sn_edit && IsWindowVisible(sn_edit)) {
                focus_visible = 0;
                SetFocus(sn_edit);
            }
            InvalidateRect(hwnd, NULL, FALSE);
        } else if (wp == VK_RETURN || wp == VK_SPACE) {
            int focus = find_focus();
            if (focus >= 0) handle_element(hwnd, focus);
        }
        return 0;
    case WM_CTLCOLOREDIT:
        if ((HWND)lp == sn_edit) {
            HDC edit_dc = (HDC)wp;
            if (!sn_brush || sn_brush_color != colors.soft) {
                if (sn_brush) DeleteObject(sn_brush);
                sn_brush = CreateSolidBrush(colors.soft);
                sn_brush_color = colors.soft;
            }
            SetTextColor(edit_dc, colors.ink);
            SetBkColor(edit_dc, colors.soft);
            return (LRESULT)sn_brush;
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wp) == SN_EDIT_ID && (HWND)lp == sn_edit) {
            if (HIWORD(wp) == EN_SETFOCUS || HIWORD(wp) == EN_KILLFOCUS) {
                state.sn_focus = HIWORD(wp) == EN_SETFOCUS;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        break;
    case WM_APP_FETCH: start_fetch(hwnd); return 0;
    case WM_JOB_DONE: job_done(hwnd); return 0;
    case WM_CLOSE:
        if (active) {
            closing = 1;
            if (active->kind == JOB_SELECT || active->kind == JOB_FETCH) InterlockedExchange(&active->cancel, 1);
            InvalidateRect(hwnd, NULL, FALSE);
        } else DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (sn_edit) SetWindowTextW(sn_edit, L"");
        if (sn_brush) { DeleteObject(sn_brush); sn_brush = NULL; }
        KillTimer(hwnd, 1); KillTimer(hwnd, 2); delete_fonts(); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void init_version(void) {
    wchar_t path[PATH_CAP];
    DWORD ignored, size;
    if (GetModuleFileNameW(NULL, path, PATH_CAP)) {
        size = GetFileVersionInfoSizeW(path, &ignored);
        if (size) {
            void *data = malloc(size);
            if (data && GetFileVersionInfoW(path, 0, size, data)) {
                VS_FIXEDFILEINFO *info;
                UINT length;
                if (VerQueryValueW(data, L"\\", (void **)&info, &length) && length) {
                    swprintf(self_version, 32, L"%u.%u.%u.%u", HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
                             HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
                }
            }
            free(data);
        }
    }
    if (!self_version[0]) wcscpy(self_version, L"0.4.0.0");
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command, int show) {
    WNDCLASSW cls;
    HWND hwnd;
    MSG msg;
    RECT rect;
    RECT work;
    GdiplusStartupInput gdiplus_input;
    POINT origin = {0, 0};
    int rc;
    (void)previous; (void)command;
    GetModuleFileNameW(NULL, exe_dir, PATH_CAP);
    {
        wchar_t *slash = wcsrchr(exe_dir, L'\\');
        if (slash) *slash = 0;
    }
    init_version();
    /* The wizard starts on step 1. state is zero-initialised, and step 0
     * would index titles[-1] on the first WM_CREATE paint (crash in USER32). */
    state.step = 1;
    state.hover = -1;
    state.hover_kind = EL_NONE;
    state.pressed = -1;
    state.focus_kind = EL_NONE;
    state.focus_index = -1;
    set_window_dpi_awareness();
    dpi = (int)primary_monitor_dpi();
    rect = (RECT){0, 0, MulDiv(1040, dpi, 96), MulDiv(700, dpi, 96)};
    work_area_for_point(origin, &work);
    {
        int max_width = (work.right - work.left) * 9 / 10;
        int max_height = (work.bottom - work.top) * 9 / 10;
        if (rect.right > max_width) rect.right = max_width;
        if (rect.bottom > max_height) rect.bottom = max_height;
    }
    ZeroMemory(&gdiplus_input, sizeof(gdiplus_input));
    gdiplus_input.GdiplusVersion = 1;
    if (GdiplusStartup(&gdiplus_token, &gdiplus_input, NULL) != Ok) gdiplus_token = 0;
    ZeroMemory(&cls, sizeof(cls));
    cls.lpfnWndProc = window_proc; cls.hInstance = instance; cls.lpszClassName = L"BootBridgeOfflineWizard";
    cls.hCursor = LoadCursorW(NULL, IDC_ARROW); cls.hbrBackground = NULL;
    if (!RegisterClassW(&cls)) return 1;
    adjust_window_rect_dpi(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0, (UINT)dpi);
    {
        int window_width = rect.right - rect.left;
        int window_height = rect.bottom - rect.top;
        int x = work.left + ((work.right - work.left) - window_width) / 2;
        int y = work.top + ((work.bottom - work.top) - window_height) / 2;
        hwnd = CreateWindowW(cls.lpszClassName, L"BootBridge", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             x, y, window_width, window_height,
                             NULL, NULL, instance, NULL);
    }
    if (!hwnd) return 1;
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    while ((rc = GetMessageW(&msg, NULL, 0, 0)) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (gdiplus_token) GdiplusShutdown(gdiplus_token);
    return rc == -1 ? 1 : (int)msg.wParam;
}
