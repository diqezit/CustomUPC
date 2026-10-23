#pragma once

#include "core/UpsState.h"

#include <cmath>
#include <cstdio>
#include <format>
#include <array>
#include <algorithm>
#include <cwctype>

namespace Ups {

    // ============================================================================
    //                          DATA UTILITIES
    // ============================================================================

    namespace DataUtils {

        [[nodiscard]] inline int ClampPercent(int v) noexcept {
            return std::clamp(v, 0, 100);
        }

        [[nodiscard]] inline float ClampPercent(float v) noexcept {
            return std::clamp(v, 0.0f, 100.0f);
        }

        [[nodiscard]] inline std::string Hex16(uint16_t v) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%04X", v);
            return buf;
        }

    } // namespace DataUtils

    // ============================================================================
    //                            VALIDATORS
    // ============================================================================

    namespace Validators {

        [[nodiscard]] constexpr bool IsValidBatteryVoltage(float v) noexcept {
            return v >= Limit::BATT_V_MIN && v <= Limit::BATT_V_MAX;
        }

        [[nodiscard]] constexpr bool IsValidInputVoltage(float v) noexcept {
            return v >= Limit::INPUT_V_MIN && v <= Limit::INPUT_V_MAX;
        }

        [[nodiscard]] constexpr bool IsValidOutputVoltage(float v) noexcept {
            return v >= Limit::OUTPUT_V_MIN && v <= Limit::OUTPUT_V_MAX;
        }

    } // namespace Validators

    // ============================================================================
    //                            DATE FUNCTIONS
    // ============================================================================

    [[nodiscard]] inline int GetDateAge(const Date& d) noexcept {
        if (!d.IsValid()) return 0;

        SYSTEMTIME now;
        GetSystemTime(&now);

        int age = now.wYear - d.year;
        if (now.wMonth < static_cast<WORD>(d.month)) --age;

        return age > 0 ? age : 0;
    }

    [[nodiscard]] inline Date ParseDateFromSerial(const std::wstring& serial) {
        Date d;

        if (serial.length() < 6) return d;
        if (!std::iswdigit(serial[2]) || !std::iswdigit(serial[5])) return d;

        const int year = (serial[2] - L'0') * 10 + (serial[3] - L'0');
        const int week = (serial[4] - L'0') * 10 + (serial[5] - L'0');

        if (week >= 1 && week <= 52) {
            d.year = 2000 + year;
            d.month = (week * 12 + 25) / 52;
            d.day = 1;
        }

        return d;
    }

    // ============================================================================
    //                         HID VALUE PROCESSING
    // ============================================================================

    [[nodiscard]] inline float ComputeScale(
        uint32_t unit,
        uint32_t unitExp) noexcept
    {
        int exp = static_cast<int>(unitExp & 0xF);
        if (exp > 7) exp -= 16;
        if (unit == 0xD121 || unit == 0xF0D121) exp -= 7;

        return std::pow(10.0f, static_cast<float>(exp));
    }

    // ============================================================================
    //                            RUNTIME SMOOTHER
    // ============================================================================

    class RuntimeSmoother {
        static constexpr size_t MAX_HISTORY = 5;

        std::array<int, MAX_HISTORY> history_{};
        size_t count_ = 0;
        size_t index_ = 0;

    public:
        void Reset() noexcept {
            count_ = index_ = 0;
            history_.fill(0);
        }

        [[nodiscard]] int Update(int seconds) noexcept {
            if (seconds <= 0 || seconds > Limit::RUNTIME_MAX) return 0;

            history_[index_] = seconds;
            index_ = (index_ + 1) % MAX_HISTORY;
            if (count_ < MAX_HISTORY) ++count_;

            int sum = 0;
            for (size_t i = 0; i < count_; ++i) {
                sum += history_[i];
            }

            return sum / static_cast<int>(count_);
        }
    };

    // ============================================================================
    //                          EXTENDED SETTERS
    // ============================================================================

    inline void SetExtOutput(Extended& e, float v) noexcept {
        e.outputVoltage = v;
        e.hasOutput = true;
    }

    inline void SetExtSensitivity(Extended& e, uint8_t s) noexcept {
        e.sensitivity = s;
        e.hasSensitivity = true;
    }

    inline void SetExtBeeper(Extended& e, uint8_t c) noexcept {
        e.beeperCode = c;
        e.hasBeeper = true;
    }

    inline void SetExtAvr(Extended& e, uint8_t code) noexcept {
        e.avrRaw = code;
        e.hasAvr = true;
    }

    inline void SetExtLowTransfer(Extended& e, uint16_t volts) noexcept {
        e.lowTransferVoltage = volts;
        e.hasLowTransfer = true;
    }

    inline void SetExtHighTransfer(Extended& e, uint16_t volts) noexcept {
        e.highTransferVoltage = volts;
        e.hasHighTransfer = true;
    }

    // ============================================================================
    //                            STATE ANALYSIS
    // ============================================================================

    [[nodiscard]] constexpr AvrStatus ParseAvrStatus(uint8_t avrCode) noexcept {
        // APC vendor status: 0x08 normal, 0x04 boost, 0x10 trim
        if (avrCode == 0x00 || (avrCode & 0x08)) return AvrStatus::Normal;
        if (avrCode & 0x04) return AvrStatus::Boost;
        if (avrCode & 0x10) return AvrStatus::Trim;
        return AvrStatus::Unknown;
    }

    [[nodiscard]] constexpr Health GetBatteryHealth(float voltage) noexcept {
        if (voltage < Limit::BATT_CRIT) return Health::Critical;
        if (voltage < Limit::BATT_WARN) return Health::Warning;
        return Health::Good;
    }

    [[nodiscard]] constexpr Health GetInputHealth(float voltage) noexcept {
        if (voltage < 1.0f) return Health::Unknown;
        if (voltage < Limit::INPUT_V_LOW)  return Health::Warning;
        if (voltage > Limit::INPUT_V_HIGH) return Health::Warning;
        return Health::Good;
    }

    [[nodiscard]] constexpr Health GetRuntimeHealth(int seconds) noexcept {
        const int mins = seconds / 60;
        if (mins < Limit::RUNTIME_CRIT_MIN) return Health::Critical;
        if (mins < Limit::RUNTIME_WARN_MIN) return Health::Warning;
        return Health::Good;
    }

    [[nodiscard]] constexpr Health GetChargeHealth(float percent) noexcept {
        if (percent < Limit::CHARGE_CRIT) return Health::Critical;
        if (percent < Limit::CHARGE_WARN) return Health::Warning;
        return Health::Good;
    }

    [[nodiscard]] constexpr Health GetLoadHealth(float percent) noexcept {
        if (percent > Limit::LOAD_CRIT) return Health::Critical;
        if (percent > Limit::LOAD_WARN) return Health::Warning;
        return Health::Good;
    }

    [[nodiscard]] inline PowerStatus GetPowerStatus(const UpsState& s) noexcept {
        if (!s.connected) return PowerStatus::Disconnected;
        if (s.badBattery) return PowerStatus::BadBattery;
        if (s.overload)   return PowerStatus::Overload;
        if (s.onBattery)  return PowerStatus::OnBattery;
        if (s.charging)   return PowerStatus::Charging;

        if (s.ext.hasAvr) {
            switch (ParseAvrStatus(s.ext.avrRaw)) {
            case AvrStatus::Boost: return PowerStatus::Boost;
            case AvrStatus::Trim:  return PowerStatus::Trim;
            default: break;
            }
        }

        return PowerStatus::Online;
    }

    // UpsState convenience wrappers
    [[nodiscard]] inline Health GetBatteryHealth(const UpsState& s) noexcept {
        return GetBatteryHealth(s.batteryVoltage);
    }

    [[nodiscard]] inline Health GetInputHealth(const UpsState& s) noexcept {
        return GetInputHealth(s.inputVoltage);
    }

    [[nodiscard]] inline Health GetRuntimeHealth(const UpsState& s) noexcept {
        return GetRuntimeHealth(s.runtimeSeconds);
    }

    [[nodiscard]] inline Health GetChargeHealth(const UpsState& s) noexcept {
        return GetChargeHealth(s.chargePercent);
    }

    [[nodiscard]] inline Health GetLoadHealth(const UpsState& s) noexcept {
        return GetLoadHealth(static_cast<float>(s.loadPercent));
    }

    [[nodiscard]] inline bool HasValidBatteryVoltage(const UpsState& s) noexcept {
        return Validators::IsValidBatteryVoltage(s.batteryVoltage);
    }

    [[nodiscard]] inline bool HasValidInputVoltage(const UpsState& s) noexcept {
        return Validators::IsValidInputVoltage(s.inputVoltage);
    }

    // ============================================================================
    //                              FORMATTER
    // ============================================================================

    class Formatter final {
    public:
        [[nodiscard]] static std::string FormatRuntime(int sec) {
            if (sec <= 0) return "--:--";

            const int h = sec / 3600;
            const int m = (sec % 3600) / 60;

            return h > 0
                ? std::format("{}h {:02d}m", h, m)
                : std::format("{}m {:02d}s", m, sec % 60);
        }

        [[nodiscard]] static std::string FormatVoltage(float v) {
            return std::format("{:.1f} V", v);
        }

        [[nodiscard]] static std::string FormatAvrStatus(AvrStatus s) {
            switch (s) {
            case AvrStatus::Normal: return "Normal";
            case AvrStatus::Boost:  return "Boost";
            case AvrStatus::Trim:   return "Trim";
            default:                return "--";
            }
        }

        [[nodiscard]] static const char* BeeperToString(uint8_t c) noexcept {
            switch (c) {
            case BeeperCode::DISABLED: return "Off";
            case BeeperCode::ENABLED:  return "On";
            case BeeperCode::MUTED:    return "Muted";
            default:                   return "Unknown";
            }
        }

        [[nodiscard]] static const char* SensitivityToString(uint8_t c) noexcept {
            switch (c) {
            case SensitivityCode::LOW:    return "Low";
            case SensitivityCode::MEDIUM: return "Medium";
            case SensitivityCode::HIGH:   return "High";
            default:                      return "Unknown";
            }
        }
    };

} // namespace Ups