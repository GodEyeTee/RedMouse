#define NOMINMAX
#include <windows.h>
#include <stdio.h>
#include <atomic>
#include <thread>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <string>
#include <sstream>
#include <Commctrl.h>
#include <intrin.h> // For _mm_pause()

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "User32.lib")

// Optimized Mouse Controller - Fast & Stable
class OptimizedMouseController {
private:
    // Core states - volatile for immediate updates
    volatile bool enabled = false;
    volatile bool running = true;
    volatile double sensitivity = 1.0551;
    volatile bool useSmoothMode = false;

    // Window handles
    HWND hMainWindow = nullptr;
    HWND hStatusText = nullptr;
    HWND hSensitivityText = nullptr;
    HWND hSlider = nullptr;
    HFONT hFont = nullptr;

    // Performance timer
    LARGE_INTEGER qpcFrequency;

    // Direct input state check - using GetAsyncKeyState for immediate response
    inline bool isLeftButtonPressed() {
        // GetAsyncKeyState gives real-time state
        return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    }

    // Initialize high precision timer
    void initializeTimer() {
        QueryPerformanceFrequency(&qpcFrequency);

        // Critical performance settings
        SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);
        timeBeginPeriod(1);
    }

    // Enhanced mouse movement for game compatibility
    void moveMouseDirect(int pixels) {
        if (pixels <= 0) return;

        // Use multiple small movements for smoother control
        while (pixels > 0) {
            int moveAmount = std::min(pixels, 5); // Max 5 pixels per input

            INPUT input = { 0 };
            input.type = INPUT_MOUSE;
            input.mi.dx = 0;
            input.mi.dy = moveAmount;
            // Add VIRTUALDESK flag for better compatibility
            input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_VIRTUALDESK;
            input.mi.time = 0;
            input.mi.dwExtraInfo = 0; // Important: 0 for game compatibility

            SendInput(1, &input, sizeof(INPUT));
            pixels -= moveAmount;
        }
    }

    // High performance mouse thread with instant stop
    void mouseThread() {
        // Maximize thread priority
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

        // Pin to CPU core for consistent performance
        SetThreadAffinityMask(GetCurrentThread(), 2); // Core 1 (0-indexed)

        double accumulator = 0.0;
        LARGE_INTEGER lastTime, currentTime, frequency;
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&lastTime);

        while (running) {
            // Real-time button state check
            bool buttonPressed = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

            if (enabled && buttonPressed) {
                QueryPerformanceCounter(&currentTime);
                double deltaTime = (double)(currentTime.QuadPart - lastTime.QuadPart) / frequency.QuadPart;

                // Limit delta time to prevent huge jumps
                deltaTime = std::min(deltaTime, 0.016); // Cap at ~60fps worth

                // Calculate smooth movement
                double moveSpeed = sensitivity * 40.0;

                if (useSmoothMode) {
                    // Smooth mode with acceleration
                    accumulator += moveSpeed * deltaTime * (1.0 + accumulator * 0.01);
                }
                else {
                    // Direct mode
                    accumulator += moveSpeed * deltaTime;
                }

                int pixels = (int)accumulator;
                if (pixels > 0) {
                    moveMouseDirect(pixels);
                    accumulator -= pixels;
                }

                lastTime = currentTime;

                // Micro-sleep for CPU efficiency
                _mm_pause(); // CPU pause instruction for better threading
            }
            else {
                // IMPORTANT: Reset immediately when button is released
                accumulator = 0.0;
                QueryPerformanceCounter(&lastTime);

                // Sleep when inactive
                Sleep(1);
            }
        }
    }

    // Fast keyboard handler
    void keyboardThread() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

        while (running) {
            // Direct key state checks for instant response
            SHORT f1State = GetAsyncKeyState(VK_F1);
            if (f1State & 1) {
                enabled = !enabled;
                updateUI();
            }

            SHORT f10State = GetAsyncKeyState(VK_F10);
            if (f10State & 1) {
                useSmoothMode = !useSmoothMode;
                updateUI();
            }

            // Preset keys F2-F9
            for (int i = 0; i < 8; i++) {
                if (GetAsyncKeyState(VK_F2 + i) & 1) {
                    double presets[] = { 0.8571, 1.408, 1.15, 1.89965, 1.72225, 12.89035, 1.35955, 2.29865 };
                    sensitivity = presets[i];
                    updateUI();
                }
            }

            // Fine adjustment
            if (GetAsyncKeyState(VK_ADD) & 1) {
                sensitivity = std::min(20.0, sensitivity + 0.00005);
                updateUI();
            }
            if (GetAsyncKeyState(VK_SUBTRACT) & 1) {
                sensitivity = std::max(0.0, sensitivity - 0.00005);
                updateUI();
            }

            // Exit
            if (GetAsyncKeyState(VK_ESCAPE) & 1) {
                running = false;
                PostMessage(hMainWindow, WM_CLOSE, 0, 0);
            }

            Sleep(10); // 100Hz polling rate
        }
    }

    void updateUI() {
        if (!hMainWindow) return;

        // Update status text
        SetWindowTextW(hStatusText, enabled ? L"● ACTIVE" : L"○ INACTIVE");

        // Update sensitivity text
        wchar_t buffer[100];
        swprintf_s(buffer, L"Sensitivity: %.7f | Mode: %s",
            sensitivity, useSmoothMode ? L"Smooth" : L"Direct");
        SetWindowTextW(hSensitivityText, buffer);

        // Update slider position
        if (hSlider) {
            int pos = (int)(sensitivity * 100);
            SendMessage(hSlider, TBM_SETPOS, TRUE, pos);
        }

        // Force redraw
        InvalidateRect(hMainWindow, NULL, FALSE);
    }

    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        OptimizedMouseController* pThis = nullptr;

        if (msg == WM_NCCREATE) {
            CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
            pThis = (OptimizedMouseController*)pCreate->lpCreateParams;
            SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pThis);
        }
        else {
            pThis = (OptimizedMouseController*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        }

        if (pThis) {
            return pThis->HandleMessage(hWnd, msg, wParam, lParam);
        }

        return DefWindowProc(hWnd, msg, wParam, lParam);
    }

    LRESULT HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_CREATE:
            createUI(hWnd);
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
            case 1001: // Toggle button
                enabled = !enabled;
                updateUI();
                break;
            case 1002: // Mode button  
                useSmoothMode = !useSmoothMode;
                updateUI();
                break;
            case 1003: // Exit button
                PostMessage(hWnd, WM_CLOSE, 0, 0);
                break;
            case 1010: // Minus button
                sensitivity = std::max(0.0, sensitivity - 0.00005);
                updateUI();
                break;
            case 1011: // Plus button
                sensitivity = std::min(20.0, sensitivity + 0.00005);
                updateUI();
                break;
            default:
                // Handle preset buttons F2-F9
                if (LOWORD(wParam) >= 2000 && LOWORD(wParam) <= 2007) {
                    double presets[] = { 0.8571, 1.408, 1.15, 1.89965, 1.72225, 12.89035, 1.35955, 2.29865 };
                    int idx = LOWORD(wParam) - 2000;
                    sensitivity = presets[idx];
                    updateUI();
                }
                break;
            }
            return 0;

        case WM_HSCROLL:
            if ((HWND)lParam == hSlider) {
                int pos = SendMessage(hSlider, TBM_GETPOS, 0, 0);
                sensitivity = pos / 100.0;
                updateUI();
            }
            return 0;

        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            HWND hwndStatic = (HWND)lParam;

            if (hwndStatic == hStatusText) {
                SetTextColor(hdc, enabled ? RGB(0, 200, 0) : RGB(200, 0, 0));
                SetBkMode(hdc, TRANSPARENT);
                return (LRESULT)GetStockObject(NULL_BRUSH);
            }
            break;
        }

        case WM_DESTROY:
            if (hFont) DeleteObject(hFont);
            running = false;
            PostQuitMessage(0);
            return 0;
        }

        return DefWindowProc(hWnd, msg, wParam, lParam);
    }

    void createUI(HWND hWnd) {
        hMainWindow = hWnd;

        // Create font
        hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        // Title
        HWND hTitle = CreateWindowW(L"STATIC", L"RedMouse V3 - Optimized",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            10, 10, 280, 25, hWnd, NULL, NULL, NULL);
        SendMessage(hTitle, WM_SETFONT, (WPARAM)hFont, TRUE);

        // Status text
        hStatusText = CreateWindowW(L"STATIC", L"○ INACTIVE",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            10, 45, 280, 30, hWnd, NULL, NULL, NULL);
        SendMessage(hStatusText, WM_SETFONT, (WPARAM)hFont, TRUE);

        // Sensitivity text
        hSensitivityText = CreateWindowW(L"STATIC", L"Sensitivity: 1.0551000",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            10, 85, 280, 25, hWnd, NULL, NULL, NULL);
        SendMessage(hSensitivityText, WM_SETFONT, (WPARAM)hFont, TRUE);

        // Sensitivity slider
        hSlider = CreateWindowW(TRACKBAR_CLASSW, L"",
            WS_VISIBLE | WS_CHILD | TBS_HORZ,
            20, 120, 260, 30, hWnd, NULL, NULL, NULL);
        SendMessage(hSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 2000));
        SendMessage(hSlider, TBM_SETPOS, TRUE, (LPARAM)(sensitivity * 100));

        // Preset buttons row 1
        int btnWidth = 65;
        int btnHeight = 30;
        for (int i = 0; i < 4; i++) {
            wchar_t btnText[10];
            swprintf_s(btnText, L"F%d", i + 2);
            CreateWindowW(L"BUTTON", btnText,
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                20 + i * (btnWidth + 5), 165, btnWidth, btnHeight,
                hWnd, (HMENU)(2000 + i), NULL, NULL);
        }

        // Preset buttons row 2
        for (int i = 0; i < 4; i++) {
            wchar_t btnText[10];
            swprintf_s(btnText, L"F%d", i + 6);
            CreateWindowW(L"BUTTON", btnText,
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                20 + i * (btnWidth + 5), 200, btnWidth, btnHeight,
                hWnd, (HMENU)(2004 + i), NULL, NULL);
        }

        // Control buttons
        CreateWindowW(L"BUTTON", L"Toggle (F1)",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            20, 245, 125, 35, hWnd, (HMENU)1001, NULL, NULL);

        CreateWindowW(L"BUTTON", L"Mode (F10)",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            155, 245, 125, 35, hWnd, (HMENU)1002, NULL, NULL);

        // Fine adjustment
        CreateWindowW(L"BUTTON", L"-",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            20, 290, 40, 30, hWnd, (HMENU)1010, NULL, NULL);

        CreateWindowW(L"BUTTON", L"+",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            70, 290, 40, 30, hWnd, (HMENU)1011, NULL, NULL);

        // Exit button
        CreateWindowW(L"BUTTON", L"Exit (ESC)",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            155, 290, 125, 30, hWnd, (HMENU)1003, NULL, NULL);

        // Credits
        HWND hCredit = CreateWindowW(L"STATIC", L"Made with ❤️ by GodEyeTee",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            10, 330, 280, 20, hWnd, NULL, NULL, NULL);

        // Apply font to all controls
        EnumChildWindows(hWnd, [](HWND hwnd, LPARAM lParam) -> BOOL {
            SendMessage(hwnd, WM_SETFONT, (WPARAM)lParam, TRUE);
            return TRUE;
            }, (LPARAM)hFont);

        updateUI();
    }

public:
    OptimizedMouseController() {
        initializeTimer();
    }

    ~OptimizedMouseController() {
        timeEndPeriod(1);
    }

    bool registerWindowClass(HINSTANCE hInstance) {
        WNDCLASSEX wcex = {};
        wcex.cbSize = sizeof(WNDCLASSEX);
        wcex.style = CS_HREDRAW | CS_VREDRAW;
        wcex.lpfnWndProc = WindowProc;
        wcex.hInstance = hInstance;
        wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
        wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wcex.lpszClassName = L"RedMouseOptimized";

        return RegisterClassEx(&wcex) != 0;
    }

    bool createWindow(HINSTANCE hInstance) {
        // Create window with standard style
        hMainWindow = CreateWindowEx(
            0, L"RedMouseOptimized", L"RedMouse V3 - Fast & Stable",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
            CW_USEDEFAULT, CW_USEDEFAULT, 315, 395,
            NULL, NULL, hInstance, this);

        if (!hMainWindow) return false;

        // Center window
        RECT rc;
        GetWindowRect(hMainWindow, &rc);
        int x = (GetSystemMetrics(SM_CXSCREEN) - (rc.right - rc.left)) / 2;
        int y = (GetSystemMetrics(SM_CYSCREEN) - (rc.bottom - rc.top)) / 2;
        SetWindowPos(hMainWindow, HWND_TOP, x, y, 0, 0, SWP_NOSIZE);

        ShowWindow(hMainWindow, SW_SHOW);
        UpdateWindow(hMainWindow);

        return true;
    }

    void run(HINSTANCE hInstance) {
        if (!registerWindowClass(hInstance) || !createWindow(hInstance)) {
            MessageBox(NULL, L"Failed to initialize", L"Error", MB_OK | MB_ICONERROR);
            return;
        }

        // Start worker threads
        std::thread mouseWorker(&OptimizedMouseController::mouseThread, this);
        std::thread keyboardWorker(&OptimizedMouseController::keyboardThread, this);

        // Message loop
        MSG msg = {};
        while (GetMessage(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        // Cleanup
        running = false;
        mouseWorker.join();
        keyboardWorker.join();
    }
};

// Entry point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Single instance check
    HANDLE hMutex = CreateMutex(NULL, TRUE, L"RedMouseV3_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBox(NULL, L"Application is already running", L"RedMouse", MB_OK | MB_ICONINFORMATION);
        CloseHandle(hMutex);
        return 0;
    }

    OptimizedMouseController controller;
    controller.run(hInstance);

    CloseHandle(hMutex);
    return 0;
}

// Made with ❤️ by GodEyeTee
