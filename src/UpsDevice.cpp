#include "UpsDevice.h"
#include "Logger.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace Ups {
    namespace {

        namespace Hid {
            enum : uint16_t {
                APC_VENDOR_ID = 0x051D,
                POWER_PAGE = 0x84
            };

            enum Collection : uint16_t {
                Any     = 0,
                Ups     = 0x04,
                Battery = 0x12,
                Input   = 0x1A,
                Output  = 0x1C,
                Summary = 0x24
            };

            enum Usage : uint32_t {
                Voltage                = 0x00840030,
                Load                   = 0x00840035,
                ConfigVoltage          = 0x00840040,
                RatedPower             = 0x00840044,
                LowTransfer            = 0x00840053,
                HighTransfer           = 0x00840054,
                DelayBeforeShutdown    = 0x00840057,
                Test                   = 0x00840058,
                Beeper                 = 0x0084005A,
                Overload               = 0x00840065,
                ShutdownImminent       = 0x00840069,
                CommunicationLost      = 0x00840073,
                ProductStringIndex     = 0x008400FE,
                SerialStringIndex      = 0x008400FF,

                RuntimeLimit           = 0x0085002A,
                LowBattery             = 0x00850042,
                RuntimeExpired         = 0x00850043,
                Charging               = 0x00850044,
                Discharging            = 0x00850045,
                BadBattery             = 0x0085004B,
                BatteryShutdownRequest = 0x00850058, // PowerChute writes 1 to request shutdown
                Charge                 = 0x00850066,
                Runtime                = 0x00850068,
                ManufactureDate        = 0x00850085,
                AcPresent              = 0x008500D0,
                BatteryPresent         = 0x008500D1,
                VoltageNotRegulated    = 0x008500DB,

                BatteryReplacementDate = 0xFF860016,
                RestartIntervalDelay   = 0xFF860024,
                FirmwareRevision       = 0xFF860042,
                TransferReason         = 0xFF860052,
                Avr                    = 0xFF860060,
                Sensitivity            = 0xFF860061,
                SystemTime             = 0xFF860077,
                UpsPowerFailReason     = 0xFF860078,
                UsbFirmwareRevision    = 0xFF860079,
                SystemDate             = 0xFF86007B,
                ApcShutdownRequest     = 0xFF86007C, // PowerChute writes 1 to request shutdown
                ApcDelayBeforeShutdown = 0xFF86007D,
                OutputPowerCutOffDelay = 0xFF86007F,
                OutletDelay1           = 0xFF860081,
                OutletDelay2           = 0xFF860082,
                DisplayDimmer          = 0xFF860083,
                LedDimmer              = 0xFF860084,
                InputFrequency         = 0xFF860085,
                OutputFrequency        = 0xFF860086,
                ApcOutputVoltage       = 0xFF860087,
                VfdScreenSaver         = 0xFF860088,
                OutletDelay3           = 0xFF860089,
                GreenModeType          = 0xFF860091,
                ClientLoadThreshold    = 0xFF860092,
                GreenMode              = 0xFF860094,
                ClientModeSensitivity  = 0xFF860095,
                GreenModeTurnOnDelay   = 0xFF860096,
                GreenModeTurnOffDelay  = 0xFF860097,
                OutletHeader           = 0xFF860099
            };

            enum Unit : uint32_t {
                Seconds = 0x1001,
                Watt    = 0xD121,
                Volt    = 0xF0D121
            };
        }

        enum class Poll : uint8_t {
            Live,
            Extra
        };

        // firstOnly: same usage can come from multiple collections
        // keep first match so live source does not jump on reparse
        // onlyIfSeconds: skip report when unit is not seconds
        // because firmware may report runtime in wrong unit
        enum FieldFlag : uint8_t {
            NoFlag = 0,
            FirstOnly = 1 << 0,
            OnlyIfSeconds = 1 << 1
        };

        struct FieldDef {
            Fid id;
            uint32_t usage;
            uint16_t collection;
            Poll poll;
            uint8_t flags = NoFlag;
        };

        // order matches FieldDef: id usage collection poll flags
        constexpr auto kFields = std::to_array<FieldDef>({
            // live - polled every cycle to detect dead link fast
            { Fid::AcPresent,        Hid::Usage::AcPresent,        Hid::Collection::Any,     Poll::Live,  FirstOnly },
            { Fid::Discharging,      Hid::Usage::Discharging,      Hid::Collection::Any,     Poll::Live,  FirstOnly },
            { Fid::Charging,         Hid::Usage::Charging,         Hid::Collection::Any,     Poll::Live,  FirstOnly },
            { Fid::Overload,         Hid::Usage::Overload,         Hid::Collection::Any,     Poll::Live           },
            { Fid::BadBattery,       Hid::Usage::BadBattery,       Hid::Collection::Any,     Poll::Live           },
            { Fid::ShutdownImminent, Hid::Usage::ShutdownImminent, Hid::Collection::Any,     Poll::Live,  FirstOnly },
            { Fid::LowBattery,       Hid::Usage::LowBattery,       Hid::Collection::Any,     Poll::Live,  FirstOnly },
            { Fid::Load,             Hid::Usage::Load,             Hid::Collection::Any,     Poll::Live           },
            { Fid::Battery,          Hid::Usage::Voltage,          Hid::Collection::Battery, Poll::Live           },
            { Fid::Input,            Hid::Usage::Voltage,          Hid::Collection::Input,   Poll::Live           },
            { Fid::Charge,           Hid::Usage::Charge,           Hid::Collection::Any,     Poll::Live           },
            { Fid::Runtime,          Hid::Usage::Runtime,          Hid::Collection::Any,     Poll::Live           },

            // extra - slow reports that must not affect failCount
            { Fid::Output,           Hid::Usage::Voltage,          Hid::Collection::Output,  Poll::Extra           },
            { Fid::Avr,              Hid::Usage::Avr,              Hid::Collection::Any,     Poll::Extra           },
            { Fid::Sensitivity,      Hid::Usage::Sensitivity,      Hid::Collection::Any,     Poll::Extra           },
            { Fid::Beeper,           Hid::Usage::Beeper,           Hid::Collection::Any,     Poll::Extra           },
            { Fid::LowTransfer,      Hid::Usage::LowTransfer,      Hid::Collection::Any,     Poll::Extra           },
            { Fid::HighTransfer,     Hid::Usage::HighTransfer,     Hid::Collection::Any,     Poll::Extra           },
            { Fid::RatedPower,       Hid::Usage::RatedPower,       Hid::Collection::Any,     Poll::Extra           },
            { Fid::InputNominal,     Hid::Usage::ConfigVoltage,    Hid::Collection::Input,   Poll::Extra           },
            { Fid::BatteryNominal,   Hid::Usage::ConfigVoltage,    Hid::Collection::Battery, Poll::Extra           },
            { Fid::UpsDate,          Hid::Usage::ManufactureDate,  Hid::Collection::Ups,     Poll::Extra           },
            { Fid::BatteryDate,      Hid::Usage::ManufactureDate,  Hid::Collection::Battery, Poll::Extra           },
            { Fid::RuntimeLimit,     Hid::Usage::RuntimeLimit,     Hid::Collection::Any,     Poll::Extra, OnlyIfSeconds },
            { Fid::TransferReason,   Hid::Usage::TransferReason,   Hid::Collection::Input,   Poll::Extra           },
        });

        // APC packs year since 1980, month and day into 16 bits
        // reject unset or impossible dates so they do not look like manufacture dates
        bool DecodeDate(uint16_t value, Date& date) {
            constexpr uint8_t days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

            const int year = 1980 + (value >> 9);
            const int month = (value >> 5) & 0xF;
            const int day = value & 0x1F;

            if (month < 1 || month > 12 || day < 1)
                return false;

            const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);

            if (day > days[month - 1] + (month == 2 && leap))
                return false;

            date = { year, month, day };
            return true;
        }

        // hidapi returns strings as wchar
        // state stores them as utf8 std::string
        std::string ToUtf8(const wchar_t* text) {
            const int size = WideCharToMultiByte(
                CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
            if (!size)
                return {};

            std::string result(size, '\0');
            WideCharToMultiByte(
                CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
            result.pop_back();

            return result;
        }

    } // namespace

    // destructor must close device
    // caller may not call Disconnect on exit
    UpsDevice::~UpsDevice() {
        Disconnect();
    }

    // open first APC device found
    bool UpsDevice::Connect() {
        if (dev_)
            return true;

        hid_device_info* devices = hid_enumerate(Hid::APC_VENDOR_ID, 0);
        if (!devices)
            return false;

        // copy path before freeing enumeration list
        const uint16_t pid = devices->product_id;
        const std::string path = devices->path;
        hid_free_enumeration(devices);

        dev_ = hid_open_path(path.c_str());

        if (!dev_) {
            LOG_ERROR("hid_open_path failed");
            return false;
        }

        if (!OpenReports(path.c_str())) {
            LOG_ERROR("HID descriptor parse failed");
            Disconnect();
            return false;
        }

        // string reads optional, failure not fatal
        wchar_t text[HID_STRING_CHARS];
        if (hid_get_manufacturer_string(dev_, text, HID_STRING_CHARS) == 0)
            state_.manufacturer = ToUtf8(text);

        if (hid_get_product_string(dev_, text, HID_STRING_CHARS) == 0)
            state_.product = ToUtf8(text);

        if (hid_get_serial_number_string(dev_, text, HID_STRING_CHARS) == 0)
            state_.serial = ToUtf8(text);

        LOG_INFO("Connected: VID=0x%04X PID=0x%04X", Hid::APC_VENDOR_ID, pid);
        return true;
    }

    // reset per-connection state so next Connect starts fresh
    // counters and profile must not carry over from old device
    void UpsDevice::Disconnect() {
        if (!dev_)
            return;

        LOG_INFO("Disconnected");
        if (ppd_)
            HidD_FreePreparsedData(ppd_);

        hid_close(dev_);

        dev_ = nullptr;
        ppd_ = nullptr;
        profile_ = {};
        state_ = {};
        buf_.clear();

        pollCount_ = 0;
        failCount_ = 0;
        runtimeCount_ = 0;
        runtimeIndex_ = 0;
    }

    // live fields poll every cycle to detect dead link fast
    // extra fields poll every N cycles and never touch failCount
    // MAX_FAIL_COUNT failures in a row triggers reconnect
    const UpsState& UpsDevice::ReadState() {
        if (!dev_ && !Connect())
            return state_;

        const int readCount = ReadFields(false);
        if (pollCount_ % POLL_EXTRA_EVERY == 0)
            ReadFields(true);

        if (readCount > 0)
            failCount_ = 0;
        else
            ++failCount_;

        if (failCount_ >= MAX_FAIL_COUNT) {
            LOG_ERROR("Max failures, disconnecting");
            Disconnect();
            return state_;
        }

        state_.connected = true;
        ++pollCount_;

        return state_;
    }

    bool UpsDevice::SetValue(Setting setting, uint32_t value) {
        const ValueRef* ref = nullptr;

        switch (setting) {
        case Setting::Beeper:
            ref = &profile_[Fid::Beeper];
            break;

        case Setting::Sensitivity:
            ref = &profile_[Fid::Sensitivity];
            break;

        case Setting::LowTransfer:
            ref = &profile_[Fid::LowTransfer];
            break;

        case Setting::HighTransfer:
            ref = &profile_[Fid::HighTransfer];
            break;
        }

        if (!ref || !ref->reportId || !dev_ || buf_.empty())
            return false;

        // read-modify-write so other fields in same report stay intact
        // raw write would zero them
        buf_[0] = ref->reportId;
        if (hid_get_feature_report(dev_, buf_.data(), buf_.size()) <= 0)
            return false;

        const NTSTATUS status = HidP_SetUsageValue(
            HidP_Feature,
            ref->page,
            ref->link,
            ref->usage,
            value,
            ppd_,
            reinterpret_cast<PCHAR>(buf_.data()),
            static_cast<ULONG>(buf_.size()));

        if (status != HIDP_STATUS_SUCCESS)
            return false;

        if (hid_send_feature_report(dev_, buf_.data(), buf_.size()) < 0)
            return false;

        // cache write result right after write so UI does not wait
        // for next extra poll to reflect new value
        switch (setting) {

        case Setting::Beeper:
            state_.ext.beeperCode = static_cast<uint8_t>(value);
            state_.ext.hasBeeper = true;
            break;

        case Setting::Sensitivity:
            state_.ext.sensitivity = static_cast<uint8_t>(value);
            state_.ext.hasSensitivity = true;
            break;

        case Setting::LowTransfer:
            state_.ext.lowTransferVoltage = static_cast<uint16_t>(value);
            state_.ext.hasLowTransfer = true;
            break;

        case Setting::HighTransfer:
            state_.ext.highTransferVoltage = static_cast<uint16_t>(value);
            state_.ext.hasHighTransfer = true;
            break;
        }

        return true;
    }

    // live fields read every poll so dead link shows fast
    // extra fields are slow and must not affect failCount
    // each case maps one usage to one state field
    int UpsDevice::ReadFields(bool extra) {
        int readCount = 0;
        float value = 0.0f;
        Poll poll = Poll::Live;
        Extended& ext = state_.ext;

        if (extra)
            poll = Poll::Extra;

        for (const FieldDef& field : kFields) {
            if (field.poll != poll)
                continue;

            const ValueRef& ref = profile_[field.id];

            // some usages report in whatever unit firmware picked
            // runtime in wrong unit would be garbage seconds
            if ((field.flags & OnlyIfSeconds) && ref.unit != Hid::Unit::Seconds)
                continue;

            if (!ref.reportId)
                continue;

            // report id goes in byte 0 on Windows hidapi
            buf_[0] = ref.reportId;
            if (hid_get_feature_report(dev_, buf_.data(), buf_.size()) <= 0)
                continue;

            ULONG raw = 0;
            const NTSTATUS status = HidP_GetUsageValue(
                HidP_Feature,
                ref.page,
                ref.link,
                ref.usage,
                &raw,
                ppd_,
                reinterpret_cast<PCHAR>(buf_.data()),
                static_cast<ULONG>(buf_.size()));

            if (status != HIDP_STATUS_SUCCESS)
                continue;

            value = static_cast<float>(raw) * ref.scale;
            ++readCount;

            switch (field.id) {
            case Fid::AcPresent: {
                state_.acPresent = value != 0.0f;
                break;
            }

            case Fid::Discharging: {
                state_.onBattery = value != 0.0f;
                break;
            }

            case Fid::Charging: {
                state_.charging = value != 0.0f;
                break;
            }

            case Fid::Overload: {
                state_.overload = value != 0.0f;
                break;
            }

            case Fid::BadBattery: {
                state_.badBattery = value != 0.0f;
                break;
            }

            case Fid::ShutdownImminent: {
                state_.shutdownImminent = value != 0.0f;
                break;
            }

            case Fid::LowBattery: {
                state_.lowBattery = value != 0.0f;
                break;
            }

            case Fid::Load: {
                state_.loadPercent =
                    std::clamp(static_cast<int>(value + 0.5f), 0, 100);
                break;
            }

            case Fid::Charge: {
                state_.chargePercent = std::clamp(value, 0.0f, 100.0f);
                break;
            }

            case Fid::Battery: {
                // outside this window is sensor noise or disconnected pack
                if (value >= BATTERY_VOLT_MIN && value <= BATTERY_VOLT_MAX)
                    state_.batteryVoltage = value;
                break;
            }

            case Fid::Input: {
                // on battery there is no AC input
                // keeping last valid reading stops UI from flickering to 0
                if (!state_.onBattery
                    && value >= INPUT_VOLT_MIN
                    && value <= INPUT_VOLT_MAX)
                    state_.inputVoltage = value;
                break;
            }

            case Fid::Runtime: {
                if (ref.unit != Hid::Unit::Seconds)
                    break;

                // raw runtime is noisy around transitions
                // average last N readings so UI does not jump between polls
                const int seconds = static_cast<int>(value + 0.5f);
                if (seconds <= 0 || seconds > MAX_RUNTIME_SEC) {
                    state_.runtimeSeconds = 0;
                    break;
                }

                runtime_[runtimeIndex_] = seconds;
                runtimeIndex_ = (runtimeIndex_ + 1) % HISTORY_SIZE;
                runtimeCount_ = std::min(
                    runtimeCount_ + 1,
                    static_cast<int>(runtime_.size()));

                const int sum = std::accumulate(
                    runtime_.begin(), runtime_.begin() + runtimeCount_, 0);

                // subtract reserve so UI shows time user can still use
                // not raw value until UPS cuts power
                state_.runtimeSeconds = std::max(
                    0,
                    sum / runtimeCount_ - RUNTIME_RESERVE_SEC);
                break;
            }

            case Fid::Output: {
                // outside this window is sensor noise or disconnected load
                if (value >= OUTPUT_VOLT_MIN && value <= OUTPUT_VOLT_MAX) {
                    ext.outputVoltage = value;
                    ext.hasOutput = true;
                }
                break;
            }

            case Fid::Avr: {
                ext.avrRaw = static_cast<uint8_t>(value);
                ext.hasAvr = true;
                break;
            }

            case Fid::Sensitivity: {
                ext.sensitivity = static_cast<uint8_t>(value);
                ext.hasSensitivity = true;
                break;
            }

            case Fid::Beeper: {
                ext.beeperCode = static_cast<uint8_t>(value);
                ext.hasBeeper = true;
                break;
            }

            case Fid::LowTransfer: {
                ext.lowTransferVoltage = static_cast<uint16_t>(value + 0.5f);
                ext.hasLowTransfer = true;
                break;
            }

            case Fid::HighTransfer: {
                ext.highTransferVoltage = static_cast<uint16_t>(value + 0.5f);
                ext.hasHighTransfer = true;
                break;
            }

            case Fid::RatedPower: {
                // 0 means field is not populated on this UPS
                if (value > 0.0f) {
                    ext.ratedPower = value;
                    ext.hasRatedPower = true;
                }
                break;
            }

            case Fid::InputNominal: {
                // 0 means field is not populated on this UPS
                if (value > 0.0f) {
                    ext.inputNominalVoltage = value;
                    ext.hasInputNominal = true;
                }
                break;
            }

            case Fid::BatteryNominal: {
                // 0 means field is not populated on this UPS
                if (value > 0.0f) {
                    ext.batteryNominalVoltage = value;
                    ext.hasBatteryNominal = true;
                }
                break;
            }

            case Fid::UpsDate: {
                // unset or impossible date must not appear as manufacture date
                if (DecodeDate(static_cast<uint16_t>(value), ext.upsDate))
                    ext.hasUpsDate = true;
                break;
            }

            case Fid::BatteryDate: {
                // unset or impossible date must not appear as manufacture date
                if (DecodeDate(static_cast<uint16_t>(value), ext.batteryDate))
                    ext.hasBatteryDate = true;
                break;
            }

            case Fid::RuntimeLimit: {
                ext.runtimeLimitSeconds = static_cast<uint16_t>(value);
                ext.hasRuntimeLimit = true;
                break;
            }

            case Fid::TransferReason: {
                ext.transferReason = static_cast<uint8_t>(raw);
                ext.hasTransferReason = true;
                break;
            }
            }
        }

        return readCount;
    }

    // HIDAPI hides native HANDLE behind hid_device
    // HidD_GetPreparsedData needs real HANDLE so open same path again
    // ppd_ used later by HidP_GetUsageValue and HidP_SetUsageValue
    // ppd_ outlives temporary handle, Disconnect frees it
    bool UpsDevice::OpenReports(const char* path) {
        // UTF-8 path to UTF-16 for CreateFileW
        // first call returns size including null terminator
        const int size = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
        if (!size)
            return false;

        std::wstring widePath(size, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, path, -1, widePath.data(), size);

        // GENERIC_READ required by HidD_GetPreparsedData
        // share modes keep HIDAPI handle usable
        HANDLE handle = CreateFileW(widePath.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            return false;

        const bool gotData = HidD_GetPreparsedData(handle, &ppd_);
        CloseHandle(handle);

        // ReadCaps fills profile_ and buf_ from ppd_
        return gotData && ReadCaps();
    }

    // parse descriptor once
    // cache caps and buffer for ReadFields and SetValue
    bool UpsDevice::ReadCaps() {
        HIDP_CAPS caps{};
        if (HidP_GetCaps(ppd_, &caps) != HIDP_STATUS_SUCCESS)
            return false;

        // HidP_GetLinkCollectionNodes needs ULONG*, caps field USHORT
        ULONG nodeCount = caps.NumberLinkCollectionNodes;
        std::vector<HIDP_LINK_COLLECTION_NODE> nodes(nodeCount);
        if (HidP_GetLinkCollectionNodes(nodes.data(), &nodeCount, ppd_) != HIDP_STATUS_SUCCESS)
            return false;

        // APC exposes readings as feature reports
        // other report types unused
        USHORT valueCount = caps.NumberFeatureValueCaps;
        std::vector<HIDP_VALUE_CAPS> values(valueCount);
        if (HidP_GetValueCaps(HidP_Feature, values.data(), &valueCount, ppd_) != HIDP_STATUS_SUCCESS)
            return false;

        for (USHORT i = 0; i < valueCount; ++i)
            MapValue(values[i], nodes);

        // buffer reused by reads and writes
        // size must match feature report
        buf_.resize(caps.FeatureReportByteLength);

        LOG_INFO("HID Caps: UsagePage=0x%04X FeatureLen=%u",
            caps.UsagePage, caps.FeatureReportByteLength);

        return true;
    }

    // same usage can describe different parts of UPS
    // duplicate collections must not replace live source already selected
    void UpsDevice::MapValue(
        const HIDP_VALUE_CAPS& cap,
        const std::vector<HIDP_LINK_COLLECTION_NODE>& nodes) {
        if (cap.IsRange)
            return;

        const uint32_t usage = (uint32_t(cap.UsagePage) << 16) | cap.NotRange.Usage;

        // usages repeat across collections
        // walk parents to keep battery and summary separate from whole-device values
        uint16_t collection = Hid::Collection::Any;

        for (USHORT link = cap.LinkCollection; ; link = nodes[link].Parent) {
            const auto& node = nodes[link];
            if (node.LinkUsagePage == Hid::POWER_PAGE) {
                switch (node.LinkUsage) {
                case Hid::Collection::Input:
                case Hid::Collection::Output:
                case Hid::Collection::Battery:
                case Hid::Collection::Summary:
                case Hid::Collection::Ups:
                    collection = node.LinkUsage;
                    break;
                }
                if (collection != Hid::Collection::Any)
                    break;
            }
            if (!link)
                break;
        }

        const auto it = std::find_if(kFields.begin(), kFields.end(),
            [usage, collection](const FieldDef& field) {
                return field.usage == usage
                    && (field.collection == Hid::Collection::Any || field.collection == collection);
            });

        if (it == kFields.end())
            return;

        // keep first match so live source does not flip on reparse
        ValueRef& ref = profile_[it->id];
        if ((it->flags & FirstOnly) && ref.reportId)
            return;

        // convert HID exponent so live and nominal readings use volts and watts
        int exp = cap.UnitsExp & 0xF;

        if (exp > 7)
            exp -= 16;

        if (cap.Units == Hid::Unit::Volt || cap.Units == Hid::Unit::Watt)
            exp -= 7;

        ref = {
            cap.ReportID, cap.UsagePage, cap.NotRange.Usage, cap.LinkCollection,
            cap.Units, std::pow(10.0f, float(exp)), cap.LogicalMin, cap.LogicalMax
        };
    }

} // namespace Ups
