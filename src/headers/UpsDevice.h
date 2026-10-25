#pragma once

#include "UpsState.h"

#include <windows.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <hidapi.h>

#include <array>
#include <cstddef>
#include <vector>

namespace Ups {

    enum class Fid : uint8_t {
        Input,
        Output,
        Battery,
        Charge,
        Load,
        Runtime,
        Charging,
        Discharging,
        AcPresent,
        Avr,
        Overload,
        BadBattery,
        ShutdownImminent,
        LowBattery,
        RatedPower,
        InputNominal,
        BatteryNominal,
        UpsDate,
        BatteryDate,
        RuntimeLimit,
        TransferReason,
        Beeper,
        Sensitivity,
        LowTransfer,
        HighTransfer,
        Count
    };

    struct ValueRef {
        uint8_t  reportId = 0;
        uint16_t page = 0;
        uint16_t usage = 0;
        uint16_t link = 0;
        uint32_t unit = 0;
        float    scale = 1.0f;
        int32_t  logicalMin = 0;
        int32_t  logicalMax = 0;
    };

    struct HidProfile {
        std::array<ValueRef, static_cast<std::size_t>(Fid::Count)> refs;

        const ValueRef& operator[](Fid id) const {
            return refs[static_cast<std::size_t>(id)];
        }

        ValueRef& operator[](Fid id) {
            return refs[static_cast<std::size_t>(id)];
        }
    };

    class UpsDevice {
    public:
        UpsDevice() = default;
        ~UpsDevice();

        UpsDevice(const UpsDevice&) = delete;
        UpsDevice& operator=(const UpsDevice&) = delete;

        bool Connect();
        void Disconnect();

        const UpsState& ReadState();
        const HidProfile& Profile() const { return profile_; }

        enum class Setting : uint8_t {
            Beeper,
            Sensitivity,
            LowTransfer,
            HighTransfer
        };

        bool SetValue(Setting setting, uint32_t value);

    private:
        enum {
            HISTORY_SIZE = 5,
            POLL_EXTRA_EVERY = 5,
            MAX_FAIL_COUNT = 3,
            RUNTIME_RESERVE_SEC = 120,
            MAX_RUNTIME_SEC = 86400,
            HID_STRING_CHARS = 128
        };

        static constexpr float BATTERY_VOLT_MIN = 5.0f;
        static constexpr float BATTERY_VOLT_MAX = 16.0f;
        static constexpr float INPUT_VOLT_MIN = 85.0f;
        static constexpr float INPUT_VOLT_MAX = 280.0f;
        static constexpr float OUTPUT_VOLT_MIN = 100.0f;
        static constexpr float OUTPUT_VOLT_MAX = 260.0f;

        hid_device* dev_ = nullptr;
        PHIDP_PREPARSED_DATA ppd_ = nullptr;

        HidProfile profile_;
        UpsState state_;
        std::vector<uint8_t> buf_;

        int pollCount_ = 0;
        int failCount_ = 0;

        std::array<int, HISTORY_SIZE> runtime_{};
        int runtimeCount_ = 0;
        int runtimeIndex_ = 0;

        int ReadFields(bool extra);

        bool OpenReports(const char* path);
        bool ReadCaps();
        void MapValue(
            const HIDP_VALUE_CAPS& cap,
            const std::vector<HIDP_LINK_COLLECTION_NODE>& nodes);
    };

} // namespace Ups
