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
        inline constexpr uint16_t LOW_VOLTAGE_TRANSFER = 0x53;
        inline constexpr uint16_t HIGH_VOLTAGE_TRANSFER = 0x54;
        inline constexpr uint16_t AUDIBLE_ALARM = 0x5A;
        inline constexpr uint16_t REMAINING_CAPACITY = 0x66;
        inline constexpr uint16_t AVR_STATUS = 0x60;
        inline constexpr uint16_t SENSITIVITY = 0x61;
        inline constexpr uint16_t CHARGING = 0x44;
        inline constexpr uint16_t DISCHARGING = 0x45;
        inline constexpr uint16_t AC_PRESENT = 0xD0;
        inline constexpr uint16_t OVERLOAD = 0x65;
        inline constexpr uint16_t NEED_REPLACEMENT = 0x4B;
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
    //                             CODE CONSTANTS
    // ============================================================================

    namespace BeeperCode {
        inline constexpr uint8_t DISABLED = 0x01;
        inline constexpr uint8_t ENABLED = 0x02;
        inline constexpr uint8_t MUTED = 0x03;
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
    //                            EXTENDED STATE
    // ============================================================================

    struct Extended {
        float outputVoltage = 0;

        uint8_t sensitivity = 0xFF;
        uint8_t beeperCode = 0xFF;
        uint8_t avrRaw = 0;

        // Transfer thresholds are whole volts on this device
        uint16_t lowTransferVoltage = 0;
        uint16_t highTransferVoltage = 0;

        bool hasOutput = false;
        bool hasSensitivity = false;
        bool hasBeeper = false;
        bool hasAvr = false;
        bool hasLowTransfer = false;
        bool hasHighTransfer = false;

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
        bool overload = false;
        bool badBattery = false;
        bool acPresent = false;

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