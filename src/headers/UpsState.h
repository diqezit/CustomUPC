#pragma once

#include <cstdint>
#include <string>

namespace Ups {

    enum BeeperCode : uint8_t {
        BEEPER_DISABLED = 0x01,
        BEEPER_ENABLED = 0x02,
        BEEPER_MUTED = 0x03
    };

    enum SensitivityCode : uint8_t {
        SENSITIVITY_LOW = 0x00,
        SENSITIVITY_MEDIUM = 0x01,
        SENSITIVITY_HIGH = 0x02
    };

    struct Date {
        int year = 0;
        int month = 0;
        int day = 0;
    };

    struct Extended {
        float outputVoltage = 0;
        float ratedPower = 0;
        float inputNominalVoltage = 0;
        float batteryNominalVoltage = 0;

        Date upsDate;
        Date batteryDate;
        uint16_t runtimeLimitSeconds = 0;

        uint8_t sensitivity = 0;
        uint8_t beeperCode = 0;
        uint8_t avrRaw = 0;
        uint8_t transferReason = 0;

        uint16_t lowTransferVoltage = 0;
        uint16_t highTransferVoltage = 0;

        bool hasOutput = false;
        bool hasSensitivity = false;
        bool hasBeeper = false;
        bool hasAvr = false;
        bool hasLowTransfer = false;
        bool hasHighTransfer = false;
        bool hasRatedPower = false;
        bool hasInputNominal = false;
        bool hasBatteryNominal = false;
        bool hasUpsDate = false;
        bool hasBatteryDate = false;
        bool hasRuntimeLimit = false;
        bool hasTransferReason = false;
    };

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
        bool shutdownImminent = false;
        bool lowBattery = false;

        std::string manufacturer;
        std::string product;
        std::string serial;

        Extended ext;
    };

} // namespace Ups
