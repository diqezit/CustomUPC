#pragma once

#include "core/UpsState.h"

#include <cmath>
#include <format>
#include <array>
#include <span>
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

        [[nodiscard]] inline uint16_t ReadLE16(const uint8_t* p) noexcept {
            return static_cast<uint16_t>(p[0]) |
                (static_cast<uint16_t>(p[1]) << 8);
        }

        [[nodiscard]] inline std::string Hex(uint8_t v) {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%02X", v);
            return buf;
        }

        [[nodiscard]] inline std::string Hex16(uint16_t v) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%04X", v);
            return buf;
        }

        [[nodiscard]] inline std::string DumpHex(
            std::span<const uint8_t> data,
            size_t max = 12)
        {
            std::string result;
            const size_t count = std::min(data.size(), max);

            for (size_t i = 0; i < count; ++i) {
                if (i > 0) result += ' ';
                result += Hex(data[i]);
            }

            if (data.size() > max) result += "...";
            return result;
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

        [[nodiscard]] constexpr bool IsValidFrequency(float f) noexcept {
            return f >= Limit::FREQ_MIN && f <= Limit::FREQ_MAX;
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

    [[nodiscard]] inline float ComputeScale(const HidValueInfo& info) noexcept {
        if (!info.IsValid()) return 1.0f;

        int exp = static_cast<int>(info.unitExp & 0xF);
        if (exp > 7) exp -= 16;
        if (info.unit == 0xD121 || info.unit == 0xF0D121) exp -= 7;

        return std::pow(10.0f, static_cast<float>(exp));
    }

    // ============================================================================
    //                            DECODERS
    // ============================================================================

    class FrequencyDecoder {
    public:
        [[nodiscard]] static float Decode(const uint8_t* buf, bool is16bit) noexcept {
            if (is16bit) {
                return static_cast<float>(DataUtils::ReadLE16(buf + 1)) * 0.1f;
            }

            switch (buf[1]) {
            case 1:  return 60.0f;
            case 2:  return 50.0f;
            default: return static_cast<float>(buf[1]);
            }
        }

        [[nodiscard]] static float DecodeOutputVoltage(uint8_t raw) noexcept {
            if (raw > 10) return static_cast<float>(raw);

            switch (raw) {
            case 3:  return 220.0f;
            case 4:  return 230.0f;
            case 5:  return 240.0f;
            default: return 0.0f;
            }
        }
    };

    class VoltageCodec {
        HidValueInfo input_, battery_, charge_;
        float inputScale_ = 1.0f;
        float batteryScale_ = 1.0f;
        float chargeScale_ = 1.0f;

    public:
        void SetInfo(
            const HidValueInfo& input,
            const HidValueInfo& battery,
            const HidValueInfo& charge)
        {
            input_ = input;
            battery_ = battery;
            charge_ = charge;

            inputScale_ = ComputeScale(input_);
            batteryScale_ = ComputeScale(battery_);
            chargeScale_ = ComputeScale(charge_);
        }

        [[nodiscard]] float DecodeInput(const uint8_t* buf) const noexcept {
            return DecodeValue(buf, input_, inputScale_);
        }

        [[nodiscard]] float DecodeBattery(const uint8_t* buf) const noexcept {
            return DecodeValue(buf, battery_, batteryScale_);
        }

        [[nodiscard]] float DecodeCharge(const uint8_t* buf) const noexcept {
            return DecodeValue(buf, charge_, chargeScale_);
        }

        [[nodiscard]] bool HasDynamicCharge() const noexcept {
            return charge_.IsValid();
        }

    private:
        [[nodiscard]] static float DecodeValue(
            const uint8_t* buf,
            const HidValueInfo& info,
            float scale) noexcept
        {
            if (!buf || !info.IsValid()) return 0.0f;

            const uint32_t raw = info.Is16bit()
                ? DataUtils::ReadLE16(buf + 1)
                : buf[1];

            return static_cast<float>(raw) * scale;
        }
    };

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
    //                          INPUT REPORT TRACKER
    // ============================================================================

    class InputReportTracker {
        bool  hasPowerState_ = false;
        bool  discharging_ = false;
        bool  charging_ = false;
        bool  hasCharge_ = false;
        float chargePct_ = 0.0f;

    public:
        void Reset() noexcept {
            *this = InputReportTracker{};
        }

        void Process(uint8_t rid, const uint8_t* data, int len) noexcept {
            if (len < 2) return;

            switch (rid) {
            case HidUsage::RID_POWER_STATE:
            case 0x13:
                if (len >= 4) {
                    const uint8_t flags = data[3];
                    hasPowerState_ = true;
                    discharging_ = (flags & InputFlag::DISCHARGING) != 0;
                    charging_ = (flags & InputFlag::CHARGING) != 0 && !discharging_;
                }
                break;

            case HidUsage::RID_INPUT_CHARGE:
                hasCharge_ = true;
                chargePct_ = static_cast<float>(DataUtils::ClampPercent(data[1]));
                break;
            }
        }

        void ApplyTo(UpsState& s) const noexcept {
            if (hasCharge_) {
                s.chargePercent = chargePct_;
            }

            if (hasPowerState_) {
                s.onBattery = discharging_;
                s.charging = charging_ && !discharging_;
                s.acPresent = !discharging_ || charging_;

                if (s.onBattery) {
                    s.inputVoltage = 0;
                }
            }
        }

        [[nodiscard]] bool IsDischarging() const noexcept { return discharging_; }
        [[nodiscard]] bool HasCharge() const noexcept { return hasCharge_; }
    };

    // ============================================================================
    //                          EXTENDED SETTERS
    // ============================================================================

    inline void SetExtOutput(Extended& e, float v) noexcept {
        e.outputVoltage = v;
        e.hasOutput = true;
    }

    inline void SetExtInputFreq(Extended& e, float f) noexcept {
        e.inputFrequency = f;
        e.hasInputFreq = true;
    }

    inline void SetExtOutputFreq(Extended& e, float f) noexcept {
        e.outputFrequency = f;
        e.hasOutputFreq = true;
    }

    inline void SetExtSensitivity(Extended& e, uint8_t s) noexcept {
        e.sensitivity = s;
        e.hasSensitivity = true;
    }

    inline void SetExtSelfTest(Extended& e, uint8_t r) noexcept {
        e.selfTestResult = r;
        e.hasSelfTest = true;
    }

    inline void SetExtLowBatt(Extended& e, uint8_t t) noexcept {
        e.lowBatteryThreshold = t;
        e.hasLowBatteryThreshold = true;
    }

    inline void SetExtBeeper(Extended& e, uint8_t c) noexcept {
        e.beeperCode = c;
        e.hasBeeper = true;
    }

    inline void SetExtAvr(Extended& e, uint8_t a, uint8_t p) noexcept {
        e.avrRaw = a;
        e.avrPowerFlags = p;
        e.hasAvr = true;
    }

    // ============================================================================
    //                            STATE ANALYSIS
    // ============================================================================

    [[nodiscard]] constexpr AvrStatus ParseAvrStatus(
        uint8_t avrByte,
        uint8_t powerFlags) noexcept
    {
        if (powerFlags & 0x10) return AvrStatus::Normal;

        switch (avrByte) {
        case 0x00: return AvrStatus::Normal;
        case 0x01: return AvrStatus::Boost;
        case 0x02: return AvrStatus::Trim;
        default:   return AvrStatus::Unknown;
        }
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

    [[nodiscard]] constexpr BatteryLevel GetBatteryLevel(float voltage) noexcept {
        if (voltage >= Limit::BATT_FULL) return BatteryLevel::Full;
        if (voltage >= Limit::BATT_WARN) return BatteryLevel::Normal;
        if (voltage >= Limit::BATT_CRIT) return BatteryLevel::Low;
        if (voltage >= 10.0f)            return BatteryLevel::Critical;
        return BatteryLevel::Empty;
    }

    [[nodiscard]] inline PowerStatus GetPowerStatus(const UpsState& s) noexcept {
        if (!s.connected) return PowerStatus::Disconnected;
        if (s.badBattery) return PowerStatus::BadBattery;
        if (s.overload)   return PowerStatus::Overload;
        if (s.onBattery)  return PowerStatus::OnBattery;
        if (s.charging)   return PowerStatus::Charging;

        if (s.ext.hasAvr) {
            switch (ParseAvrStatus(s.ext.avrRaw, s.ext.avrPowerFlags)) {
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

        [[nodiscard]] static std::wstring FormatRuntimeW(int sec) {
            if (sec <= 0) return L"--:--";

            const int h = sec / 3600;
            const int m = (sec % 3600) / 60;

            return h > 0
                ? std::format(L"{}h {:02d}m", h, m)
                : std::format(L"{}m {:02d}s", m, sec % 60);
        }

        [[nodiscard]] static std::string FormatVoltage(float v) {
            return std::format("{:.1f} V", v);
        }

        [[nodiscard]] static std::string FormatPercent(int v) {
            return std::format("{}%", v);
        }

        [[nodiscard]] static std::string FormatFrequency(float f) {
            return f < 10.0f
                ? "--.- Hz"
                : std::format("{:.1f} Hz", f);
        }

        [[nodiscard]] static std::string FormatAvrStatus(AvrStatus s) {
            switch (s) {
            case AvrStatus::Normal: return "Normal";
            case AvrStatus::Boost:  return "Boost";
            case AvrStatus::Trim:   return "Trim";
            default:                return "--";
            }
        }

        [[nodiscard]] static std::wstring FormatPowerStatus(PowerStatus s) {
            switch (s) {
            case PowerStatus::Disconnected: return L"Disconnected";
            case PowerStatus::Online:       return L"Online";
            case PowerStatus::OnBattery:    return L"On Battery";
            case PowerStatus::Charging:     return L"Charging";
            case PowerStatus::Boost:        return L"AVR Boost";
            case PowerStatus::Trim:         return L"AVR Trim";
            case PowerStatus::Overload:     return L"OVERLOAD";
            case PowerStatus::BadBattery:   return L"Replace Battery";
            }
            return L"Unknown";
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