#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <mmsystem.h>
#include <shellapi.h>
#include "gfx.h"
#include "app.h"
#include "resource.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "shell32.lib")

static const wchar_t* kClass = L"EggDoneTodoClass";
static UINT_PTR kTimer = 1;
static const UINT kTrayMsg = WM_APP + 1;
static const UINT kTrayId = 1;
static MMRESULT g_mmTimer = 0;
static HWND g_timerHwnd = nullptr;

static void AddTrayIcon(HWND hwnd) {
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = kTrayId;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = kTrayMsg;
    nid.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP));
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

static void CALLBACK mmTimerCb(UINT, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR) {
    if (g_timerHwnd) PostMessageW(g_timerHwnd, WM_TIMER, kTimer, 0);
}

static LARGE_INTEGER g_qpfFreq = {};
static LARGE_INTEGER g_qpfLast = {};

static double nowSec() {
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)g_qpfFreq.QuadPart;
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
            timeBeginPeriod(1);
            g_timerHwnd = hwnd;
            g_mmTimer = timeSetEvent(1, 0, mmTimerCb, 0, TIME_PERIODIC);
            SetTimer(hwnd, 2, 30000, nullptr); // 30秒提醒一次定时检查
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
            if (g_app) g_app->render();
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_TIMER: {
            if (wp == 2) {
                if (g_app) g_app->checkReminders();
                return 0;
            }
            double now = nowSec();
            float dt = (float)(now - ((double)g_qpfLast.QuadPart / (double)g_qpfFreq.QuadPart));
            g_qpfLast.QuadPart = (LONGLONG)(now * (double)g_qpfFreq.QuadPart);
            if (dt > 0.1f) dt = 0.1f;
            if (g_app) g_app->tick(dt);
            return 0;
        }
        case WM_SIZE: {
            g_gfx.resize();
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
        case WM_CHAR: {
            if (g_app) g_app->onChar((wchar_t)wp);
            return 0;
        }
        case WM_KEYDOWN: {
            if (g_app) g_app->onKeyDown((int)wp);
            return 0;
        }
        case kTrayMsg: {
            if (LOWORD(lp) == WM_RBUTTONUP) {
                SetForegroundWindow(hwnd);
                HMENU menu = CreatePopupMenu();
                AppendMenuW(menu, MF_STRING, 1001, L"🖥️ 显示主面板");
                AppendMenuW(menu, MF_STRING, 1002, L"⚡ 开机自启切换");
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(menu, MF_STRING, 1003, L"✖ 彻底退出");
                POINT pt; GetCursorPos(&pt);
                int cmd = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, hwnd, nullptr);
                DestroyMenu(menu);
                if (cmd == 1001 && g_app) g_app->expandWindow();
                else if (cmd == 1002) set_auto_start(!is_auto_start());
                else if (cmd == 1003) DestroyWindow(hwnd);
            } else if (LOWORD(lp) == WM_LBUTTONUP) {
                if (g_app) g_app->expandWindow();
            }
            return 0;
        }
        case WM_DESTROY: {
            RemoveTrayIcon(hwnd);
            if (g_app) { g_app->destroy(); delete g_app; g_app = nullptr; }
            if (g_mmTimer) { timeKillEvent(g_mmTimer); g_mmTimer = 0; }
            timeEndPeriod(1);
            g_gfx.cleanup();
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    SetProcessDPIAware();

    // 互斥单例保护
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"EggDone_Win32_Instance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"程序已在运行！请查看屏幕右侧或系统托盘。", L"提示", MB_OK | MB_ICONINFORMATION);
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
    int initialSize = (int)(AppC::BALL_SIZE * g_gfx.dpiScale + 0.5f);
    int initX = wa.right - initialSize - 20;
    int initY = wa.top + 60;

    // 创建顶层浮动透明窗口：无边框 + 工具栏隐藏 + 置顶
    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        kClass, L"EggDone",
        WS_POPUP | WS_VISIBLE,
        initX, initY, initialSize, initialSize,
        nullptr, nullptr, hInst, nullptr);

    if (!hwnd) return 0;

    // 设置初始不透明度
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
