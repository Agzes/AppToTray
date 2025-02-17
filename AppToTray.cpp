#include <windows.h>
#include <shellapi.h>
#include <tchar.h>
#include <dwmapi.h>
#include <vector>
#include <sstream>
#include <string>

#pragma comment(lib, "dwmapi.lib")

#define ID_TRAY_ICON 1
#define ID_EXIT 2
#define IDI_APP_ICON 101

#define ID_BIND_SETTINGS 3
#define ID_HOTKEY_TOGGLE 4
#define ID_SHOW_ALL 5
#define ID_GITHUB_LINK 6
#define ID_HIDDEN_BASE 100

HWND g_hwnd;
HINSTANCE g_hInst;
NOTIFYICONDATA g_nid;
HMENU g_hMenu;
HMENU g_hHiddenMenu;

bool g_inBindMode = false;
UINT g_hotkey = 0;
UINT g_hotkeyModifiers = 0;
bool g_hotkeyEnabled = true;
HHOOK g_hHook = NULL;
int g_nextHiddenCmd = ID_HIDDEN_BASE;

struct HiddenWindow
{
    HWND hwnd;
    DWORD pid;
    TCHAR title[256];
};

std::vector<HiddenWindow> g_hiddenWindows;

HICON CreateCustomIcon();
void ApplyDarkMode(HWND hWnd, bool enable);
void RebuildHiddenMenu();

void SaveHotkeyToRegistry()
{
    HKEY hKey;
    if (RegCreateKeyEx(HKEY_CURRENT_USER, L"Software\\Agzes\\AppToTray", 0, NULL,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        RegSetValueEx(hKey, L"HotkeyCode", 0, REG_DWORD, (BYTE*)&g_hotkey, sizeof(DWORD));
        RegSetValueEx(hKey, L"HotkeyModifiers", 0, REG_DWORD, (BYTE*)&g_hotkeyModifiers, sizeof(DWORD));
        RegCloseKey(hKey);
    }
}

void LoadHotkeyFromRegistry()
{
    HKEY hKey;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\Agzes\\AppToTray", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD size = sizeof(DWORD);
        RegQueryValueEx(hKey, L"HotkeyCode", NULL, NULL, (BYTE*)&g_hotkey, &size);
        RegQueryValueEx(hKey, L"HotkeyModifiers", NULL, NULL, (BYTE*)&g_hotkeyModifiers, &size);
        RegCloseKey(hKey);

        if (g_hotkey != 0 && g_hotkeyEnabled)
        {
            RegisterHotKey(g_hwnd, 1, g_hotkeyModifiers, g_hotkey);
        }
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);

void ShowTrayNotification(const wchar_t* title, const wchar_t* msg)
{
    NOTIFYICONDATA info = { 0 };
    info.cbSize = sizeof(NOTIFYICONDATA);
    info.hWnd = g_hwnd;
    info.uID = ID_TRAY_ICON;
    info.uFlags = NIF_INFO;
    lstrcpy(info.szInfoTitle, title);
    lstrcpy(info.szInfo, msg);
    info.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIcon(NIM_MODIFY, &info);
}

std::wstring GetKeyNameStr(UINT vkCode, UINT scanCode, DWORD flags)
{
    LONG lParam = (scanCode << 16) | ((flags & LLKHF_EXTENDED) ? (1 << 24) : 0);
    WCHAR name[128] = { 0 };
    if (GetKeyNameText(lParam, name, 128) != 0)
        return std::wstring(name);
    std::wstringstream ss;
    ss << L"VK " << vkCode;
    return ss.str();
}

HICON CreateCustomIcon()
{
    COLORREF innerColor = RGB(0x17, 0x17, 0x17);
    BITMAPV5HEADER bi = { 0 };
    bi.bV5Size = sizeof(BITMAPV5HEADER);
    bi.bV5Width = 32;
    bi.bV5Height = -32;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;
    HDC screenDC = GetDC(NULL);
    void* lpBits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(screenDC, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &lpBits, NULL, 0);
    ReleaseDC(NULL, screenDC);
    if (!hBitmap)
        return NULL;
    memset(lpBits, 0, 32 * 32 * 4);
    HDC memDC = CreateCompatibleDC(NULL);
    HGDIOBJ oldBmp = SelectObject(memDC, hBitmap);
    RECT rOuter = { 0, 0, 32, 32 };
    HBRUSH hBrush = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(memDC, &rOuter, hBrush);
    DeleteObject(hBrush);
    RECT rInner = { 2, 2, 30, 30 };
    hBrush = CreateSolidBrush(innerColor);
    FillRect(memDC, &rInner, hBrush);
    DeleteObject(hBrush);
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, RGB(255, 255, 255));
    HFONT hFont = CreateFont(15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
        DEFAULT_PITCH, _T("Arial"));
    HFONT oldFont = (HFONT)SelectObject(memDC, hFont);
    DrawText(memDC, _T("ATT"), -1, &rInner, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(memDC, oldFont);
    DeleteObject(hFont);
    HBITMAP hMono = CreateBitmap(32, 32, 1, 1, NULL);
    ICONINFO ii = { 0 };
    ii.fIcon = TRUE;
    ii.xHotspot = 0;
    ii.yHotspot = 0;
    ii.hbmMask = hMono;
    ii.hbmColor = hBitmap;
    HICON hIcon = CreateIconIndirect(&ii);
    SelectObject(memDC, oldBmp);
    DeleteDC(memDC);
    DeleteObject(hBitmap);
    DeleteObject(hMono);
    return hIcon;
}

void ApplyDarkMode(HWND hWnd, bool enable)
{
    HMODULE hUxTheme = GetModuleHandle(L"uxtheme.dll");
    if (hUxTheme)
    {
        typedef int(WINAPI* SetPreferredAppModeFunc)(int);
        typedef void(WINAPI* FlushMenuThemesFunc)(void);
        SetPreferredAppModeFunc SetPreferredAppMode = (SetPreferredAppModeFunc)GetProcAddress(hUxTheme, (LPCSTR)135);
        FlushMenuThemesFunc FlushMenuThemes = (FlushMenuThemesFunc)GetProcAddress(hUxTheme, (LPCSTR)136);
        if (SetPreferredAppMode && FlushMenuThemes)
        {
            int mode = enable ? 2 : 0;
            SetPreferredAppMode(mode);
            FlushMenuThemes();
        }
    }
    HMODULE hDwm = LoadLibrary(L"dwmapi.dll");
    if (hDwm)
    {
        typedef HRESULT(WINAPI* DwmSetWindowAttributeFunc)(HWND, DWORD, LPCVOID, DWORD);
        DwmSetWindowAttributeFunc DwmSetWindowAttributePtr = (DwmSetWindowAttributeFunc)GetProcAddress(hDwm, "DwmSetWindowAttribute");
        if (DwmSetWindowAttributePtr)
        {
            const DWORD DWMWA_USE_IMMERSIVE_DARK_MODE = 20;
            BOOL dark = enable ? TRUE : FALSE;
            HRESULT hr = DwmSetWindowAttributePtr(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
            if (FAILED(hr))
            {
                const DWORD DWMWA_USE_IMMERSIVE_DARK_MODE_FALLBACK = 19;
                DwmSetWindowAttributePtr(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE_FALLBACK, &dark, sizeof(dark));
            }
        }
        FreeLibrary(hDwm);
    }
}

void RebuildHiddenMenu()
{
    while (GetMenuItemCount(g_hHiddenMenu) > 0)
        DeleteMenu(g_hHiddenMenu, 0, MF_BYPOSITION);
    g_nextHiddenCmd = ID_HIDDEN_BASE;
    for (size_t i = 0; i < g_hiddenWindows.size(); i++)
    {
        TCHAR menuEntry[300];
        _stprintf_s(menuEntry, _T("%s (PID: %d)"), g_hiddenWindows[i].title, g_hiddenWindows[i].pid);
        AppendMenu(g_hHiddenMenu, MF_STRING, g_nextHiddenCmd++, menuEntry);
    }
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    static UINT currentModifiers = 0;
    static std::wstring currentComboText;

    if (nCode != HC_ACTION || !g_inBindMode)
        return CallNextHookEx(g_hHook, nCode, wParam, lParam);

    KBDLLHOOKSTRUCT* p = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

    if (wParam == WM_KEYDOWN)
    {
        if (p->vkCode == VK_CONTROL || p->vkCode == VK_LCONTROL || p->vkCode == VK_RCONTROL)
        {
            currentModifiers |= MOD_CONTROL;
            currentComboText = L"Ctrl+";
            return 1;
        }

        if (p->vkCode == VK_MENU || p->vkCode == VK_LMENU || p->vkCode == VK_RMENU)
        {
            currentModifiers |= MOD_ALT;
            if (currentComboText.find(L"Alt+") == std::wstring::npos)
                currentComboText += L"Alt+";
            return 1;
        }

        if (p->vkCode == VK_SHIFT || p->vkCode == VK_LSHIFT || p->vkCode == VK_RSHIFT)
        {
            currentModifiers |= MOD_SHIFT;
            if (currentComboText.find(L"Shift+") == std::wstring::npos)
                currentComboText += L"Shift+";
            return 1;
        }

        if (p->vkCode == VK_LWIN || p->vkCode == VK_RWIN)
        {
            currentModifiers |= MOD_WIN;
            if (currentComboText.find(L"Win+") == std::wstring::npos)
                currentComboText += L"Win+";
            return 1;
        }

        if (currentModifiers != 0)
        {
            g_hotkey = p->vkCode;
            g_hotkeyModifiers = currentModifiers;
            SaveHotkeyToRegistry();
            g_inBindMode = false;

            if (g_hHook)
            {
                UnhookWindowsHookEx(g_hHook);
                g_hHook = NULL;
            }

            if (g_hotkeyEnabled)
            {
                UnregisterHotKey(g_hwnd, 1);
                RegisterHotKey(g_hwnd, 1, g_hotkeyModifiers, g_hotkey);
            }

            currentComboText += GetKeyNameStr(g_hotkey, p->scanCode, p->flags);
            ShowTrayNotification(L"Hotkey Recorded", currentComboText.c_str());

            currentModifiers = 0;
            currentComboText.clear();
            return 1;
        }
    }

    return CallNextHookEx(g_hHook, nCode, wParam, lParam);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        ApplyDarkMode(hwnd, true);
        HICON hIcon = CreateCustomIcon();
        if (!hIcon)
            hIcon = LoadIcon(g_hInst, MAKEINTRESOURCE(IDI_APP_ICON));
        ZeroMemory(&g_nid, sizeof(g_nid));
        g_nid.cbSize = sizeof(NOTIFYICONDATA);
        g_nid.hWnd = hwnd;
        g_nid.uID = ID_TRAY_ICON;
        g_nid.uFlags = NIF_MESSAGE | NIF_TIP | NIF_ICON;
        g_nid.uCallbackMessage = WM_USER + 1;
        _tcscpy_s(g_nid.szTip, _T("App-To-Tray v1.0"));
        g_nid.hIcon = hIcon;
        Shell_NotifyIcon(NIM_ADD, &g_nid);
        g_hMenu = CreatePopupMenu();
        AppendMenu(g_hMenu, MF_STRING, ID_GITHUB_LINK, _T("App-To-Tray by Agzes"));
        AppendMenu(g_hMenu, MF_STRING | MF_DISABLED, 0, _T("v1.0 | With ❤️"));
        AppendMenu(g_hMenu, MF_SEPARATOR, 0, _T(""));
        AppendMenu(g_hMenu, MF_STRING, ID_BIND_SETTINGS, _T("Set bind for hide"));
        AppendMenu(g_hMenu, MF_STRING | MF_CHECKED, ID_HOTKEY_TOGGLE, _T("Hotkey Toggle"));
        AppendMenu(g_hMenu, MF_SEPARATOR, 0, _T(""));
        AppendMenu(g_hMenu, MF_STRING, ID_SHOW_ALL, _T("Show ALL"));
        g_hHiddenMenu = CreatePopupMenu();
        AppendMenu(g_hMenu, MF_POPUP, (UINT_PTR)g_hHiddenMenu, _T("Currently Hided"));
        AppendMenu(g_hMenu, MF_SEPARATOR, 0, _T(""));
        AppendMenu(g_hMenu, MF_STRING, ID_EXIT, _T("Exit"));
        break;
    }
    case WM_USER + 1:
    {
        if (LOWORD(lParam) == WM_RBUTTONUP)
        {
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            TrackPopupMenu(g_hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
        }
        break;
    }
    case WM_COMMAND:
    {
        int cmd = LOWORD(wParam);
        if (cmd == ID_GITHUB_LINK)
        {
            ShellExecute(NULL, L"open", L"https://github.com/Agzes/AppToTray", NULL, NULL, SW_SHOWNORMAL);
        }
        else if (cmd == ID_EXIT)
        {
            for (auto& hw : g_hiddenWindows)
                ShowWindow(hw.hwnd, SW_SHOW);
            g_hiddenWindows.clear();
            RebuildHiddenMenu();
            Shell_NotifyIcon(NIM_DELETE, &g_nid);
            DestroyMenu(g_hMenu);
            PostQuitMessage(0);
        }
        else if (cmd == ID_BIND_SETTINGS)
        {
            g_inBindMode = true;
            ShowTrayNotification(L"Recording Hotkey", L"Please press a key to record the hotkey...");
            g_hHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
        }
        else if (cmd == ID_HOTKEY_TOGGLE)
        {
            g_hotkeyEnabled = !g_hotkeyEnabled;
            UnregisterHotKey(hwnd, 1);
            if (g_hotkeyEnabled && g_hotkey != 0)
                RegisterHotKey(hwnd, 1, g_hotkeyModifiers, g_hotkey);
            CheckMenuItem(g_hMenu, ID_HOTKEY_TOGGLE, MF_BYCOMMAND | (g_hotkeyEnabled ? MF_CHECKED : MF_UNCHECKED));
            ShowTrayNotification(L"Hotkey Toggle", g_hotkeyEnabled ? L"Hotkey Enabled" : L"Hotkey Disabled");
        }
        else if (cmd == ID_SHOW_ALL)
        {
            for (auto& hw : g_hiddenWindows)
                ShowWindow(hw.hwnd, SW_SHOW);
            g_hiddenWindows.clear();
            RebuildHiddenMenu();
            ShowTrayNotification(L"Show All", L"All hidden windows have been restored.");
        }
        else if (cmd >= ID_HIDDEN_BASE)
        {
            int index = cmd - ID_HIDDEN_BASE;
            if (index >= 0 && index < (int)g_hiddenWindows.size())
            {
                ShowWindow(g_hiddenWindows[index].hwnd, SW_SHOW);
                g_hiddenWindows.erase(g_hiddenWindows.begin() + index);
                RebuildHiddenMenu();
                ShowTrayNotification(L"Window Restored", L"A hidden window has been restored.");
            }
        }
        break;
    }    case WM_HOTKEY:
    {
        if (g_hotkeyEnabled && g_hotkey != 0)
        {
            HWND target = GetForegroundWindow();
            if (target && target != g_hwnd)
            {
                ShowWindow(target, SW_HIDE);
                TCHAR title[256] = { 0 };
                GetWindowText(target, title, 255);
                DWORD pid = 0;
                GetWindowThreadProcessId(target, &pid);
                HiddenWindow hw;
                hw.hwnd = target;
                hw.pid = pid;
                _tcscpy_s(hw.title, title);
                g_hiddenWindows.push_back(hw);
                RebuildHiddenMenu();
            }
        }
        break;
    }
    case WM_DESTROY:
        for (auto& hw : g_hiddenWindows)
            ShowWindow(hw.hwnd, SW_SHOW);
        g_hiddenWindows.clear();
        RebuildHiddenMenu();
        Shell_NotifyIcon(NIM_DELETE, &g_nid);
        DestroyMenu(g_hMenu);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    g_hInst = hInstance;
    const TCHAR CLASS_NAME[] = _T("AppToTrayTrayClass");
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);
    g_hwnd = CreateWindow(CLASS_NAME, _T("TrayApp"), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, NULL, hInstance, NULL);
    LoadHotkeyFromRegistry();
    ShowWindow(g_hwnd, SW_HIDE);
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
