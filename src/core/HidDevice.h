#pragma once

#include "core/UpsState.h"
#include "core/UpsMonitor.h"
#include "utils/Logger.h"

#include <hidapi.h>
#include <windows.h>
#include <hidsdi.h>
#include <hidpi.h>

#include <vector>
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

    // Addresses a HID field by the descriptor data, never by a byte offset.
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
        ValueRef vInput, vOutput, vBattery, vCharge, vLoad, vRuntime;
        ValueRef vCharging, vDischarging, vAcPresent, vAvr;
        ValueRef vOverload, vBadBattery;
        ValueRef vBeeper, vSensitivity;

        // These are writable settings, not live measurements
        ValueRef vLowTransfer, vHighTransfer;

        size_t featureLen = HidReportConfig::DEFAULT_FEATURE_LEN;
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

        static void Bind(const HIDP_VALUE_CAPS& v, ValueRef* ref) {
            if (!ref) return;

            *ref = ValueRef{
                v.ReportID,
                v.UsagePage,
                v.NotRange.Usage,
                v.LinkCollection,
                v.Units,
                ComputeScale(v.Units, v.UnitsExp)
            };
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

            if (v.UsagePage == HidUsage::PAGE_APC_VENDOR) {
                if (usage == HidUsage::AVR_STATUS) {
                    Bind(v, &p.vAvr);
                }
                else if (usage == HidUsage::SENSITIVITY) {
                    Bind(v, &p.vSensitivity);
                }
                return;
            }

            switch (v.UsagePage) {
            case HidUsage::PAGE_POWER_DEVICE:
                switch (usage) {
                case HidUsage::VOLTAGE:
                    BindVoltage(v, nodes, p);
                    break;

                case HidExtra::PERCENT_LOAD:
                    Bind(v, &p.vLoad);
                    break;

                case HidUsage::AUDIBLE_ALARM:
                    Bind(v, &p.vBeeper);
                    break;

                case HidUsage::LOW_VOLTAGE_TRANSFER:
                    Bind(v, &p.vLowTransfer);
                    break;

                case HidUsage::HIGH_VOLTAGE_TRANSFER:
                    Bind(v, &p.vHighTransfer);
                    break;

                case HidUsage::OVERLOAD:
                    Bind(v, &p.vOverload);
                    break;
                }
                break;

            case HidUsage::PAGE_BATTERY_SYSTEM:
                switch (usage) {
                case HidUsage::REMAINING_CAPACITY:
                    Bind(v, &p.vCharge);
                    break;

                case HidExtra::RUNTIME_TO_EMPTY:
                    Bind(v, &p.vRuntime);
                    break;

                case HidUsage::CHARGING:
                    if (!p.vCharging.IsValid()) Bind(v, &p.vCharging);
                    break;

                case HidUsage::DISCHARGING:
                    if (!p.vDischarging.IsValid()) Bind(v, &p.vDischarging);
                    break;

                case HidUsage::AC_PRESENT:
                    if (!p.vAcPresent.IsValid()) Bind(v, &p.vAcPresent);
                    break;

                case HidUsage::NEED_REPLACEMENT:
                    Bind(v, &p.vBadBattery);
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
                Bind(v, &p.vOutput);
                return;

            case HidCollection::BATTERY:
                Bind(v, &p.vBattery);
                return;

            case HidCollection::INPUT:
                Bind(v, &p.vInput);
                return;

            default:
                return;
            }
        }
    };

    // ============================================================================
    //                            HID REPORT READER
    // ============================================================================

    class HidReportReader {
    public:
        bool Open(hid_device* dev, const std::string& path) {
            Close();
            dev_ = dev;
            if (!dev_) return false;

            if (!CapabilitiesParser::Parse(path, profile_, ppd_)) {
                LOG_ERROR("HID caps parse failed");
                Close();
                return false;
            }

            // Buffer length must match FeatureReportByteLength because hidapi and
            // the HID parser both reject shorter reports
            buf_.assign(
                std::max(profile_.featureLen, HidReportConfig::MIN_FEATURE_LEN),
                0
            );

            return true;
        }

        void Close() noexcept {
            dev_ = nullptr;
            ppd_.Reset();
            profile_ = ModelProfile{};
            buf_.clear();
        }

        [[nodiscard]] const ModelProfile& Profile() const noexcept { return profile_; }

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

        bool WriteValue(const ValueRef& r, uint32_t raw) {
            if (!r.IsValid() || !ppd_ || !ReadFeature(r.reportId)) return false;

            const NTSTATUS st = HidP_SetUsageValue(
                HidP_Feature,
                r.page,
                r.link,
                r.usage,
                raw,
                ppd_.Get(),
                reinterpret_cast<PCHAR>(buf_.data()),
                static_cast<ULONG>(buf_.size())
            );

            if (st != HIDP_STATUS_SUCCESS) return false;

            return hid_send_feature_report(dev_, buf_.data(), buf_.size()) >= 0;
        }

    private:
        bool ReadFeature(uint8_t rid) {
            if (!dev_ || !rid || buf_.size() < HidReportConfig::MIN_FEATURE_LEN) {
                return false;
            }

            std::fill(buf_.begin(), buf_.end(), 0);
            buf_[0] = rid;

            return hid_get_feature_report(dev_, buf_.data(), buf_.size()) > 0;
        }

        hid_device* dev_ = nullptr;
        PreparsedDataPtr     ppd_;
        ModelProfile         profile_;
        std::vector<uint8_t> buf_;
    };

} // namespace Ups
