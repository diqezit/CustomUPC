#pragma once

#include "core/UpsState.h"
#include "core/UpsMonitor.h"
#include "utils/Logger.h"

#include <hidapi.h>
#include <windows.h>
#include <hidsdi.h>
#include <hidpi.h>

#include <vector>
#include <array>
#include <string>
#include <algorithm>

#pragma comment(lib, "hid.lib")

namespace Ups {

    // ============================================================================
    //                          HID REPORT CONFIGURATION
    // ============================================================================

    namespace HidReportConfig {
        inline constexpr size_t MIN_FEATURE_LEN = 2;
        inline constexpr size_t DEFAULT_FEATURE_LEN = 64;
        inline constexpr size_t DEFAULT_INPUT_LEN = 5;
        inline constexpr int    MAX_COLLECTION_DEPTH = 32;
    }

    // ============================================================================
    //                           PREPARSED DATA RAII
    // ============================================================================

    class PreparsedDataPtr {
        PHIDP_PREPARSED_DATA data_ = nullptr;

    public:
        PreparsedDataPtr() = default;

        explicit PreparsedDataPtr(HANDLE h) {
            if (h && h != INVALID_HANDLE_VALUE) {
                if (!HidD_GetPreparsedData(h, &data_)) {
                    data_ = nullptr;
                }
            }
        }

        ~PreparsedDataPtr() { Reset(); }

        PreparsedDataPtr(PreparsedDataPtr&& o) noexcept
            : data_(o.data_)
        {
            o.data_ = nullptr;
        }

        PreparsedDataPtr& operator=(PreparsedDataPtr&& o) noexcept {
            if (this != &o) {
                Reset();
                data_ = o.data_;
                o.data_ = nullptr;
            }
            return *this;
        }

        PreparsedDataPtr(const PreparsedDataPtr&) = delete;
        PreparsedDataPtr& operator=(const PreparsedDataPtr&) = delete;

        void Reset() noexcept {
            if (data_) {
                HidD_FreePreparsedData(data_);
                data_ = nullptr;
            }
        }

        [[nodiscard]] PHIDP_PREPARSED_DATA Get() const noexcept {
            return data_;
        }

        [[nodiscard]] explicit operator bool() const noexcept {
            return data_ != nullptr;
        }
    };

    // ============================================================================
    //                              MODEL PROFILE
    // ============================================================================

    enum ReportIndex : size_t {
        IDX_STATUS,
        IDX_TELEMETRY,
        IDX_BATT_V,
        IDX_INPUT_V,
        IDX_CHARGE,
        IDX_RUNTIME,
        IDX_OUT_VOLT,
        IDX_FREQUENCY,
        IDX_AVR,
        IDX_LOW_BATT,
        IDX_SENSITIVITY,
        IDX_BEEPER,
        IDX_SELF_TEST,
        REPORT_COUNT
    };

    namespace HidCollection {
        inline constexpr uint16_t BATTERY = 0x12;
        inline constexpr uint16_t INPUT = 0x1A;
        inline constexpr uint16_t OUTPUT = 0x1C;
    }

    namespace HidExtra {
        inline constexpr uint16_t PERCENT_LOAD = 0x35;      // 0x84:0x35
        inline constexpr uint16_t RUNTIME_TO_EMPTY = 0x68;  // 0x85:0x68
        inline constexpr uint32_t UNIT_SECONDS = 0x1001;    // HID unit s
    }

    // Addresses HID value for fields whose bit offset cannot be guessed
    struct ValueRef {
        uint8_t  reportId = 0;
        uint16_t page = 0;
        uint16_t usage = 0;
        uint16_t link = 0;
        uint32_t unit = 0;
        float    scale = 1.0f;

        [[nodiscard]] bool IsValid() const noexcept { return reportId != 0; }
    };

    struct ModelProfile {
        std::array<uint8_t, REPORT_COUNT> discoveredIds{};
        HidValueInfo input, battery, charge, inputFreq, outputFreq;
        ValueRef vInput, vOutput, vBattery, vCharge, vLoad, vRuntime;
        size_t featureLen = HidReportConfig::DEFAULT_FEATURE_LEN;
        size_t inputLen = HidReportConfig::DEFAULT_INPUT_LEN;

        static constexpr std::array<uint8_t, REPORT_COUNT> FALLBACKS = {
            HidUsage::RID_STATUS,
            HidUsage::RID_TELEMETRY,
            HidUsage::RID_VOLTAGE,
            0,
            HidUsage::RID_CHARGE,
            HidUsage::RID_RUNTIME,
            HidUsage::RID_OUT_VOLT,
            HidUsage::RID_FREQUENCY,
            0, 0, 0,
            HidUsage::RID_BEEPER,
            HidUsage::RID_SELFTEST
        };

        [[nodiscard]] uint8_t GetRID(ReportIndex i) const noexcept {
            return discoveredIds[i] ? discoveredIds[i] : FALLBACKS[i];
        }
    };

    // ============================================================================
    //                          CAPABILITIES PARSER
    // ============================================================================

    class CapabilitiesParser {
    public:
        static bool Parse(
            const std::string& path,
            ModelProfile& p,
            PreparsedDataPtr& ppdOut)
        {
            const std::wstring wpath(path.begin(), path.end());

            HANDLE h = CreateFileW(
                wpath.c_str(),
                0,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr,
                OPEN_EXISTING,
                0,
                nullptr
            );

            if (h == INVALID_HANDLE_VALUE) return false;

            PreparsedDataPtr ppd(h);
            CloseHandle(h);

            if (!ppd) return false;

            HIDP_CAPS caps{};
            if (HidP_GetCaps(ppd.Get(), &caps) != HIDP_STATUS_SUCCESS) {
                return false;
            }

            p.featureLen = caps.FeatureReportByteLength;
            p.inputLen = caps.InputReportByteLength;

            LOG_INFO(
                "HID Caps: UsagePage=0x" + DataUtils::Hex16(caps.UsagePage) +
                " FeatureLen=" + std::to_string(p.featureLen)
            );

            ParseValueCaps(
                ppd.Get(),
                caps.NumberFeatureValueCaps,
                GetCollectionNodes(ppd.Get(), caps),
                p
            );

            // Descriptor outlives parsing because every poll calls HidP_GetUsageValue
            ppdOut = std::move(ppd);
            return true;
        }

    private:
        using NodeList = std::vector<HIDP_LINK_COLLECTION_NODE>;

        struct SimpleUsageEntry {
            uint16_t    page;
            uint16_t    usage;
            ReportIndex idx;
        };

        static constexpr std::array<SimpleUsageEntry, 4> SIMPLE_USAGES = { {
            { HidUsage::PAGE_POWER_DEVICE,   HidUsage::AUDIBLE_ALARM,  IDX_BEEPER      },
            { HidUsage::PAGE_BATTERY_SYSTEM, HidUsage::CAPACITY_LIMIT, IDX_LOW_BATT    },
            { HidUsage::PAGE_APC_VENDOR,     HidUsage::AVR_STATUS,     IDX_AVR         },
            { HidUsage::PAGE_APC_VENDOR,     HidUsage::SENSITIVITY,    IDX_SENSITIVITY },
        } };

        static void MarkDiscovered(ModelProfile& p, ReportIndex idx, uint8_t rid) noexcept {
            p.discoveredIds[idx] = rid;
        }

        static bool TryMarkSimpleUsage(
            uint16_t page,
            uint16_t usage,
            uint8_t reportId,
            ModelProfile& p)
        {
            for (const auto& e : SIMPLE_USAGES) {
                if (e.page == page && e.usage == usage) {
                    MarkDiscovered(p, e.idx, reportId);
                    return true;
                }
            }
            return false;
        }

        static void Bind(
            const HIDP_VALUE_CAPS& v,
            ReportIndex idx,
            ModelProfile& p,
            ValueRef* ref,
            HidValueInfo* info = nullptr)
        {
            const uint8_t  reportId = v.ReportID;
            const uint16_t usage = v.NotRange.Usage;
            const uint32_t units = v.Units;

            const HidValueInfo vi{
                reportId,
                usage,
                v.BitSize,
                units,
                v.UnitsExp,
                v.LogicalMax
            };

            if (ref) {
                *ref = ValueRef{
                    reportId,
                    v.UsagePage,
                    usage,
                    v.LinkCollection,
                    units,
                    ComputeScale(vi)
                };
            }

            if (info) *info = vi;

            MarkDiscovered(p, idx, reportId);
        }

        static void BindInfoIfEmpty(
            HidValueInfo& target,
            const HIDP_VALUE_CAPS& v,
            ReportIndex idx,
            ModelProfile& p)
        {
            if (!target.IsValid()) Bind(v, idx, p, nullptr, &target);
        }

        static NodeList GetCollectionNodes(
            PHIDP_PREPARSED_DATA ppd,
            const HIDP_CAPS& caps)
        {
            NodeList nodes;
            if (caps.NumberLinkCollectionNodes == 0) return nodes;

            nodes.resize(caps.NumberLinkCollectionNodes);
            ULONG n = caps.NumberLinkCollectionNodes;

            if (HidP_GetLinkCollectionNodes(nodes.data(), &n, ppd)
                != HIDP_STATUS_SUCCESS)
            {
                nodes.clear();
            }

            return nodes;
        }

        // Values usually sit in a nested collection
        // so walk up until one of the three power collections is reached
        static uint16_t ResolveCollection(const NodeList& nodes, USHORT link) {
            size_t idx = link;

            for (int guard = 0;
                guard < HidReportConfig::MAX_COLLECTION_DEPTH && idx < nodes.size();
                ++guard)
            {
                const auto& n = nodes[idx];

                if (n.LinkUsagePage == HidUsage::PAGE_POWER_DEVICE &&
                    (n.LinkUsage == HidCollection::INPUT ||
                        n.LinkUsage == HidCollection::OUTPUT ||
                        n.LinkUsage == HidCollection::BATTERY))
                {
                    return n.LinkUsage;
                }

                if (n.Parent == 0) break;
                idx = n.Parent;
            }

            return 0;
        }

        static void ParseValueCaps(
            PHIDP_PREPARSED_DATA ppd,
            USHORT count,
            const NodeList& nodes,
            ModelProfile& p)
        {
            if (count == 0) return;

            std::vector<HIDP_VALUE_CAPS> vc(count);
            USHORT got = count;

            if (HidP_GetValueCaps(HidP_Feature, vc.data(), &got, ppd)
                != HIDP_STATUS_SUCCESS)
            {
                return;
            }

            for (USHORT i = 0; i < got; ++i) {
                // Ranged caps cover arrays of usages and carry no single metric
                if (!vc[i].IsRange) ProcessCap(vc[i], nodes, p);
            }
        }

        static void ProcessCap(
            const HIDP_VALUE_CAPS& v,
            const NodeList& nodes,
            ModelProfile& p)
        {
            const uint16_t usage = v.NotRange.Usage;

            if (TryMarkSimpleUsage(v.UsagePage, usage, v.ReportID, p)) return;

            switch (v.UsagePage) {
            case HidUsage::PAGE_POWER_DEVICE:
                switch (usage) {
                case HidUsage::VOLTAGE:
                    BindVoltage(v, nodes, p);
                    break;

                case HidExtra::PERCENT_LOAD:
                    Bind(v, IDX_TELEMETRY, p, &p.vLoad);
                    break;

                case HidUsage::CONFIG_VOLTAGE:

                    // ConfigVoltage normally holds nominal mains voltage
                    if (!p.vBattery.IsValid() && v.LogicalMax <= 20) {
                        Bind(v, IDX_BATT_V, p, nullptr, &p.battery);
                    }
                    break;

                case HidUsage::FREQ_INPUT:
                    Bind(v, IDX_FREQUENCY, p, nullptr, &p.inputFreq);
                    break;

                case HidUsage::FREQ_OUTPUT:
                case HidUsage::FREQUENCY:
                    BindInfoIfEmpty(p.outputFreq, v, IDX_FREQUENCY, p);
                    break;
                }
                break;

            case HidUsage::PAGE_BATTERY_SYSTEM:
                switch (usage) {
                case HidUsage::REMAINING_CAPACITY:
                    Bind(v, IDX_CHARGE, p, &p.vCharge, &p.charge);
                    break;

                case HidExtra::RUNTIME_TO_EMPTY:
                    Bind(v, IDX_RUNTIME, p, &p.vRuntime);
                    break;
                }
                break;
            }
        }

        static void BindVoltage(
            const HIDP_VALUE_CAPS& v,
            const NodeList& nodes,
            ModelProfile& p)
        {
            switch (ResolveCollection(nodes, v.LinkCollection)) {
            case HidCollection::OUTPUT:
                Bind(v, IDX_OUT_VOLT, p, &p.vOutput);
                return;

            case HidCollection::BATTERY:
                Bind(v, IDX_BATT_V, p, &p.vBattery, &p.battery);
                return;

            case HidCollection::INPUT:
                Bind(v, IDX_INPUT_V, p, &p.vInput, &p.input);
                return;

            default:
                if (!p.vInput.IsValid()) {
                    Bind(v, IDX_INPUT_V, p, &p.vInput, &p.input);
                }
                return;
            }
        }
    };

    // ============================================================================
    //                            HID REPORT READER
    // ============================================================================

    class HidReportReader {
    public:

        // Fallback report IDs still work on firmware with an unreadable descriptor
        // 
        // so a parse failure leaves the reader usable
        //
        bool Open(hid_device* dev, const std::string& path) {
            Close();
            dev_ = dev;
            if (!dev_) return false;

            const bool parsed = CapabilitiesParser::Parse(path, profile_, ppd_);
            if (!parsed) {
                LOG_ERROR("HID caps parse failed, using fallback report IDs");
                profile_ = ModelProfile{};
                ppd_.Reset();
            }

            // Buffer length must match FeatureReportByteLength because hidapi and
            // 
            // the HID parser both reject shorter reports
            //
            buf_.assign(
                std::max(profile_.featureLen, HidReportConfig::MIN_FEATURE_LEN),
                0
            );

            return parsed;
        }

        void Close() noexcept {
            dev_ = nullptr;
            ppd_.Reset();
            profile_ = ModelProfile{};
            buf_.clear();
        }

        [[nodiscard]] const ModelProfile& Profile() const noexcept { return profile_; }
        [[nodiscard]] uint8_t Rid(ReportIndex i) const noexcept { return profile_.GetRID(i); }
        [[nodiscard]] size_t InputLen() const noexcept { return profile_.inputLen; }

        [[nodiscard]] const uint8_t* Data() const noexcept { return buf_.data(); }
        [[nodiscard]] size_t Size() const noexcept { return buf_.size(); }

        bool ReadFeature(uint8_t rid) {
            if (!dev_ || !rid || buf_.size() < HidReportConfig::MIN_FEATURE_LEN) {
                return false;
            }

            std::fill(buf_.begin(), buf_.end(), 0);
            buf_[0] = rid;

            return hid_get_feature_report(dev_, buf_.data(), buf_.size()) > 0;
        }

        // Let the Windows HID parser locate the field
        // 
        // 
        // Hardcoded byte offsets land on neighbouring bits 
        // in packed reports and yield 0.0 V readings
        //

        bool ReadValue(const ValueRef& r, float& out) {
            if (!r.IsValid() || !ppd_ || !ReadFeature(r.reportId)) return false;

            ULONG raw = 0;
            const NTSTATUS st = HidP_GetUsageValue(
                HidP_Feature,
                r.page,
                r.link,
                r.usage,
                &raw,
                ppd_.Get(),
                reinterpret_cast<PCHAR>(buf_.data()),
                static_cast<ULONG>(buf_.size())
            );

            if (st != HIDP_STATUS_SUCCESS) return false;

            out = static_cast<float>(raw) * r.scale;
            return true;
        }

        // parse first and fixed-offset decoder second
        // 
        // devices without preparsed data keep reporting
        //
        template <class LegacyDecode>
        bool ReadMetric(
            const ValueRef& ref,
            ReportIndex idx,
            float& out,
            LegacyDecode decode)
        {
            if (ReadValue(ref, out)) return true;
            if (!ReadFeature(Rid(idx))) return false;

            out = decode(buf_.data());
            return true;
        }

        bool ReadByte(ReportIndex idx, uint8_t& out) {
            if (!ReadFeature(Rid(idx))) return false;

            out = buf_[1];
            return true;
        }

        bool WriteByte(ReportIndex idx, uint8_t value) {
            if (!ReadFeature(Rid(idx))) return false;

            buf_[1] = value;
            return hid_send_feature_report(dev_, buf_.data(), buf_.size()) >= 0;
        }

        bool ReadFrequency(const HidValueInfo& info, float& out) {
            if (!info.IsValid() || !ReadFeature(info.reportId)) return false;

            out = FrequencyDecoder::Decode(buf_.data(), info.Is16bit());
            return Validators::IsValidFrequency(out);
        }

        int ReadInput(uint8_t* data, size_t len, int timeoutMs) {
            if (!dev_) return -1;

            return hid_read_timeout(dev_, data, len, timeoutMs);
        }

    private:
        hid_device* dev_ = nullptr;
        PreparsedDataPtr     ppd_;
        ModelProfile         profile_;
        std::vector<uint8_t> buf_;
    };

} // namespace Ups