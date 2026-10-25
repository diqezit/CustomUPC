# UPS Monitor

<p align="center">
  <a href="https://github.com/diqezit/CustomUPC"><img src="https://img.shields.io/badge/Platform-Windows-0078D6?style=for-the-badge&logo=windows" alt="Platform"></a>
  <a href="https://github.com/diqezit/CustomUPC"><img src="https://img.shields.io/badge/Language-C%2B%2B20-00599C?style=for-the-badge&logo=cplusplus" alt="Language"></a>
  <a href="https://github.com/diqezit/CustomUPC"><img src="https://img.shields.io/badge/GUI-ImGui-4DB6AC?style=for-the-badge" alt="GUI"></a>
  <a href="https://github.com/diqezit/CustomUPC"><img src="https://img.shields.io/badge/Graphics-DirectX%2011-68BC71?style=for-the-badge&logo=nvidia" alt="Graphics"></a>
</p>

Lightweight Windows app for real-time monitoring of APC UPS devices via USB HID. Built with C++20, Dear ImGui and DirectX 11

## Features
- Real-time monitoring of battery and input voltage, load, charge and runtime
- Output voltage only when exposed by device descriptor
- AVR status for Automatic Voltage Regulation modes
- Hardware low-battery and imminent-shutdown warnings when exposed by descriptor
- Read-only Details page with rated power, nominal voltages, manufacture dates, runtime limit and last transfer reason
- Configurable beeper, transfer sensitivity, low and high transfer voltages
- Modern dark-themed always-on-top draggable UI
- Low resource usage with bundled runtime DLLs
- Auto-reconnect on USB disconnect

## Screenshot

<p align="center">
  <img src="https://github.com/user-attachments/assets/f6aff976-0eca-4ae6-b5ff-77729e78fb84" width="300" alt="Monitor">
  <img src="https://github.com/user-attachments/assets/c94deb5c-e926-429c-84d9-42ebbddfddd2" width="300" alt="Settings">
</p>

## Supported Devices
APC USB HID devices, vendor ID 0x051D
Tested with Back-UPS XS 650CI; available readings and settings depend on descriptor

## Reverse Engineering
Uses HID descriptor fields and mappings checked against official APC PowerChute software

## Build Requirements
Visual Studio 2022, CMake 3.20+ and Windows SDK 10.0+
HIDAPI and ImGui headers, import libraries and runtime DLLs are included

```bash
git clone https://github.com/diqezit/CustomUPC
cd CustomUPC
cmake -B build
cmake --build build --config Release
```

## Source files
| Module | Purpose |
|--------|---------|
| src/headers/UpsState.h | Plain UPS data and setting codes |
| src/headers/UpsDevice.h | Device interface and descriptor field types |
| src/UpsDevice.cpp | HID parsing, polling and existing settings |
| src/headers/MainWindow.h | Three UI entry points |
| src/MainWindow.cpp | Formatting, display cache and ImGui rendering |
| src/main.cpp | Win32 window, DirectX 11 and message loop |
| src/headers/Logger.h and src/Logger.c | Small C logging API |

Data flow: USB HID -> UpsDevice -> UpsState -> UI

## Usage
1. Connect your UPS via USB
2. Run CustomUPC.exe
3. Monitor real-time status

Click and drag the header area to move the window
Open Details for values reported by UPS. Manufacture dates are not replacement dates, and rated power is not measured load

Runtime limit is shown read-only in Details in minutes and represents a warning threshold, not a power-off delay
Use Tab, arrow keys and Enter for native keyboard navigation

Status indicators:
- ONLINE: Normal operation on AC power
- CHARGING: Battery charging
- ON BATTERY: Running on battery power
- AVR BOOST/TRIM: Voltage regulation active
- OVERLOAD: UPS overloaded
- REPLACE BATTERY: Battery needs replacement
- DISCONNECTED: No UPS detected
- LOW BATTERY: UPS reports charge below its capacity limit
- UPS SHUTDOWN SOON: UPS reports imminent shutdown

## Troubleshooting
- UPS not detected: Check USB cable/port, verify in Device Manager, run as Administrator
- Incorrect readings: Wait for initial calibration, ensure battery is healthy
- Application won't start: Install Visual C++ Redistributable and update DirectX 11

Logs are written to ups_monitor.log in the application directory
