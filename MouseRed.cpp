#define NOMINMAX
#include <windows.h>
#include <stdio.h>
#include <atomic>
#include <thread>
#include <chrono>
#include <algorithm>
#include <iomanip>

#pragma comment(lib, "winmm.lib")  // เพื่อให้ใช้งาน timeBeginPeriod/timeEndPeriod ได้

class MouseController {
private:
    std::atomic<bool> enabled{ false };
    std::atomic<bool> running{ true };
    std::atomic<double> sensitivity{ 1.0551 }; // ค่าเริ่มต้นอยู่ในช่วง 0-1 (0.5 = 50% ของ max speed)
    HANDLE hConsole;

    void initConsole() {
        hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode;
        GetConsoleMode(hConsole, &mode);
        SetConsoleMode(hConsole, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }

    void setColor(const char* color) {
        printf("%s", color);
    }

    void printWithColor(const char* color, const char* text) {
        setColor(color);
        printf("%s", text);
        setColor("\033[0m"); // Reset color
    }

    // อัปเดตค่า sensitivity พร้อมแสดงผล
    void setSensitivity(double value) {
        sensitivity = value;
        printWithColor("\033[93m", "Sensitivity set to: ");
        printf("%.7f\n", value);
    }

    // ฟังก์ชันสำหรับเคลื่อนเมาส์ด้วย fixed timestep loop
    void mouseThread() {
        using namespace std::chrono;
        // ตั้ง thread priority ให้สูงขึ้นเพื่อลด jitter
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

        auto nextTick = steady_clock::now();
        const double dt = 0.01; // 10ms (0.01 วินาที) ต่อ tick
        double pixelAccumulator = 0.0; // ตัวสะสมพิกเซล

        // กำหนด max speed เมื่อ sensitivity = 1 ให้เคลื่อนที่ 40px/s
        const double maxSpeed = 40.0; // px/s

        while (running) {
            // ใช้ dt คงที่ทุก tick
            if (enabled && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
                double currentSensitivity = sensitivity.load(); // ค่าความไว (0-1)
                double speed = currentSensitivity * maxSpeed;      // คำนวณความเร็ว (px/s)
                pixelAccumulator += speed * dt;                    // เพิ่มจำนวนพิกเซลที่จะเลื่อนใน tick นี้

                int movePixels = static_cast<int>(pixelAccumulator); // จำนวนพิกเซลเต็มที่ควรเลื่อน
                if (movePixels > 0) {
                    mouse_event(MOUSEEVENTF_MOVE, 0, movePixels, 0, 0);
                    pixelAccumulator -= movePixels;
                }
            }
            else {
                // เมื่อปล่อยปุ่มซ้าย รีเซ็ต accumulator เพื่อเริ่มใหม่ในครั้งถัดไป
                pixelAccumulator = 0.0;
            }

            nextTick += milliseconds(10);   // กำหนดเวลาของ tick ถัดไป
            std::this_thread::sleep_until(nextTick); // รอให้ถึงเวลาต่อไป
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

            // ปรับค่า sensitivity แบบละเอียดด้วย Numpad + และ -
            if (GetAsyncKeyState(VK_ADD) & 1) {
                setSensitivity(std::min(20.0, sensitivity.load() + 0.00005));
            }
            if (GetAsyncKeyState(VK_SUBTRACT) & 1) {
                setSensitivity(std::max(0.0, sensitivity.load() - 0.00005));
            }

            // ออกจากโปรแกรมเมื่อกด ESC
            if (GetAsyncKeyState(VK_ESCAPE) & 1) {
                running = false;
            }
            Sleep(0.025);
        }
    }

    void printHeader() {
        system("cls");
        printWithColor("\033[96m", "+--------------------------------+\n");
        printWithColor("\033[96m", "|      Mouse Movement Controller     |\n");
        printWithColor("\033[96m", "+--------------------------------+\n\n");

        printWithColor("\033[93m", "Controls:\n");
        printf("----------------------------------\n");
        printWithColor("\033[97m", "F1"); printf(": Toggle Control\n");
        printWithColor("\033[97m", "F2-F9"); printf(": Set Sensitivity (0-1)\n");
        printWithColor("\033[97m", "Numpad +/-"); printf(": Fine-tune Sensitivity\n");
        printWithColor("\033[97m", "ESC"); printf(": Exit\n\n");

        printWithColor("\033[93m", "Current Settings:\n");
        printf("----------------------------------\n");
    }

public:
    MouseController() {
        initConsole();
    }

    void run() {
        printHeader();
        printWithColor("\033[91m", "Status: DISABLED\n");
        printf("Initial sensitivity: %.7f\n\n", sensitivity.load());

        // เพิ่มความละเอียดของ timer system
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
