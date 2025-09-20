// RedMouseV3_StablePlus.cpp — compile-clean (VS/MSVC), modern UI, no features removed

#define NOMINMAX
#include <windows.h>
#include <CommCtrl.h>
#include <dwmapi.h>
#include <shellscalingapi.h>
#include <atomic>
#include <thread>
#include <chrono>
#include <algorithm>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "Shcore.lib")
#pragma comment(lib, "winmm.lib")

#pragma comment(linker, \
"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// -------- Theme --------
static COLORREF kBg   = RGB(28,28,28);
static COLORREF kText = RGB(232,232,232);
static COLORREF kGood = RGB(40,200,40);
static COLORREF kBad  = RGB(220,60,60);
static HBRUSH   kBgBr = CreateSolidBrush(kBg);

static BOOL EnableDarkTitleBar(HWND hwnd, BOOL enable) {
    const DWORD DWMWA_USE_IMMERSIVE_DARK_MODE = 20; // Win10 1809+/Win11
    return DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE,
                                 &enable, sizeof(enable)) == S_OK;
}

// -------- App --------
class StableMouseController {
private:
    std::atomic<bool>   enabled{false};
    std::atomic<bool>   running{true};
    std::atomic<double> sensitivity{1.0551};

    std::thread mouseThread, keyboardThread;
    LARGE_INTEGER qpcFreq{};

    // UI
    HWND  hMain=nullptr, hStatus=nullptr, hSensText=nullptr, hSlider=nullptr;
    HFONT hFont=nullptr;

    void initTimer() {
        QueryPerformanceFrequency(&qpcFreq);
        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
        timeBeginPeriod(1); // pair with timeEndPeriod(1)
    }

    static void sendMouseMoveY(int dy) {
        if (dy==0) return;
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = MOUSEEVENTF_MOVE;
        in.mi.dx = 0;
        in.mi.dy = dy;
        SendInput(1, &in, sizeof(INPUT));
    }

    void mouseProc() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

        LARGE_INTEGER last, now;
        QueryPerformanceCounter(&last);

        double acc = 0.0;
        constexpr double MAX_ACC = 10.0;

        while (running.load()) {
            const bool lDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

            if (enabled.load() && lDown) {
                QueryPerformanceCounter(&now);
                double dt = double(now.QuadPart - last.QuadPart) / qpcFreq.QuadPart;
                last = now;
                if (dt > 0.016) dt = 0.016; // frame clamp ~60Hz

                const double pix = sensitivity.load() * 40.0 * dt;

                acc += pix;
                if (acc > MAX_ACC) acc = MAX_ACC;

                if (acc >= 1.0) {
                    int py = (int)acc;
                    sendMouseMoveY(py);
                    acc -= py;
                }
            } else {
                acc = 0.0;
                QueryPerformanceCounter(&last);
            }
            Sleep(1);
        }
    }

    void keyboardProc() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
        static const double P[8] = {0.8571,1.408,1.15,1.89965,1.72225,12.89035,1.35955,2.29865};

        while (running.load()) {
            if (GetAsyncKeyState(VK_F1) & 1) { enabled.store(!enabled.load()); updateUI(); }
            for (int i=0;i<8;++i)
                if (GetAsyncKeyState(VK_F2+i) & 1) { sensitivity.store(P[i]); updateUI(); }

            if (GetAsyncKeyState(VK_ADD) & 1)
                sensitivity.store(std::min(20.0, sensitivity.load()+0.00005)), updateUI();
            if (GetAsyncKeyState(VK_SUBTRACT) & 1)
                sensitivity.store(std::max(0.0,  sensitivity.load()-0.00005)), updateUI();

            if (GetAsyncKeyState(VK_ESCAPE) & 1) { shutdown(); }
            Sleep(10);
        }
    }

    void updateUI() {
        if (!hMain) return;
        SetWindowTextW(hStatus, enabled.load()? L"● ACTIVE" : L"○ INACTIVE");
        wchar_t buf[128];
        swprintf_s(buf, L"Sensitivity: %.7f", sensitivity.load());
        SetWindowTextW(hSensText, buf);
        if (hSlider) SendMessageW(hSlider, TBM_SETPOS, (WPARAM)TRUE, (LPARAM)(sensitivity.load()*100));
        InvalidateRect(hMain, nullptr, FALSE);
    }

    // ------- helpers for controls (fixed types) -------
    HWND mkStatic(LPCWSTR txt, int x, int y, int w, int h) {
        HWND hCtrl = CreateWindowExW(
            0, L"STATIC", txt, WS_VISIBLE|WS_CHILD|SS_CENTER,
            x, y, w, h,
            hMain, (HMENU)0, GetModuleHandleW(nullptr), nullptr
        );
        if (hCtrl) {
            SendMessageW(hCtrl, WM_SETFONT, (WPARAM)hFont, (LPARAM)TRUE); // wParam=HFONT, lParam=BOOL
        }
        return hCtrl; // HWND OK
    }

    HWND mkButton(int id, LPCWSTR txt, int x, int y, int w, int h) {
        HWND hCtrl = CreateWindowExW(
            0, L"BUTTON", txt, WS_VISIBLE|WS_CHILD|BS_PUSHBUTTON,
            x, y, w, h,
            hMain, (HMENU)id, GetModuleHandleW(nullptr), nullptr
        );
        if (hCtrl) {
            SendMessageW(hCtrl, WM_SETFONT, (WPARAM)hFont, (LPARAM)TRUE);
        }
        return hCtrl; // HWND OK
    }

    void buildUI(HWND hWnd) {
        hMain = hWnd;

        hFont = CreateFontW(18,0,0,0, FW_MEDIUM, FALSE,FALSE,FALSE,
                            DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH|FF_DONTCARE, L"Segoe UI");

        mkStatic(L"RedMouse V3 — Stable+ (Modern UI)", 10, 10, 320-20, 26);

        hStatus   = mkStatic(L"○ INACTIVE",             10, 40, 320-20, 28);
        hSensText = mkStatic(L"Sensitivity: 1.0551000", 10, 72, 320-20, 24);

        hSlider = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_VISIBLE|WS_CHILD|TBS_HORZ,
                                  20, 100, 320-40, 30, hWnd, (HMENU)100,
                                  GetModuleHandleW(nullptr), nullptr);
        SendMessageW(hSlider, TBM_SETRANGE, (WPARAM)TRUE, MAKELPARAM(0,2000));
        SendMessageW(hSlider, TBM_SETPOS,   (WPARAM)TRUE, (LPARAM)(sensitivity.load()*100));
        SendMessageW(hSlider, WM_SETFONT, (WPARAM)hFont, (LPARAM)TRUE);

        for (int i=0;i<4;++i){ wchar_t t[8]; swprintf_s(t,L"F%d", i+2); mkButton(2000+i, t, 20+i*72, 140, 64, 32); }
        for (int i=0;i<4;++i){ wchar_t t[8]; swprintf_s(t,L"F%d", i+6); mkButton(2004+i, t, 20+i*72, 176, 64, 32); }

        mkButton(1001, L"Toggle (F1)", 20, 216, 132, 36);
        mkButton(1003, L"Exit (ESC)",  168, 216, 132, 36);
        mkButton(1010, L"−",           20, 260, 48,  32);
        mkButton(1011, L"+",           76, 260, 48,  32);
        mkStatic(L"Hold Left Click + F1 to activate", 132, 262, 168, 28);
        mkStatic(L"Made with ❤️ by GodEyeTee",         10, 302, 320-20, 22);

        updateUI();
    }

    void shutdown() {
        enabled.store(false);
        running.store(false);
        if (hMain) PostMessageW(hMain, WM_CLOSE, 0, 0);
    }

    // -------- WndProc --------
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<StableMouseController*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));

        switch (msg) {
        case WM_NCCREATE: {
            auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            return (LRESULT)TRUE; // must be TRUE to continue creation
        }
        case WM_CREATE: {
            EnableDarkTitleBar(hWnd, TRUE);
            self->buildUI(hWnd);
            return (LRESULT)0;
        }
        case WM_ERASEBKGND: {
            HDC dc = (HDC)wParam;
            RECT rc; GetClientRect(hWnd, &rc);
            FillRect(dc, &rc, kBgBr);
            return (LRESULT)1; // handled
        }
        case WM_HSCROLL:
            if ((HWND)lParam == self->hSlider) {
                int pos = (int)SendMessageW(self->hSlider, TBM_GETPOS, 0, 0);
                self->sensitivity.store(pos / 100.0);
                self->updateUI();
            }
            return (LRESULT)0;

        case WM_COMMAND: {
            WORD id = LOWORD(wParam);
            if (id==1001){ self->enabled.store(!self->enabled.load()); self->updateUI(); }
            else if (id==1003){ self->shutdown(); }
            else if (id==1010){ self->sensitivity.store(std::max(0.0,  self->sensitivity.load()-0.00005)); self->updateUI(); }
            else if (id==1011){ self->sensitivity.store(std::min(20.0, self->sensitivity.load()+0.00005)); self->updateUI(); }
            else if (id>=2000 && id<=2007){
                static const double P[8]={0.8571,1.408,1.15,1.89965,1.72225,12.89035,1.35955,2.29865};
                self->sensitivity.store(P[id-2000]);
                self->updateUI();
            }
            return (LRESULT)0;
        }

        case WM_CTLCOLORDLG:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORBTN: {
            HDC hdc = (HDC)wParam;
            SetBkColor(hdc, kBg);
            SetTextColor(hdc, (HWND)lParam == self->hStatus
                               ? (self->enabled.load()? kGood : kBad)
                               : kText);
            return (LRESULT)(INT_PTR)kBgBr; // return HBRUSH
        }

        case WM_DPICHANGED:
            InvalidateRect(hWnd, nullptr, TRUE);
            return (LRESULT)0;

        case WM_CLOSE:
            DestroyWindow(hWnd);
            return (LRESULT)0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return (LRESULT)0;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

public:
    StableMouseController(){ initTimer(); }
    ~StableMouseController(){
        shutdown();
        if (mouseThread.joinable())    mouseThread.join();
        if (keyboardThread.joinable()) keyboardThread.join();
        timeEndPeriod(1);
        if (hFont) DeleteObject(hFont);
        if (kBgBr){ DeleteObject(kBgBr); kBgBr=nullptr; }
    }

    bool registerClass(HINSTANCE hInst) {
        INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES };
        InitCommonControlsEx(&icc);

        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = hInst;
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
        wc.lpszClassName = L"RedMouseStablePlus";
        return RegisterClassExW(&wc) != 0;
    }

    bool createMain(HINSTANCE hInst) {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        HWND w = CreateWindowExW(WS_EX_APPWINDOW, L"RedMouseStablePlus",
                                 L"RedMouse V3 — Stable+",
                                 WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
                                 CW_USEDEFAULT, CW_USEDEFAULT, 320, 360,
                                 nullptr, nullptr, hInst, this);
        if (!w) return false;

        RECT r; GetWindowRect(w, &r);
        int W = r.right-r.left, H = r.bottom-r.top;
        int X = (GetSystemMetrics(SM_CXSCREEN)-W)/2;
        int Y = (GetSystemMetrics(SM_CYSCREEN)-H)/2;
        SetWindowPos(w, nullptr, X, Y, 0, 0, SWP_NOSIZE|SWP_NOZORDER);

        ShowWindow(w, SW_SHOW);
        UpdateWindow(w);
        return true;
    }

    void run(HINSTANCE hInst) {
        if (!registerClass(hInst) || !createMain(hInst)) {
            MessageBoxW(nullptr, L"Initialization failed.", L"Error", MB_ICONERROR|MB_OK);
            return;
        }
        mouseThread    = std::thread(&StableMouseController::mouseProc,    this);
        keyboardThread = std::thread(&StableMouseController::keyboardProc, this);

        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
};

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    HANDLE mx = CreateMutexW(nullptr, TRUE, L"RedMouseV3_StablePlus_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND w = FindWindowW(L"RedMouseStablePlus", nullptr);
        if (w) SetForegroundWindow(w);
        return 0;
    }
    {
        StableMouseController app;
        app.run(hInst);
    }
    if (mx){ ReleaseMutex(mx); CloseHandle(mx); }
    return 0;
}
