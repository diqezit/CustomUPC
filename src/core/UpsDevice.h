#pragma once

#include "core/UpsState.h"
#include "core/UpsMonitor.h"
#include "core/HidDevice.h"
#include "utils/Logger.h"

#include <hidapi.h>

#include <vector>
#include <string>
#include <algorithm>

namespace Ups {

    // ============================================================================
    //                        DEVICE CONFIGURATION
    // ============================================================================

    namespace DeviceConfig {
        inline constexpr int    MAX_FAILURES = 3;
        inline constexpr int    EXTENDED_INTERVAL = 5;
        inline constexpr int    DRAIN_READS = 6;
        inline constexpr int    READ_TIMEOUT_MS = 20;
        inline constexpr size_t STRING_LEN = 128;
    }

    // ============================================================================
    //                               UPS DEVICE
    // ============================================================================

    class UpsDevice {
    public:
        UpsDevice() { hid_init(); }
        ~UpsDevice() { Disconnect(); hid_exit(); }

        UpsDevice(const UpsDevice&) = delete;
        UpsDevice& operator=(const UpsDevice&) = delete;

        [[nodiscard]] bool IsConnected() const noexcept {
            return dev_ != nullptr;
        }

        bool Connect() {
            if (dev_) return true;

            hid_device_info* devs = hid_enumerate(0, 0);
            hid_device_info* found = nullptr;
            std::string foundPath;

            for (auto* cur = devs; cur; cur = cur->next) {
                if (Vendor::IsSupported(cur->vendor_id)) {
                    found = cur;
                    if (cur->path) foundPath = cur->path;
                    break;
                }
            }

            if (!found) {
                hid_free_enumeration(devs);
                return false;
            }

            vid_ = found->vendor_id;
            pid_ = found->product_id;
            dev_ = hid_open_path(found->path);
            hid_free_enumeration(devs);

            if (!dev_) {
                LOG_ERROR("hid_open_path failed");
                ResetIdentity();
                return false;
            }

            hid_.Open(dev_, foundPath);

            const ModelProfile& p = hid_.Profile();
            codec_.SetInfo(p.input, p.battery, p.charge);

            ReadStrings();
            ResetSession();

            LOG_INFO(
                "Connected: VID=0x" + DataUtils::Hex16(vid_) +
                " PID=0x" + DataUtils::Hex16(pid_)
            );

            return true;
        }

        void Disconnect() {
            if (!dev_) return;

            LOG_INFO("Disconnected");
            hid_.Close();
            hid_close(dev_);

            dev_ = nullptr;
            ResetIdentity();
            ResetSession();
        }

        UpsState ReadState() {
            UpsState s = state_;
            s.timestamp = static_cast<DWORD>(GetTickCount64());

            if (!dev_ && !Connect()) {
                s.connected = false;
                return s;
            }

            DrainInput();
            const int ok = ReadCoreReports(s);

            if (pollCount_ % DeviceConfig::EXTENDED_INTERVAL == 0) {
                ReadExtendedReports(s);
            }

            if (ok == 0 && ++failCount_ >= DeviceConfig::MAX_FAILURES) {
                LOG_ERROR("Max failures, disconnecting");
                Disconnect();
                s.connected = false;
                return s;
            }

            if (ok > 0) failCount_ = 0;

            input_.ApplyTo(s);
            FinalizeState(s);

            state_ = s;
            ++pollCount_;

            return s;
        }

    private:
        hid_device* dev_ = nullptr;
        uint16_t    vid_ = 0;
        uint16_t    pid_ = 0;

        HidReportReader    hid_;
        VoltageCodec       codec_;
        RuntimeSmoother    runtime_;
        InputReportTracker input_;
        UpsState           state_{};

        std::wstring manufacturer_;
        std::wstring product_;
        std::wstring serial_;

        int  pollCount_ = 0;
        int  failCount_ = 0;
        bool dateParsed_ = false;

        void ResetIdentity() noexcept {
            vid_ = pid_ = 0;
            manufacturer_.clear();
            product_.clear();
            serial_.clear();
        }

        void ResetSession() noexcept {
            state_ = {};
            input_.Reset();
            runtime_.Reset();

            pollCount_ = 0;
            failCount_ = 0;
            dateParsed_ = false;
        }

        using StringGetter = int (*)(hid_device*, wchar_t*, size_t);

        void ReadString(StringGetter get, std::wstring& out) {
            wchar_t b[DeviceConfig::STRING_LEN] = {};

            if (get(dev_, b, DeviceConfig::STRING_LEN) == 0) {
                out = b;
            }
        }

        void ReadStrings() {
            ReadString(hid_get_manufacturer_string, manufacturer_);
            ReadString(hid_get_product_string, product_);
            ReadString(hid_get_serial_number_string, serial_);
        }

        bool ReadReport(ReportIndex idx, size_t minLen = 0) {
            return hid_.ReadFeature(hid_.Rid(idx)) && hid_.Size() >= minLen;
        }

        // keeps shown runtime below point where the UPS cuts power
        void ApplyRuntime(UpsState& s, int seconds) {
            const int smoothed = runtime_.Update(seconds);
            s.runtimeSeconds = std::max(
                0,
                smoothed - Config::RUNTIME_RESERVE_MINUTES * 60
            );
        }

        // input reports queue up between polls 
        // and stale entries would mask a power event
        void DrainInput() {
            std::vector<uint8_t> b(
                std::max(hid_.InputLen(), HidReportConfig::MIN_FEATURE_LEN)
            );

            for (int i = 0; i < DeviceConfig::DRAIN_READS; ++i) {
                const int r = hid_.ReadInput(
                    b.data(),
                    b.size(),
                    DeviceConfig::READ_TIMEOUT_MS
                );

                if (r <= 0) break;
                input_.Process(b[0], b.data(), r);
            }
        }

        template <class Setter>
        void ReadExtByte(ReportIndex idx, Setter set, UpsState& s) {
            uint8_t raw = 0;
            if (hid_.ReadByte(idx, raw)) set(s.ext, raw);
        }

        int ReadCoreReports(UpsState& s) {
            const ModelProfile& p = hid_.Profile();
            int ok = 0;
            float v = 0.0f;

            const auto got = [&](bool done) {
                if (done) ++ok;
                return done;
            };

            // Status
            if (got(ReadReport(IDX_STATUS))) {
                s.rawFlags = hid_.Data()[1];
            }

            // Load
            const size_t len = hid_.Size();
            if (got(hid_.ReadMetric(p.vLoad, IDX_TELEMETRY, v,
                [len](const uint8_t* b) {
                    return len >= 3 ? static_cast<float>(b[2]) : 0.0f;
                })))
            {
                s.loadPercent = DataUtils::ClampPercent(
                    static_cast<int>(v + 0.5f)
                );
            }

            // Battery Voltage
            if (got(hid_.ReadMetric(p.vBattery, IDX_BATT_V, v,
                [this](const uint8_t* b) { return codec_.DecodeBattery(b); })))
            {
                if (Validators::IsValidBatteryVoltage(v)) {
                    s.batteryVoltage = v;
                }
            }

            // Input Voltage
            // 
            // firmware reports garbage while on battery
            // so the last mains reading is kept instead
            //
            if (got(hid_.ReadMetric(p.vInput, IDX_INPUT_V, v,
                [this](const uint8_t* b) { return codec_.DecodeInput(b); })))
            {
                if (!input_.IsDischarging() &&
                    Validators::IsValidInputVoltage(v))
                {
                    s.inputVoltage = v;
                }
            }

            // Charge
            // 
            // interrupt reports are more accurate and win when present
            //

            if (got(hid_.ReadMetric(p.vCharge, IDX_CHARGE, v,
                [this](const uint8_t* b) {
                    return codec_.HasDynamicCharge()
                        ? codec_.DecodeCharge(b)
                        : static_cast<float>(b[1]);
                })))
            {
                if (!input_.HasCharge()) {
                    s.chargePercent = DataUtils::ClampPercent(v);
                }
            }

            // Runtime
            // 
            // spec says seconds (unit 0x1001) while APC models leave unit empty and send minute
            //
            if (got(hid_.ReadValue(p.vRuntime, v))) {
                const bool inSeconds = p.vRuntime.unit == HidExtra::UNIT_SECONDS;
                ApplyRuntime(
                    s,
                    static_cast<int>(inSeconds ? v + 0.5f : v * 60.0f + 0.5f)
                );
            }
            else if (got(ReadReport(IDX_RUNTIME, 3))) {
                ApplyRuntime(s, DataUtils::ReadLE16(hid_.Data() + 1));
            }

            return ok;
        }

        void ReadExtendedReports(UpsState& s) {
            const ModelProfile& p = hid_.Profile();
            float v = 0.0f;

            // Output Voltage
            if (hid_.ReadMetric(p.vOutput, IDX_OUT_VOLT, v,
                [](const uint8_t* b) {
                    return FrequencyDecoder::DecodeOutputVoltage(b[1]);
                }))
            {
                if (Validators::IsValidOutputVoltage(v)) {
                    SetExtOutput(s.ext, v);
                }
            }

            // frequency report covers both lines
            // 
            // but mirroring it onto input side only holds while mains is present
            ///

            if (ReadReport(IDX_FREQUENCY)) {
                v = FrequencyDecoder::Decode(hid_.Data(), false);
                if (Validators::IsValidFrequency(v)) {
                    SetExtOutputFreq(s.ext, v);
                    if (s.inputVoltage > 0) {
                        SetExtInputFreq(s.ext, v);
                    }
                }
            }

            // Dedicated frequency fields override the generic report
            if (hid_.ReadFrequency(p.inputFreq, v))  SetExtInputFreq(s.ext, v);
            if (hid_.ReadFrequency(p.outputFreq, v)) SetExtOutputFreq(s.ext, v);

            // AVR
            if (ReadReport(IDX_AVR, 4)) {
                SetExtAvr(s.ext, hid_.Data()[1], hid_.Data()[3]);
            }

            // Single-byte settings
            ReadExtByte(IDX_LOW_BATT, SetExtLowBatt, s);
            ReadExtByte(IDX_SENSITIVITY, SetExtSensitivity, s);
            ReadExtByte(IDX_BEEPER, SetExtBeeper, s);
            ReadExtByte(IDX_SELF_TEST, SetExtSelfTest, s);
        }

        void FinalizeState(UpsState& s) {
            s.manufacturer = manufacturer_;
            s.product = product_;
            s.serial = serial_;

            if (!dateParsed_ && !s.serial.empty()) {
                s.date = ParseDateFromSerial(s.serial);
                dateParsed_ = true;
            }

            const auto has = [&](auto flag) {
                return Flag::Has(s.rawFlags, flag);
            };

            if (!input_.IsDischarging()) {
                s.charging = has(Flag::CHARGING);
                s.onBattery = has(Flag::DISCHARGING);
                s.acPresent = has(Flag::AC_PRESENT) || s.charging;
            }

            s.lowBattery = has(Flag::LOW_BATTERY);
            s.overload = has(Flag::OVERLOAD);
            s.badBattery = has(Flag::BAD_BATTERY);
            s.connected = true;
        }
    };

} // namespace Ups