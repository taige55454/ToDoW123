#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include "gfx.h"
#include "app.h"
#include "resource.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

static const wchar_t* kClass = L"EggDoneTodoClass";
static const UINT_PTR kAnimTimer = 1;
static const UINT_PTR kRemindTimer = 2;
static const UINT kTrayMsg = WM_APP + 1;
static const UINT kTrayId = 1;
static LARGE_INTEGER g_qpfFreq = {};
static LARGE_INTEGER g_qpfLast = {};

static void AddTrayIcon(HWND hwnd) {
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = kTrayId;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = kTrayMsg;
    nid.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP));
    if (!nid.hIcon) nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"EggDone - 桌面待办");
    Shell_NotifyIconW(NIM_ADD, &nid);
}

static void RemoveTrayIcon(HWND hwnd) {
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = kTrayId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g_gfx.init(hwnd);
            g_app = new App();
            g_app->create(hwnd);
            AddTrayIcon(hwnd);
            QueryPerformanceFrequency(&g_qpfFreq);
            QueryPerformanceCounter(&g_qpfLast);
            // 16ms (~60fps) 轻量级状态轮询定时器 + 30秒提醒定时器，告别 1ms CPU 轰炸
            SetTimer(hwnd, kAnimTimer, 16, nullptr);
            SetTimer(hwnd, kRemindTimer, 30000, nullptr);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
            if (g_app) g_app->render();
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_TIMER: {
            if (wp == kRemindTimer) {
                if (g_app) g_app->checkReminders();
                return 0;
            }
            if (wp == kAnimTimer && g_app) {
                LARGE_INTEGER now; QueryPerformanceCounter(&now);
                float dt = (float)((double)(now.QuadPart - g_qpfLast.QuadPart) / (double)g_qpfFreq.QuadPart);
                g_qpfLast = now;
                if (dt > 0.1f) dt = 0.1f;
                g_app->tick(dt);
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (g_app) g_app->onMouseMove(g_app->toDip(GET_X_LPARAM(lp)), g_app->toDip(GET_Y_LPARAM(lp)));
            return 0;
        }
        case WM_LBUTTONDOWN: {
            if (g_app) g_app->onLButtonDown(g_app->toDip(GET_X_LPARAM(lp)), g_app->toDip(GET_Y_LPARAM(lp)));
            return 0;
        }
        case WM_RBUTTONUP: {
            if (g_app) g_app->onRButtonUp(g_app->toDip(GET_X_LPARAM(lp)), g_app->toDip(GET_Y_LPARAM(lp)));
            return 0;
        }
        case WM_MOUSEWHEEL: {
            if (g_app) g_app->onWheel(GET_WHEEL_DELTA_WPARAM(wp));
            return 0;
        }
        case kTrayMsg: {
            if (LOWORD(lp) == WM_RBUTTONUP) {
                SetForegroundWindow(hwnd);
                HMENU menu = CreatePopupMenu();
                AppendMenuW(menu, MF_STRING, 1001, L"🖥️ 显示主面板");
                UINT autoFlags = MF_STRING | (is_auto_start() ? MF_CHECKED : MF_UNCHECKED);
                AppendMenuW(menu, autoFlags, 1002, L"⚡ 开机自动启动");
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(menu, MF_STRING, 1003, L"✖ 彻底退出");
                POINT pt; GetCursorPos(&pt);
                int cmd = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, hwnd, nullptr);
                DestroyMenu(menu);
                if (cmd == 1001 && g_app) g_app->expandWindow();
                else if (cmd == 1002 && g_app) { g_app->expandWindow(); g_app->toggleAutoStartFromTray(); }
                else if (cmd == 1003) DestroyWindow(hwnd);
            } else if (LOWORD(lp) == WM_LBUTTONUP) {
                if (g_app) g_app->expandWindow();
            }
            return 0;
        }
        case WM_DESTROY: {
            KillTimer(hwnd, kAnimTimer);
            KillTimer(hwnd, kRemindTimer);
            RemoveTrayIcon(hwnd);
            if (g_app) { g_app->destroy(); delete g_app; g_app = nullptr; }
            g_gfx.cleanup();
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    SetProcessDPIAware();

    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"EggDone_Python_Instance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"程序已在运行！请查看屏幕右侧或系统托盘。", L"提示", MB_OK | MB_ICONWARNING);
        return 0;
    }

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClass;
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APP));
    RegisterClassExW(&wc);

    HDC hdc = GetDC(nullptr);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(nullptr, hdc);
    g_gfx.dpiScale = (dpi <= 0 ? 96 : dpi) / 96.0f;

    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int winW = (int)(AppC::PANEL_W * g_gfx.dpiScale + 0.5f);
    int winH = (int)(AppC::PANEL_H * g_gfx.dpiScale + 0.5f);
    int winX = wa.right - winW - 20;
    int winY = wa.top + 60;

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        kClass, L"EggDoneTodo",
        WS_POPUP | WS_CLIPCHILDREN | WS_VISIBLE,
        winX, winY, winW, winH,
        nullptr, nullptr, hInst, nullptr);

    if (!hwnd) return 0;

    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    ReleaseMutex(hMutex);
    CloseHandle(hMutex);
    return (int)msg.wParam;
}
