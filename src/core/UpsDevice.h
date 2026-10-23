#pragma once

#include "core/UpsState.h"
#include "core/UpsMonitor.h"
#include "core/HidDevice.h"
#include "utils/Logger.h"

#include <hidapi.h>

#include <string>
#include <algorithm>

namespace Ups {

    // ============================================================================
    //                        DEVICE CONFIGURATION
    // ============================================================================

    namespace DeviceConfig {
        inline constexpr int    MAX_FAILURES = 3;
        inline constexpr int    EXTENDED_INTERVAL = 5;
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

            if (!hid_.Open(dev_, foundPath)) {
                LOG_ERROR("HID descriptor parse failed");
                hid_close(dev_);
                dev_ = nullptr;
                ResetIdentity();
                return false;
            }

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

            const int ok = ReadCoreReports(s);

            // Settings and slow-changing values do not need a per second read,
            // so they share the slower extended path
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

            FinalizeState(s);

            state_ = s;
            ++pollCount_;

            return s;
        }

        // Successful writes update the cached state so the UI
        // does not wait for the next extended poll
        bool SetBeeper(uint8_t code) {
            if (!dev_ || !hid_.WriteValue(hid_.Profile().vBeeper, code)) {
                return false;
            }
            SetExtBeeper(state_.ext, code);
            return true;
        }

        bool SetSensitivity(uint8_t code) {
            if (!dev_ || !hid_.WriteValue(hid_.Profile().vSensitivity, code)) {
                return false;
            }
            SetExtSensitivity(state_.ext, code);
            return true;
        }

        bool SetLowTransfer(uint16_t volts) {
            if (!dev_ || !hid_.WriteValue(hid_.Profile().vLowTransfer, volts)) {
                return false;
            }
            SetExtLowTransfer(state_.ext, volts);
            return true;
        }

        bool SetHighTransfer(uint16_t volts) {
            if (!dev_ || !hid_.WriteValue(hid_.Profile().vHighTransfer, volts)) {
                return false;
            }
            SetExtHighTransfer(state_.ext, volts);
            return true;
        }

    private:
        hid_device* dev_ = nullptr;
        uint16_t    vid_ = 0;
        uint16_t    pid_ = 0;

        HidReportReader    hid_;
        RuntimeSmoother    runtime_;
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

        // keeps shown runtime below point where the UPS cuts power
        void ApplyRuntime(UpsState& s, int seconds) {
            const int smoothed = runtime_.Update(seconds);
            s.runtimeSeconds = std::max(
                0,
                smoothed - Config::RUNTIME_RESERVE_MINUTES * 60
            );
        }

        int ReadCoreReports(UpsState& s) {
            const ModelProfile& p = hid_.Profile();
            int ok = 0;
            float v = 0.0f;

            const auto got = [&](bool done) {
                if (done) ++ok;
                return done;
                };

            // Power status is read from the descriptor fields themselves.
            if (got(hid_.ReadValue(p.vAcPresent, v))) {
                s.acPresent = v != 0.0f;
            }
            if (got(hid_.ReadValue(p.vDischarging, v))) {
                s.onBattery = v != 0.0f;
            }
            if (got(hid_.ReadValue(p.vCharging, v))) {
                s.charging = v != 0.0f;
            }
            if (got(hid_.ReadValue(p.vOverload, v))) {
                s.overload = v != 0.0f;
            }
            if (got(hid_.ReadValue(p.vBadBattery, v))) {
                s.badBattery = v != 0.0f;
            }

            // Load
            if (got(hid_.ReadValue(p.vLoad, v))) {
                s.loadPercent = DataUtils::ClampPercent(
                    static_cast<int>(v + 0.5f)
                );
            }

            // Battery Voltage
            if (got(hid_.ReadValue(p.vBattery, v))) {
                if (Validators::IsValidBatteryVoltage(v)) {
                    s.batteryVoltage = v;
                }
            }

            // Input Voltage
            //
            // On battery / on AC the firmware can report garbage for input voltage,
            // so the last valid AC reading is kept instead of showing a false value
            //
            if (got(hid_.ReadValue(p.vInput, v))) {
                if (!s.onBattery &&
                    Validators::IsValidInputVoltage(v))
                {
                    s.inputVoltage = v;
                }
            }

            // Charge
            if (got(hid_.ReadValue(p.vCharge, v))) {
                s.chargePercent = DataUtils::ClampPercent(v);
            }

            // Runtime
            //
            // The official driver applies the HID unit; this field is seconds.
            //
            if (got(hid_.ReadValue(p.vRuntime, v))) {
                if (p.vRuntime.unit == HidExtra::UNIT_SECONDS) {
                    ApplyRuntime(s, static_cast<int>(v + 0.5f));
                }
            }

            return ok;
        }

        void ReadExtendedReports(UpsState& s) {
            const ModelProfile& p = hid_.Profile();
            float v = 0.0f;

            // Output Voltage is only present when the descriptor exposes an
            // Output collection.
            if (hid_.ReadValue(p.vOutput, v)) {
                if (Validators::IsValidOutputVoltage(v)) {
                    SetExtOutput(s.ext, v);
                }
            }

            // AVR
            if (hid_.ReadValue(p.vAvr, v)) {
                SetExtAvr(s.ext, static_cast<uint8_t>(v));
            }

            // Descriptor-bound settings
            if (hid_.ReadValue(p.vSensitivity, v)) {
                SetExtSensitivity(s.ext, static_cast<uint8_t>(v));
            }
            if (hid_.ReadValue(p.vBeeper, v)) {
                SetExtBeeper(s.ext, static_cast<uint8_t>(v));
            }
            if (hid_.ReadValue(p.vLowTransfer, v)) {
                SetExtLowTransfer(s.ext, static_cast<uint16_t>(v + 0.5f));
            }
            if (hid_.ReadValue(p.vHighTransfer, v)) {
                SetExtHighTransfer(s.ext, static_cast<uint16_t>(v + 0.5f));
            }
        }

        void FinalizeState(UpsState& s) {
            s.manufacturer = manufacturer_;
            s.product = product_;
            s.serial = serial_;

            if (!dateParsed_ && !s.serial.empty()) {
                s.date = ParseDateFromSerial(s.serial);
                dateParsed_ = true;
            }

            s.connected = true;
        }
    };

} // namespace Ups
