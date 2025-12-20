# UPS Monitor

[![Platform](https://img.shields.io/badge/Platform-Windows-0078D6?style=for-the-badge&logo=windows)](https://github.com/diqezit/CustomUPC)
[![Language](https://img.shields.io/badge/Language-C%2B%2B20-00599C?style=for-the-badge&logo=cplusplus)](https://github.com/diqezit/CustomUPC)
[![GUI](https://img.shields.io/badge/GUI-ImGui-4DB6AC?style=for-the-badge)](https://github.com/diqezit/CustomUPC)
[![Graphics](https://img.shields.io/badge/Graphics-DirectX%2011-68BC71?style=for-the-badge&logo=nvidia)](https://github.com/diqezit/CustomUPC)

Lightweight Windows app for real-time monitoring of APC UPS devices via USB HID. Built with C++20, Dear ImGui and DirectX 11

## Features
- Real-time monitoring of battery, input/output voltage, load, charge and runtime
- AVR status for Automatic Voltage Regulation modes
- Modern dark-themed always-on-top draggable UI
- Low resource usage with zero external dependencies
- Auto-reconnect on USB disconnect

## Screenshot
![Main Window Screenshot](https://github.com/user-attachments/assets/a8f2e847-557b-4890-9fe5-057a5c6267e0)

## Supported Devices
Works with APC, MGE and Belkin UPS models supporting USB HID Power Device class
Vendor IDs: APC 0x051D, MGE 0x0463, Belkin 0x050D

## Reverse Engineering
Uses IDA Pro reverse engineering of official APC PowerChute software to uncover hidden HID report IDs and scaling factors for accurate voltage and status readings

## Build Requirements
Visual Studio 2022, CMake 3.20+ and Windows SDK 10.0+
Header-only dependencies for HIDAPI and ImGui are included

```bash
git clone https://github.com/diqezit/CustomUPC
cd CustomUPC
cmake -B build
cmake --build build --config Release
```

## Architecture
| Module | Purpose |
|--------|---------|
| UpsState.h | Data structures, enums and constants |
| UpsMonitor.h | Data processing, validation and formatting |
| UpsDevice.h | USB HID communication and device management |
| MainWindow.h | UI rendering with ImGui and DirectX11 |

Data flow: UpsDevice -> UpsMonitor -> UpsState -> MainWindow

## Usage
1. Connect your UPS via USB
2. Run UpsMonitor.exe
3. Monitor real-time status

Click and drag the header area to move the window

Status indicators:
- ONLINE: Normal operation on AC power
- CHARGING: Battery charging
- ON BATTERY: Running on battery power
- AVR BOOST/TRIM: Voltage regulation active
- OVERLOAD: UPS overloaded
- REPLACE BATTERY: Battery needs replacement
- DISCONNECTED: No UPS detected

## Troubleshooting
- UPS not detected: Check USB cable/port, verify in Device Manager, run as Administrator
- Incorrect readings: Wait for initial calibration, ensure battery is healthy
- Application won't start: Install Visual C++ Redistributable and update DirectX 11

Logs are written to ups_monitor.log in the application directory
