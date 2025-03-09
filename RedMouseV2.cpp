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
#include "Resource.h"

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "Comctl32.lib")

// Window class and control IDs
const wchar_t* WINDOW_CLASS_NAME = L"MouseControllerClass";
const wchar_t* WINDOW_TITLE = L"Mouse Movement Controller";

#define ID_STATUS_TEXT       1001
#define ID_SENSITIVITY_TEXT  1002
#define ID_CURVE_TEXT        1003
#define ID_TOGGLE_BUTTON     1004
#define ID_CURVE_BUTTON      1005
#define ID_SENSITIVITY_MINUS 1006
#define ID_SENSITIVITY_PLUS  1007
#define ID_EXIT_BUTTON       1008
#define ID_SENSITIVITY_SLIDER 1009
#define ID_PRESET_COMBO      1010

// Colors
#define COLOR_ENABLED        RGB(0, 180, 0)
#define COLOR_DISABLED       RGB(220, 0, 0)
#define COLOR_BACKGROUND     RGB(240, 240, 240)
#define COLOR_BUTTON         RGB(230, 230, 230)
#define COLOR_BUTTON_HOVER   RGB(200, 200, 200)
#define COLOR_TEXT           RGB(30, 30, 30)
#define COLOR_TITLE          RGB(0, 100, 160)

class MouseController {
private:
    std::atomic<bool> enabled{ false };
    std::atomic<bool> running{ true };
    std::atomic<double> sensitivity{ 1.0551 }; // Default sensitivity
    std::atomic<bool> useCurvePattern{ false };
    HWND hMainWindow;
    HWND hStatusText;
    HWND hSensitivityText;
    HWND hCurveText;
    HWND hToggleButton;
    HWND hCurveButton;
    HWND hSlider;
    HWND hPresetCombo;
    HFONT hFontNormal;
    HFONT hFontBold;
    HBRUSH hBrushEnabled;
    HBRUSH hBrushDisabled;
    HBRUSH hBackgroundBrush;

    // ฟังก์ชัน interpolation แบบ quadratic Bezier
    POINT bezierInterpolation(const POINT& start, const POINT& control, const POINT& end, double t) {
        double u = 1.0 - t;
        double tt = t * t;
        double uu = u * u;
        POINT result;
        result.x = static_cast<LONG>(uu * start.x + 2 * u * t * control.x + tt * end.x);
        result.y = static_cast<LONG>(uu * start.y + 2 * u * t * control.y + tt * end.y);
        return result;
    }

    void updateStatusText() {
        SetWindowTextW(hStatusText, enabled ? L"Status: ENABLED" : L"Status: DISABLED");
        InvalidateRect(hStatusText, NULL, TRUE);
    }

    void updateSensitivityText() {
        std::wstringstream ss;
        ss.precision(7);
        ss << L"Sensitivity: " << std::fixed << sensitivity.load();
        SetWindowTextW(hSensitivityText, ss.str().c_str());

        // อัพเดท slider ถ้ามีการเปลี่ยนแปลงจากภายนอก
        double sliderPos = sensitivity.load() * 100;
        if (sliderPos > 2000) sliderPos = 2000; // Max 20.0
        SendMessage(hSlider, TBM_SETPOS, TRUE, (LPARAM)sliderPos);
    }

    void updateCurveText() {
        SetWindowTextW(hCurveText, useCurvePattern ? L"Curve Pattern: ON" : L"Curve Pattern: OFF");
        InvalidateRect(hCurveText, NULL, TRUE);
    }

    // ตั้งค่าความไว
    void setSensitivity(double value) {
        sensitivity = value;
        updateSensitivityText();
    }

    void mouseThread() {
        using namespace std::chrono;
        // ตั้ง priority สูงสำหรับ thread นี้
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

        auto nextTick = steady_clock::now();
        const double dt = 0.01; // ระยะเวลาของ tick = 10ms
        double pixelAccumulator = 0.0;
        const double maxSpeed = 40.0; // เมื่อ sensitivity = 1.0 -> 40px/s

        while (running) {
            if (enabled && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                double currentSensitivity = sensitivity.load();
                double speed = currentSensitivity * maxSpeed;
                pixelAccumulator += speed * dt;
                int movePixels = static_cast<int>(pixelAccumulator);

                if (movePixels > 0) {
                    if (useCurvePattern.load()) {
                        // โหมด Curve Pattern: ใช้ interpolation แบบ Bezier
                        POINT startPos;
                        GetCursorPos(&startPos);
                        // กำหนดตำแหน่งปลายเป้าหมาย: เคลื่อนที่ในแนวตั้ง
                        POINT targetPos = { startPos.x, startPos.y + movePixels };
                        // กำหนด control point (ปรับแต่งได้ตามความต้องการ)
                        POINT controlPos = { startPos.x, startPos.y + movePixels / 2 - 5 };
                        const int steps = 10;
                        for (int i = 1; i <= steps; ++i) {
                            double t = static_cast<double>(i) / steps;
                            POINT interpolated = bezierInterpolation(startPos, controlPos, targetPos, t);
                            SetCursorPos(interpolated.x, interpolated.y);
                            std::this_thread::sleep_for(milliseconds(1));
                        }
                    }
                    else {
                        // โหมดปกติ: ใช้ SendInput() เพื่อเลื่อนเคอร์เซอร์แบบ relative
                        INPUT input = { 0 };
                        input.type = INPUT_MOUSE;
                        input.mi.dx = 0; // ไม่มีการเคลื่อนที่ในแนวนอน
                        input.mi.dy = movePixels; // เคลื่อนที่ในแนวตั้ง
                        input.mi.dwFlags = MOUSEEVENTF_MOVE;
                        SendInput(1, &input, sizeof(INPUT));
                    }
                    pixelAccumulator -= movePixels;
                }
            }
            else {
                // รีเซ็ต accumulator เมื่อไม่มีการกดปุ่มซ้าย
                pixelAccumulator = 0.0;
            }
            nextTick += milliseconds(10);
            std::this_thread::sleep_until(nextTick);
        }
    }

    void keyboardThread() {
        while (running) {
            // Toggle เปิด/ปิดด้วย F1
            if (GetAsyncKeyState(VK_F1) & 1) {
                enabled = !enabled;
                updateStatusText();
                // อัพเดตสีของปุ่ม Toggle
                SetWindowTextW(hToggleButton, enabled ? L"Disable (F1)" : L"Enable (F1)");
                InvalidateRect(hToggleButton, NULL, TRUE);
            }
            // กำหนด sensitivity (0-1) ผ่าน F2-F9
            if (GetAsyncKeyState(VK_F2) & 1) {
                setSensitivity(0.8571);
                SendMessage(hPresetCombo, CB_SETCURSEL, 0, 0);
            }
            if (GetAsyncKeyState(VK_F3) & 1) {
                setSensitivity(1.408);
                SendMessage(hPresetCombo, CB_SETCURSEL, 1, 0);
            }
            if (GetAsyncKeyState(VK_F4) & 1) {
                setSensitivity(1.15);
                SendMessage(hPresetCombo, CB_SETCURSEL, 2, 0);
            }
            if (GetAsyncKeyState(VK_F5) & 1) {
                setSensitivity(1.89965);
                SendMessage(hPresetCombo, CB_SETCURSEL, 3, 0);
            }
            if (GetAsyncKeyState(VK_F6) & 1) {
                setSensitivity(1.72225);
                SendMessage(hPresetCombo, CB_SETCURSEL, 4, 0);
            }
            if (GetAsyncKeyState(VK_F7) & 1) {
                setSensitivity(12.89035);
                SendMessage(hPresetCombo, CB_SETCURSEL, 5, 0);
            }
            if (GetAsyncKeyState(VK_F8) & 1) {
                setSensitivity(1.35955);
                SendMessage(hPresetCombo, CB_SETCURSEL, 6, 0);
            }
            if (GetAsyncKeyState(VK_F9) & 1) {
                setSensitivity(2.29865);
                SendMessage(hPresetCombo, CB_SETCURSEL, 7, 0);
            }
            // F10: สลับเปิด/ปิด Curve Pattern พร้อมแสดงสถานะ
            if (GetAsyncKeyState(VK_F10) & 1) {
                useCurvePattern = !useCurvePattern;
                updateCurveText();
                SetWindowTextW(hCurveButton, useCurvePattern ? L"Curve: ON (F10)" : L"Curve: OFF (F10)");
                InvalidateRect(hCurveButton, NULL, TRUE);
            }
            // Numpad + และ -: ปรับค่า sensitivity แบบละเอียด โดยมีค่า max sensitivity = 20.0
            if (GetAsyncKeyState(VK_ADD) & 1) {
                double newVal = std::min(20.0, sensitivity.load() + 0.00005);
                setSensitivity(newVal);
            }
            if (GetAsyncKeyState(VK_SUBTRACT) & 1) {
                double newVal = std::max(0.0, sensitivity.load() - 0.00005);
                setSensitivity(newVal);
            }
            // ESC: ออกจากโปรแกรม
            if (GetAsyncKeyState(VK_ESCAPE) & 1) {
                running = false;
                PostMessage(hMainWindow, WM_CLOSE, 0, 0);
            }
            Sleep(10);
        }
    }

    // สร้าง UI
    void createUI(HWND hWnd) {
        hMainWindow = hWnd;

        // สร้าง Fonts
        hFontNormal = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        hFontBold = CreateFont(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        // สร้าง Brushes
        hBrushEnabled = CreateSolidBrush(COLOR_ENABLED);
        hBrushDisabled = CreateSolidBrush(COLOR_DISABLED);
        hBackgroundBrush = CreateSolidBrush(COLOR_BACKGROUND);

        // หัวข้อ
        HWND hTitle = CreateWindowW(L"STATIC", L"Mouse Movement Controller",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            10, 10, 280, 25, hWnd, NULL, NULL, NULL);
        SendMessage(hTitle, WM_SETFONT, (WPARAM)hFontBold, TRUE);

        // สร้าง Static Text สำหรับแสดงสถานะ
        hStatusText = CreateWindowW(L"STATIC", L"Status: DISABLED",
            WS_VISIBLE | WS_CHILD | SS_CENTER | SS_NOTIFY,
            20, 45, 260, 30, hWnd, (HMENU)ID_STATUS_TEXT, NULL, NULL);

        // สร้าง Static Text สำหรับแสดงค่า sensitivity
        hSensitivityText = CreateWindowW(L"STATIC", L"Sensitivity: 1.0551000",
            WS_VISIBLE | WS_CHILD | SS_LEFT,
            20, 85, 260, 20, hWnd, (HMENU)ID_SENSITIVITY_TEXT, NULL, NULL);

        // สร้าง Slider control สำหรับปรับ sensitivity
        hSlider = CreateWindowW(TRACKBAR_CLASSW, L"",
            WS_VISIBLE | WS_CHILD | TBS_HORZ | TBS_AUTOTICKS,
            20, 110, 260, 30, hWnd, (HMENU)ID_SENSITIVITY_SLIDER, NULL, NULL);
        SendMessage(hSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 2000)); // 0.0 to 20.0
        SendMessage(hSlider, TBM_SETPOS, TRUE, (LPARAM)(sensitivity.load() * 100));
        SendMessage(hSlider, TBM_SETTICFREQ, 200, 0);

        // สร้าง Preset Combo Box
        hPresetCombo = CreateWindowW(L"COMBOBOX", L"",
            WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST,
            20, 150, 260, 200, hWnd, (HMENU)ID_PRESET_COMBO, NULL, NULL);

        // เพิ่มตัวเลือกใน Combo Box
        SendMessage(hPresetCombo, CB_ADDSTRING, 0, (LPARAM)L"Preset 1 (F2): 0.8571");
        SendMessage(hPresetCombo, CB_ADDSTRING, 0, (LPARAM)L"Preset 2 (F3): 1.408");
        SendMessage(hPresetCombo, CB_ADDSTRING, 0, (LPARAM)L"Preset 3 (F4): 1.15");
        SendMessage(hPresetCombo, CB_ADDSTRING, 0, (LPARAM)L"Preset 4 (F5): 1.89965");
        SendMessage(hPresetCombo, CB_ADDSTRING, 0, (LPARAM)L"Preset 5 (F6): 1.72225");
        SendMessage(hPresetCombo, CB_ADDSTRING, 0, (LPARAM)L"Preset 6 (F7): 12.89035");
        SendMessage(hPresetCombo, CB_ADDSTRING, 0, (LPARAM)L"Preset 7 (F8): 1.35955");
        SendMessage(hPresetCombo, CB_ADDSTRING, 0, (LPARAM)L"Preset 8 (F9): 2.29865");

        // สร้าง Static Text สำหรับแสดงสถานะ Curve Pattern
        hCurveText = CreateWindowW(L"STATIC", L"Curve Pattern: OFF",
            WS_VISIBLE | WS_CHILD | SS_LEFT,
            20, 185, 260, 20, hWnd, (HMENU)ID_CURVE_TEXT, NULL, NULL);

        // สร้างปุ่ม Toggle
        hToggleButton = CreateWindowW(L"BUTTON", L"Enable (F1)",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            20, 215, 125, 35, hWnd, (HMENU)ID_TOGGLE_BUTTON, NULL, NULL);

        // สร้างปุ่ม Curve Pattern
        hCurveButton = CreateWindowW(L"BUTTON", L"Curve: OFF (F10)",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            155, 215, 125, 35, hWnd, (HMENU)ID_CURVE_BUTTON, NULL, NULL);

        // สร้างปุ่ม -/+ สำหรับปรับ sensitivity ละเอียด
        CreateWindowW(L"BUTTON", L"-",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            20, 260, 40, 30, hWnd, (HMENU)ID_SENSITIVITY_MINUS, NULL, NULL);

        CreateWindowW(L"BUTTON", L"+",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            70, 260, 40, 30, hWnd, (HMENU)ID_SENSITIVITY_PLUS, NULL, NULL);

        // สร้างปุ่ม Exit
        CreateWindowW(L"BUTTON", L"Exit (ESC)",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            155, 260, 125, 30, hWnd, (HMENU)ID_EXIT_BUTTON, NULL, NULL);

        // สร้างเครดิต
        HWND hCredit = CreateWindowW(L"STATIC", L"Made with ❤️ by GodEyrTee",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            20, 300, 260, 20, hWnd, NULL, NULL, NULL);

        // ใช้ Font ปกติสำหรับทุก control
        EnumChildWindows(hWnd, [](HWND hwndChild, LPARAM lParam) -> BOOL {
            SendMessage(hwndChild, WM_SETFONT, (WPARAM)lParam, TRUE);
            return TRUE;
            }, (LPARAM)hFontNormal);

        // ตั้งค่าสถานะเริ่มต้น
        updateStatusText();
        updateSensitivityText();
        updateCurveText();
    }

    // ฟังก์ชันจัดการ window message
    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
        MouseController* pThis = nullptr;

        if (message == WM_NCCREATE) {
            CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
            pThis = (MouseController*)pCreate->lpCreateParams;
            SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pThis);
        }
        else {
            pThis = (MouseController*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        }

        if (pThis) {
            return pThis->HandleMessage(hWnd, message, wParam, lParam);
        }

        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    // จัดการ window message
    LRESULT HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_CREATE:
            // กำหนดให้ใช้ Common Controls
            INITCOMMONCONTROLSEX icex;
            icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
            icex.dwICC = ICC_BAR_CLASSES;
            InitCommonControlsEx(&icex);

            createUI(hWnd);
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
            case ID_TOGGLE_BUTTON:
                enabled = !enabled;
                updateStatusText();
                SetWindowTextW(hToggleButton, enabled ? L"Disable (F1)" : L"Enable (F1)");
                InvalidateRect(hToggleButton, NULL, TRUE);
                return 0;

            case ID_CURVE_BUTTON:
                useCurvePattern = !useCurvePattern;
                updateCurveText();
                SetWindowTextW(hCurveButton, useCurvePattern ? L"Curve: ON (F10)" : L"Curve: OFF (F10)");
                InvalidateRect(hCurveButton, NULL, TRUE);
                return 0;

            case ID_SENSITIVITY_MINUS:
                setSensitivity(std::max(0.0, sensitivity.load() - 0.00005));
                return 0;

            case ID_SENSITIVITY_PLUS:
                setSensitivity(std::min(20.0, sensitivity.load() + 0.00005));
                return 0;

            case ID_EXIT_BUTTON:
                running = false;
                PostMessage(hWnd, WM_CLOSE, 0, 0);
                return 0;

            case ID_PRESET_COMBO:
                if (HIWORD(wParam) == CBN_SELCHANGE) {
                    int idx = SendMessage(hPresetCombo, CB_GETCURSEL, 0, 0);
                    switch (idx) {
                    case 0: setSensitivity(0.8571); break;
                    case 1: setSensitivity(1.408); break;
                    case 2: setSensitivity(1.15); break;
                    case 3: setSensitivity(1.89965); break;
                    case 4: setSensitivity(1.72225); break;
                    case 5: setSensitivity(12.89035); break;
                    case 6: setSensitivity(1.35955); break;
                    case 7: setSensitivity(2.29865); break;
                    }
                }
                return 0;
            }
            break;

        case WM_HSCROLL:
            if ((HWND)lParam == hSlider) {
                int pos = SendMessage(hSlider, TBM_GETPOS, 0, 0);
                double newSensitivity = pos / 100.0;
                setSensitivity(newSensitivity);
            }
            return 0;

        case WM_CTLCOLORSTATIC:
        {
            HDC hdcStatic = (HDC)wParam;
            HWND hwndStatic = (HWND)lParam;

            if (hwndStatic == hStatusText) {
                SetTextColor(hdcStatic, enabled ? COLOR_ENABLED : COLOR_DISABLED);
                SetBkColor(hdcStatic, COLOR_BACKGROUND);
                return (LRESULT)hBackgroundBrush;
            }

            if (hwndStatic == hCurveText) {
                SetTextColor(hdcStatic, useCurvePattern ? COLOR_ENABLED : COLOR_TEXT);
                SetBkColor(hdcStatic, COLOR_BACKGROUND);
                return (LRESULT)hBackgroundBrush;
            }

            SetTextColor(hdcStatic, COLOR_TEXT);
            SetBkColor(hdcStatic, COLOR_BACKGROUND);
            return (LRESULT)hBackgroundBrush;
        }

        case WM_DESTROY:
            // ทำลาย resources
            DeleteObject(hFontNormal);
            DeleteObject(hFontBold);
            DeleteObject(hBrushEnabled);
            DeleteObject(hBrushDisabled);
            DeleteObject(hBackgroundBrush);

            running = false;
            PostQuitMessage(0);
            return 0;
        }

        return DefWindowProc(hWnd, message, wParam, lParam);
    }

public:
    MouseController() {}

    // ลงทะเบียน window class
    bool registerWindowClass(HINSTANCE hInstance) {
        WNDCLASSEX wcex = {};
        wcex.cbSize = sizeof(WNDCLASSEX);
        wcex.style = CS_HREDRAW | CS_VREDRAW;
        wcex.lpfnWndProc = WindowProc;
        wcex.hInstance = hInstance;
        wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
        wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wcex.lpszClassName = WINDOW_CLASS_NAME;

        wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
        wcex.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));

        return RegisterClassEx(&wcex);
    }

    // สร้างและแสดงหน้าต่าง
    bool createWindow(HINSTANCE hInstance) {
        hMainWindow = CreateWindowEx(
            WS_EX_CLIENTEDGE, WINDOW_CLASS_NAME, WINDOW_TITLE,
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
            CW_USEDEFAULT, CW_USEDEFAULT, 300, 360,
            NULL, NULL, hInstance, this);

        if (!hMainWindow) {
            return false;
        }

        // จัดให้หน้าต่างอยู่ตรงกลางหน้าจอ
        RECT rc;
        GetWindowRect(hMainWindow, &rc);
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int windowWidth = rc.right - rc.left;
        int windowHeight = rc.bottom - rc.top;
        int x = (screenWidth - windowWidth) / 2;
        int y = (screenHeight - windowHeight) / 2;

        SetWindowPos(hMainWindow, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

        ShowWindow(hMainWindow, SW_SHOW);
        UpdateWindow(hMainWindow);
        return true;
    }

    // รัน message loop
    void run(HINSTANCE hInstance) {
        if (!registerWindowClass(hInstance) || !createWindow(hInstance)) {
            MessageBox(NULL, L"Failed to create window", L"Error", MB_OK | MB_ICONERROR);
            return;
        }

        timeBeginPeriod(1);
        std::thread mouse(&MouseController::mouseThread, this);
        std::thread keyboard(&MouseController::keyboardThread, this);

        // Message loop
        MSG msg = {};
        while (GetMessage(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        running = false;
        mouse.join();
        keyboard.join();
        timeEndPeriod(1);
    }
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    MouseController controller;
    controller.run(hInstance);
    return 0;
}

//Made with ❤️ by GodEyeTee