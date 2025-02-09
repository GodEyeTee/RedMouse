#define NOMINMAX
#include <windows.h>
#include <stdio.h>
#include <atomic>
#include <thread>
#include <chrono>
#include <cmath>
#include <algorithm>

#pragma comment(lib, "winmm.lib")

class MouseController {
private:
    std::atomic<bool> enabled{ false };
    std::atomic<bool> running{ true };
    std::atomic<double> sensitivity{ 1.0551 }; // Default sensitivity
    std::atomic<bool> useCurvePattern{ false };
    HANDLE hConsole;

    void initConsole() {
        hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    }

    void setColor(const char* color) {
        printf("%s", color);
    }

    void printWithColor(const char* color, const char* text) {
        setColor(color);
        printf("%s", text);
        setColor("\033[0m"); // Reset color
    }

    // ตั้งค่าความไวพร้อมแสดงผลในคอนโซล
    void setSensitivity(double value) {
        sensitivity = value;
        printWithColor("\033[93m", "Sensitivity set to: ");
        printf("%.7f\n", value);
    }

    // ฟังก์ชัน interpolation แบบ quadratic Bezier
    // รับค่า start, control, end และ t (0 <= t <= 1)
    POINT bezierInterpolation(const POINT& start, const POINT& control, const POINT& end, double t) {
        double u = 1.0 - t;
        double tt = t * t;
        double uu = u * u;
        POINT result;
        result.x = static_cast<LONG>(uu * start.x + 2 * u * t * control.x + tt * end.x);
        result.y = static_cast<LONG>(uu * start.y + 2 * u * t * control.y + tt * end.y);
        return result;
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
                printWithColor(enabled ? "\033[92m" : "\033[91m",
                    enabled ? "Status: ENABLED\n" : "Status: DISABLED\n");
            }
            // กำหนด sensitivity (0-1) ผ่าน F2-F9
            if (GetAsyncKeyState(VK_F2) & 1) setSensitivity(0.8571);
            if (GetAsyncKeyState(VK_F3) & 1) setSensitivity(1.4065);
            if (GetAsyncKeyState(VK_F4) & 1) setSensitivity(1.15);
            if (GetAsyncKeyState(VK_F5) & 1) setSensitivity(8.2);
            if (GetAsyncKeyState(VK_F6) & 1) setSensitivity(8);
            if (GetAsyncKeyState(VK_F7) & 1) setSensitivity(9.095);
            if (GetAsyncKeyState(VK_F8) & 1) setSensitivity(1.36);
            if (GetAsyncKeyState(VK_F9) & 1) setSensitivity(8.605);
            // F10: สลับเปิด/ปิด Curve Pattern พร้อมแสดงสถานะ
            if (GetAsyncKeyState(VK_F10) & 1) {
                useCurvePattern = !useCurvePattern;
                printWithColor("\033[93m", "Curve Pattern: ");
                printf("%s\n", useCurvePattern ? "ON" : "OFF");
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
            }
            Sleep(10);
        }
    }

    void printHeader() {
        system("cls");
        printWithColor("\033[96m", "+--------------------------------+\n");
        printWithColor("\033[96m", "|      Mouse Movement Controller      |\n");
        printWithColor("\033[96m", "+--------------------------------+\n\n");
        printWithColor("\033[93m", "Controls:\n");
        printf("----------------------------------\n");
        printWithColor("\033[97m", "F1");   printf(": Toggle Control\n");
        printWithColor("\033[97m", "F2-F9"); printf(": Set Sensitivity (presets)\n");
        printWithColor("\033[97m", "F10");  printf(": Toggle Curve Pattern\n");
        printWithColor("\033[97m", "Numpad +/-");  printf(": Fine-tune Sensitivity\n");
        printWithColor("\033[97m", "ESC");  printf(": Exit\n\n");
    }

public:
    MouseController() {
        initConsole();
    }

    void run() {
        printHeader();
        printWithColor("\033[91m", "Status: DISABLED\n");
        printf("Initial sensitivity: %.7f\n\n", sensitivity.load());
        timeBeginPeriod(1);
        std::thread mouse(&MouseController::mouseThread, this);
        std::thread keyboard(&MouseController::keyboardThread, this);
        mouse.join();
        keyboard.join();
        timeEndPeriod(1);
        printWithColor("\033[93m", "\nProgram terminated.\n");
    }
};

int main() {
    SetConsoleTitle(L"Mouse Movement Controller");
    MouseController controller;
    controller.run();
    return 0;
}
