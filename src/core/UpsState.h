#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include <cstdint>

namespace Ups {

    // ============================================================================
    //                            CONFIGURATION
    // ============================================================================

    namespace Config {
        inline constexpr int RUNTIME_RESERVE_MINUTES = 2;
        inline constexpr int UPDATE_INTERVAL_MS = 1000;
    }

    // ============================================================================
    //                          HID PROTOCOL CONSTANTS
    // ============================================================================

    namespace HidUsage {
        // Pages
        inline constexpr uint16_t PAGE_POWER_DEVICE = 0x84;
        inline constexpr uint16_t PAGE_BATTERY_SYSTEM = 0x85;
        inline constexpr uint16_t PAGE_APC_VENDOR = 0xFF86;

        // Usages
        inline constexpr uint16_t VOLTAGE = 0x30;
        inline constexpr uint16_t CONFIG_VOLTAGE = 0x40;
        inline constexpr uint16_t FREQ_INPUT = 0x37;
        inline constexpr uint16_t FREQ_OUTPUT = 0x38;
        inline constexpr uint16_t FREQUENCY = 0x32;
        inline constexpr uint16_t AUDIBLE_ALARM = 0x5A;
        inline constexpr uint16_t REMAINING_CAPACITY = 0x66;
        inline constexpr uint16_t CAPACITY_LIMIT = 0x2A;
        inline constexpr uint16_t AVR_STATUS = 0x60;
        inline constexpr uint16_t SENSITIVITY = 0x61;

        // Report IDs
        inline constexpr uint8_t RID_STATUS = 0x01;
        inline constexpr uint8_t RID_FREQUENCY = 0x02;
        inline constexpr uint8_t RID_OUT_VOLT = 0x03;
        inline constexpr uint8_t RID_POWER_STATE = 0x06;
        inline constexpr uint8_t RID_VOLTAGE = 0x07;
        inline constexpr uint8_t RID_TELEMETRY = 0x09;
        inline constexpr uint8_t RID_BEEPER = 0x0B;
        inline constexpr uint8_t RID_INPUT_CHARGE = 0x0C;
        inline constexpr uint8_t RID_CHARGE = 0x0D;
        inline constexpr uint8_t RID_SELFTEST = 0x18;
        inline constexpr uint8_t RID_RUNTIME = 0x74;
    }

    // ============================================================================
    //                              VENDOR IDS
    // ============================================================================

    namespace Vendor {
        inline constexpr uint16_t APC = 0x051D;
        inline constexpr uint16_t MGE = 0x0463;
        inline constexpr uint16_t BELKIN = 0x050D;

        [[nodiscard]] constexpr bool IsSupported(uint16_t vid) noexcept {
            return vid == APC || vid == MGE || vid == BELKIN;
        }
    }

    // ============================================================================
    //                             STATUS FLAGS
    // ============================================================================

    namespace Flag {
        inline constexpr uint8_t CHARGING = 0x01;
        inline constexpr uint8_t DISCHARGING = 0x02;
        inline constexpr uint8_t LOW_BATTERY = 0x04;
        inline constexpr uint8_t AC_PRESENT = 0x08;
        inline constexpr uint8_t BAD_BATTERY = 0x10;
        inline constexpr uint8_t OVERLOAD = 0x20;

        [[nodiscard]] constexpr bool Has(uint8_t flags, uint8_t mask) noexcept {
            return (flags & mask) != 0;
        }
    }

    namespace InputFlag {
        inline constexpr uint8_t DISCHARGING = 0x10;
        inline constexpr uint8_t CHARGING = 0x08;
    }

    // ============================================================================
    //                             CODE CONSTANTS
    // ============================================================================

    namespace BeeperCode {
        inline constexpr uint8_t DISABLED = 0x01;
        inline constexpr uint8_t ENABLED = 0x02;
        inline constexpr uint8_t MUTED = 0x03;

        [[nodiscard]] constexpr bool IsOn(uint8_t c) noexcept {
            return c == ENABLED;
        }
    }

    namespace SensitivityCode {
        inline constexpr uint8_t LOW = 0x00;
        inline constexpr uint8_t MEDIUM = 0x01;
        inline constexpr uint8_t HIGH = 0x02;
    }

    // ============================================================================
    //                                ENUMS
    // ============================================================================

    enum class Health : uint8_t {
        Good,
        Warning,
        Critical,
        Unknown
    };

    enum class PowerStatus : uint8_t {
        Disconnected,
        Online,
        OnBattery,
        Charging,
        Boost,
        Trim,
        Overload,
        BadBattery
    };

    enum class BatteryLevel : uint8_t {
        Full,
        Normal,
        Low,
        Critical,
        Empty
    };

    enum class AvrStatus : uint8_t {
        Normal,
        Boost,
        Trim,
        Unknown
    };

    // ============================================================================
    //                               LIMITS
    // ============================================================================

    namespace Limit {
        // Battery voltage
        inline constexpr float BATT_V_MIN = 5.0f;
        inline constexpr float BATT_V_MAX = 16.0f;
        inline constexpr float BATT_FULL = 13.20f;
        inline constexpr float BATT_WARN = 11.50f;
        inline constexpr float BATT_CRIT = 10.60f;

        // Input voltage
        inline constexpr float INPUT_V_MIN = 85.0f;
        inline constexpr float INPUT_V_MAX = 280.0f;
        inline constexpr float INPUT_V_LOW = 200.0f;
        inline constexpr float INPUT_V_HIGH = 250.0f;

        // Output voltage
        inline constexpr float OUTPUT_V_MIN = 100.0f;
        inline constexpr float OUTPUT_V_MAX = 260.0f;

        // Frequency
        inline constexpr float FREQ_MIN = 45.0f;
        inline constexpr float FREQ_MAX = 65.0f;

        // Thresholds
        inline constexpr float CHARGE_CRIT = 20.0f;
        inline constexpr float CHARGE_WARN = 50.0f;
        inline constexpr float LOAD_WARN = 50.0f;
        inline constexpr float LOAD_CRIT = 80.0f;

        // Runtime
        inline constexpr int RUNTIME_CRIT_MIN = 5;
        inline constexpr int RUNTIME_WARN_MIN = 15;
        inline constexpr int RUNTIME_MAX = 86400;
    }

    // ============================================================================
    //                                 DATE
    // ============================================================================

    struct Date {
        int year = 0;
        int month = 0;
        int day = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return year >= 1980 && year <= 2100 &&
                month >= 1 && month <= 12;
        }
    };

    // ============================================================================
    //                              HID VALUE INFO
    // ============================================================================

    struct HidValueInfo {
        uint8_t  reportId = 0;
        uint16_t usage = 0;
        uint16_t bitSize = 0;
        uint32_t unit = 0;
        uint32_t unitExp = 0;
        long     logicalMax = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return reportId != 0;
        }

        [[nodiscard]] constexpr bool Is16bit() const noexcept {
            return bitSize > 8;
        }
    };

    // ============================================================================
    //                            EXTENDED STATE
    // ============================================================================

    struct Extended {
        float outputVoltage = 0;
        float inputFrequency = 0;
        float outputFrequency = 0;

        uint8_t sensitivity = 0xFF;
        uint8_t selfTestResult = 0xFF;
        uint8_t beeperCode = 0xFF;
        uint8_t lowBatteryThreshold = 0;
        uint8_t avrRaw = 0;
        uint8_t avrPowerFlags = 0;

        bool hasOutput = false;
        bool hasInputFreq = false;
        bool hasOutputFreq = false;
        bool hasSensitivity = false;
        bool hasSelfTest = false;
        bool hasBeeper = false;
        bool hasLowBatteryThreshold = false;
        bool hasAvr = false;

        constexpr void Reset() noexcept {
            *this = Extended{};
        }
    };

    // ============================================================================
    //                              UPS STATE
    // ============================================================================

    struct UpsState {
        float batteryVoltage = 0;
        float inputVoltage = 0;
        float chargePercent = 0;
        int   loadPercent = 0;
        int   runtimeSeconds = 0;

        bool connected = false;
        bool onBattery = false;
        bool charging = false;
        bool lowBattery = false;
        bool overload = false;
        bool badBattery = false;
        bool acPresent = false;

        uint8_t rawFlags = 0;

        std::wstring manufacturer;
        std::wstring product;
        std::wstring serial;

        DWORD    timestamp = 0;
        Date     date{};
        Extended ext{};

        [[nodiscard]] constexpr float GetChargeNormalized() const noexcept {
            return chargePercent / 100.0f;
        }

        [[nodiscard]] constexpr float GetLoadNormalized() const noexcept {
            return static_cast<float>(loadPercent) / 100.0f;
        }
    };

} // namespace Ups