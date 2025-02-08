#MouseRed
MouseRed is a Windows application that allows you to control the continuous downward movement of the mouse when holding the left button. The movement speed can be adjusted in real time.

Features
Smooth Mouse Movement: The mouse moves downward continuously while the left button is held.
Real-time Sensitivity Adjustment: Sensitivity can be set within the range of 0-1 (e.g., 0.1, 0.2, ... 1.0), where higher values result in faster movement (e.g., at sensitivity = 1, the speed is 40px/s).
Customizable Hotkeys:
F1: Enable/disable movement
F2-F9: Preset sensitivity settings
Numpad + / -: Fine-tune sensitivity
ESC: Exit the program
Console Display: Uses ANSI escape sequences to display the current status and sensitivity settings with color-coded output.
Stable and Smooth Performance: Implements a fixed timestep loop with improved timer resolution (timeBeginPeriod) and increased thread priority to minimize jitter and ensure consistent movement speed.
How to Use
Download the latest release from GitHub.
Compile the project using a compatible development environment (Visual Studio or any toolchain that supports Windows API).
Run the program and use the provided hotkeys to control or adjust movement speed as needed.
Support & Development
If you encounter issues or have suggestions, feel free to open an issue or submit a pull request on the repository.
Join the development and contribute to making the program even better!
