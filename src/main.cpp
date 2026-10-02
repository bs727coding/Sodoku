// Entry point: single-instance guard, window creation, message procedure and the render loop.
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>

#include <cwchar>
#include <string>

#include "app.h"

using namespace sudoku;

namespace {

App* g_app = nullptr;
bool g_trackingMouse = false;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    App* app = g_app;
    switch (msg) {
    case WM_CREATE:
        return app->init(hwnd) ? 0 : -1;
    case WM_SIZE:
        if (wp == SIZE_MINIMIZED) {
            app->onMinimized(true);
        } else {
            app->onMinimized(false);
            app->onSize(LOWORD(lp), HIWORD(lp));
            app->render();  // keep the content in step with live resizing
        }
        return 0;
    case WM_DPICHANGED: {
        const RECT* r = reinterpret_cast<const RECT*>(lp);
        app->onDpiChanged(HIWORD(wp));
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        const UINT dpi = GetDpiForWindow(hwnd);
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize = {MulDiv(560, int(dpi), 96), MulDiv(600, int(dpi), 96)};
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        app->invalidate();
        app->render();
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_ACTIVATE:
        app->onActivate(LOWORD(wp) != WA_INACTIVE);
        return 0;
    case WM_MOUSEMOVE:
        if (!g_trackingMouse) {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            g_trackingMouse = TrackMouseEvent(&tme) != FALSE;
        }
        app->onMouseMove(float(GET_X_LPARAM(lp)), float(GET_Y_LPARAM(lp)));
        return 0;
    case WM_MOUSELEAVE:
        g_trackingMouse = false;
        app->onMouseLeave();
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        app->onMouseDown(float(GET_X_LPARAM(lp)), float(GET_Y_LPARAM(lp)));
        return 0;
    case WM_LBUTTONUP:
        app->onMouseUp(float(GET_X_LPARAM(lp)), float(GET_Y_LPARAM(lp)));
        return 0;
    case WM_MOUSEWHEEL:
        app->onMouseWheel(float(GET_WHEEL_DELTA_WPARAM(wp)) / float(WHEEL_DELTA));
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        const bool shift = GetKeyState(VK_SHIFT) < 0, ctrl = GetKeyState(VK_CONTROL) < 0;
        const bool alt = (lp & (1 << 29)) != 0;
        if (app->onKeyDown(UINT(wp), shift, ctrl, alt)) return 0;
        if (wp == VK_F10) return 0;  // don't enter menu mode
        break;
    }
    case WM_SYSCHAR:
        if (wp != VK_SPACE) return 0;  // Alt+digit toggles notes; avoid the menu beep
        break;
    case WM_SYSKEYUP:
        if (wp == VK_F10) return 0;
        break;
    case WM_TIMER:
        app->onTimer(wp);
        return 0;
    case WM_SETTINGCHANGE:
    case WM_DWMCOLORIZATIONCOLORCHANGED:
    case WM_THEMECHANGED:
        app->onSystemSettingsChanged();
        break;
    case WM_APP_POOL:
        app->onPool(wp, lp);
        return 0;
    case WM_QUERYENDSESSION:
        app->onEndSession();
        return TRUE;
    case WM_ENDSESSION:
        if (wp) app->onEndSession();
        return 0;
    case WM_CLOSE:
        app->shutdown();
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default: break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int runScreenshot(int argc, wchar_t** argv) {
    // --screenshot <page> <light|dark> <W>x<H> <scale> <out.png>
    if (argc < 7) return 2;
    ScreenshotSpec spec;
    spec.page = argv[2];
    spec.dark = std::wcscmp(argv[3], L"dark") == 0;
    swscanf(argv[4], L"%dx%d", &spec.width, &spec.height);
    spec.scale = float(_wtof(argv[5]));
    spec.out = argv[6];
    if (spec.width <= 0 || spec.height <= 0 || spec.scale <= 0) return 2;
    static App app;
    return app.runScreenshot(spec) ? 0 : 1;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCmd) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (argv && argc >= 2 && std::wcscmp(argv[1], L"--screenshot") == 0) {
        const int rc = runScreenshot(argc, argv);
        LocalFree(argv);
        return rc;
    }
    if (argv) LocalFree(argv);

    // One instance at a time (saves and history are shared files).
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\Sudoku.ARM64.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND other = FindWindowW(kWindowClass, nullptr)) {
            if (IsIconic(other)) ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
        }
        return 0;
    }

    static App app;
    g_app = &app;
    app.loadSettingsEarly();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                                             GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
    wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                               LR_DEFAULTCOLOR));
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);

    // Default size: 1080 x 740 DIPs, centred on the primary monitor's work area.
    const UINT dpi = GetDpiForSystem();
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
    const RECT work = mi.rcWork;
    const int workW = work.right - work.left, workH = work.bottom - work.top;
    const int w = std::min(MulDiv(1080, int(dpi), 96), workW - MulDiv(32, int(dpi), 96));
    const int h = std::min(MulDiv(740, int(dpi), 96), workH - MulDiv(32, int(dpi), 96));
    const int x = work.left + (workW - w) / 2, y = work.top + (workH - h) / 2;

    HWND hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, kWindowClass, L"Sudoku", WS_OVERLAPPEDWINDOW, x, y, w, h,
                                nullptr, nullptr, instance, nullptr);
    if (!hwnd) {
        MessageBoxW(nullptr, L"Sudoku could not start because the graphics device could not be initialised.",
                    L"Sudoku", MB_ICONERROR);
        return 1;
    }

    bool shown = false;
    const WindowRect& saved = app.settings().window;
    if (saved.valid) {
        const RECT rc{saved.x, saved.y, saved.x + saved.w, saved.y + saved.h};
        if (MonitorFromRect(&rc, MONITOR_DEFAULTTONULL)) {
            WINDOWPLACEMENT wp{};
            wp.length = sizeof(wp);
            GetWindowPlacement(hwnd, &wp);
            wp.rcNormalPosition = rc;
            wp.showCmd = saved.maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
            SetWindowPlacement(hwnd, &wp);
            shown = true;
        }
    }
    if (!shown) ShowWindow(hwnd, showCmd);
    UpdateWindow(hwnd);

    // Event-driven loop: sleep until a message arrives; while animating, render one frame per
    // vsync (App::render waits on the swap chain's frame-latency object).
    MSG msg{};
    for (;;) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                ReleaseMutex(mutex);
                CloseHandle(mutex);
                return int(msg.wParam);
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (app.needsFrame()) {
            app.render();
            continue;
        }
        MsgWaitForMultipleObjectsEx(0, nullptr, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
}
