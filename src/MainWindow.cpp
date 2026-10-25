#include "MainWindow.h"
#include "UpsDevice.h"

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <format>

namespace Ups {
namespace {

    constexpr float CLOSE_SIZE = 20.0f;
    constexpr float LABEL_W    = 128.0f;
    constexpr float BAR_HEIGHT = 14.0f;
    constexpr float BUTTON_H   = 20.0f;
    constexpr uint64_t POLL_MS = 1000;

    constexpr ImVec4 BG_WIN      = { 0.13f, 0.13f, 0.15f, 1.0f };
    constexpr ImVec4 BG_BAR      = { 0.22f, 0.22f, 0.25f, 1.0f };
    constexpr ImVec4 TEXT_MAIN   = { 0.92f, 0.92f, 0.94f, 1.0f };
    constexpr ImVec4 TEXT_SUB    = { 0.70f, 0.70f, 0.75f, 1.0f };
    constexpr ImVec4 TEXT_DIM    = { 0.50f, 0.50f, 0.55f, 1.0f };
    constexpr ImVec4 CLOSE_HOVER = { 0.80f, 0.30f, 0.30f, 1.0f };
    constexpr ImVec4 GREEN       = { 0.26f, 0.75f, 0.50f, 1.0f };
    constexpr ImVec4 YELLOW      = { 0.95f, 0.70f, 0.20f, 1.0f };
    constexpr ImVec4 RED         = { 0.85f, 0.35f, 0.35f, 1.0f };
    constexpr ImVec4 BLUE        = { 0.30f, 0.65f, 0.95f, 1.0f };

    enum ItemId : int {
        MetricBattery,
        MetricInput,
        MetricRuntime,
        MetricOutput,
        MetricSensitivity,
        MetricBeeper,
        BarCharge,
        BarLoad,
        DetailRatedPower,
        DetailInputNominal,
        DetailBatteryNominal,
        DetailUpsDate,
        DetailBatteryDate,
        DetailRuntimeLimit,
        DetailTransferReason,
        InfoTitle,
        InfoProduct,
        InfoSerial,
        ItemCount
    };

    constexpr auto kBarGap = std::to_array<float>({ 12.0f, 2.0f });
    static_assert(kBarGap.size() == DetailRatedPower - BarCharge);

    enum PageId : int {
        PageMonitor,
        PageDetails,
        PageConfig
    };

    enum AvrFlag : uint8_t {
        AvrBoost  = 0x04,
        AvrNormal = 0x08,
        AvrTrim   = 0x10
    };

    constexpr float BATTERY_CRITICAL_V = 10.60f;
    constexpr float BATTERY_WARNING_V  = 11.50f;
    constexpr float INPUT_LOW_V        = 200.0f;
    constexpr float INPUT_HIGH_V       = 250.0f;
    constexpr float RUNTIME_CRITICAL_S = 300.0f;
    constexpr float RUNTIME_WARNING_S  = 900.0f;
    constexpr float CHARGE_CRITICAL    = 20.0f;
    constexpr float CHARGE_WARNING     = 50.0f;
    constexpr float LOAD_WARNING       = 50.0f;
    constexpr float LOAD_CRITICAL      = 80.0f;

    struct Status {
        const char* text;
        ImVec4 color = RED;
    };

    constexpr auto kStatuses = std::to_array<Status>({
        { "DISCONNECTED" },
        { "UPS SHUTDOWN SOON" },
        { "REPLACE BATTERY" },
        { "OVERLOAD" },
        { "LOW BATTERY" },
        { "BATTERY MODE", YELLOW },
        { "CHARGING", BLUE },
        { "AVR BOOST", GREEN },
        { "AVR TRIM", GREEN },
        { "ONLINE", GREEN }
    });

    struct Item {
        const char* label;
        std::string text;
        ImVec4 color;
        bool visible;
        float value = 0.0f;
    };

    struct View {
        Item items[ItemCount]{};
        Status status = kStatuses[0];
        Extended ext{};
    };

    struct Config {
        int beeper = 0;
        int sens = 0;
        int low = 0;
        int high = 0;
        std::string lowItems;
        std::string highItems;
    };

    UpsDevice device;
    View view;
    Config config;
    PageId page = PageMonitor;
    uint64_t lastPoll = 0;

    constexpr auto kBeeperText = std::to_array<const char*>({ "Unknown", "Off", "On", "Muted" });
    constexpr auto kSensitivityText = std::to_array<const char*>({ "Low", "Medium", "High" });
    constexpr auto kTransferReasonText = std::to_array<const char*>({
        "No transfers", "Low voltage", "High voltage", "Ripple",
        "Notch/spike/blackout", "Test/calibration", "Forced shutdown",
        "Frequency out of range", "Notch/blackout", "Spike/blackout",
        "Accessory shutdown", "Command self-test", "Button self-test", "Scheduled self-test"
    });

    constexpr const char* kItemLabel[ItemCount] = {
        "Battery Voltage",
        "Input Voltage",
        "Est. Runtime",
        "Output Voltage",
        "Sensitivity",
        "Beeper",
        "Charge",
        "Load",
        "Rated power",
        "Nominal input",
        "Nominal battery",
        "UPS made",
        "Battery made",
        "Runtime limit",
        "Last transfer",
        nullptr,
        nullptr,
        nullptr
    };

    ImVec4 LowColor(float value, float critical, float warning) {
        if (value < critical)
            return RED;
        if (value < warning)
            return YELLOW;
        return GREEN;
    }

    std::string VoltageText(float volts) {
        return std::format("{:.1f} V", volts);
    }

    std::string DateText(const Date& date) {
        return std::format(
            "{:04d}-{:02d}-{:02d}",
            date.year,
            date.month,
            date.day);
    }

    // polling is slower than rendering
    // format values once per poll so frame loop does no string allocs
    // visible=false when hardware does not expose the field
    void UpdateView(const UpsState& state) {
        const Extended& ext = state.ext;

        for (int i = 0; i < ItemCount; ++i) {
            Item& item = view.items[i];
            item = { kItemLabel[i], {}, TEXT_MAIN, true };

            switch (static_cast<ItemId>(i)) {
            case MetricBattery:

                if (state.batteryVoltage <= 0.0f) {
                    item.text = "--";
                    item.color = TEXT_DIM;
                    break;
                }

                item.text = VoltageText(state.batteryVoltage);
                item.color = LowColor(
                    state.batteryVoltage, BATTERY_CRITICAL_V, BATTERY_WARNING_V);
                break;

            case MetricInput:
                if (state.onBattery || state.inputVoltage <= 0.0f) {
                    item.text = "--";
                    item.color = TEXT_DIM;
                    break;
                }

                item.text = VoltageText(state.inputVoltage);
                item.color = GREEN;

                if (state.inputVoltage < INPUT_LOW_V
                    || state.inputVoltage > INPUT_HIGH_V)
                    item.color = YELLOW;
                break;

            case MetricRuntime: {
                const int seconds = state.runtimeSeconds;

                if (seconds <= 0)
                    item.text = "--:--";

                else if (seconds >= 3600)
                    item.text = std::format("{}h {:02d}m", seconds / 3600, seconds % 3600 / 60);
                else
                    item.text = std::format("{}m {:02d}s", seconds / 60, seconds % 60);

                item.color = LowColor(
                    static_cast<float>(seconds), RUNTIME_CRITICAL_S, RUNTIME_WARNING_S);
                break;
            }

            case MetricOutput:
                item.text = VoltageText(ext.outputVoltage);
                item.visible = ext.hasOutput;
                break;

            case MetricSensitivity:
                item.text = "Unknown";

                if (ext.sensitivity < kSensitivityText.size())
                    item.text = kSensitivityText[ext.sensitivity];

                item.color = TEXT_SUB;
                item.visible = ext.hasSensitivity;
                break;

            case MetricBeeper:
                item.text = "Unknown";

                if (ext.beeperCode < kBeeperText.size())
                    item.text = kBeeperText[ext.beeperCode];

                item.color = TEXT_SUB;
                item.visible = ext.hasBeeper;
                break;

            case BarCharge:
                item.value = state.chargePercent / 100.0f;
                item.color = LowColor(state.chargePercent, CHARGE_CRITICAL, CHARGE_WARNING);
                item.text = std::format("{:.0f}%", state.chargePercent);
                break;

            case BarLoad:
                item.value = static_cast<float>(state.loadPercent) / 100.0f;
                item.color = GREEN;

                if (state.loadPercent > LOAD_CRITICAL)
                    item.color = RED;

                else if (state.loadPercent > LOAD_WARNING)
                    item.color = YELLOW;

                item.text = std::format("{}%", state.loadPercent);
                break;

            case DetailRatedPower:
                item.text = std::format("{:.0f} W", ext.ratedPower);
                item.visible = ext.hasRatedPower;
                break;

            case DetailInputNominal:
                item.text = VoltageText(ext.inputNominalVoltage);
                item.visible = ext.hasInputNominal;
                break;

            case DetailBatteryNominal:
                item.text = VoltageText(ext.batteryNominalVoltage);
                item.visible = ext.hasBatteryNominal;
                break;

            case DetailUpsDate:
                item.text = DateText(ext.upsDate);
                item.color = TEXT_SUB;
                item.visible = ext.hasUpsDate;
                break;

            case DetailBatteryDate:
                item.text = DateText(ext.batteryDate);
                item.color = TEXT_SUB;
                item.visible = ext.hasBatteryDate;
                break;

            case DetailRuntimeLimit:
                item.text = std::format("{:g} min", ext.runtimeLimitSeconds / 60.0f);
                item.color = TEXT_SUB;
                item.visible = ext.hasRuntimeLimit;
                break;

            case DetailTransferReason:
                if (ext.transferReason < kTransferReasonText.size())
                    item.text = kTransferReasonText[ext.transferReason];
                else
                    item.text = std::format("Unknown ({})", ext.transferReason);
                item.color = TEXT_SUB;
                item.visible = ext.hasTransferReason;
                break;

            case InfoTitle:
                if (!state.manufacturer.empty()) {
                    item.text = state.manufacturer;
                    break;
                }
                if (!state.product.empty()) {
                    item.text = state.product;
                    break;
                }
                item.text = "UPS Device";
                break;

            case InfoProduct:
                item.color = TEXT_SUB;
                if (state.manufacturer.empty()
                    || state.product == state.manufacturer)
                    break;

                item.text = state.product;
                break;

            case InfoSerial:
                item.text = state.serial;
                item.color = TEXT_DIM;
                break;
            }
        }

        // first active status wins
        const bool avr = ext.hasAvr && !(ext.avrRaw & AvrNormal);
        const auto active = std::to_array({
            !state.connected, state.shutdownImminent, state.badBattery, state.overload,
            state.lowBattery, state.onBattery, state.charging,
            avr && (ext.avrRaw & AvrBoost), avr && (ext.avrRaw & AvrTrim), true
        });
        static_assert(active.size() == kStatuses.size());
        view.status = kStatuses[std::find(active.begin(), active.end(), true) - active.begin()];
        view.ext = ext;
    }

    void Text(float align, const std::string& text, const ImVec4& color) {
        const float slack =
            ImGui::GetContentRegionAvail().x
            - ImGui::CalcTextSize(text.c_str()).x;
        if (slack > 0.0f)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + slack * align);

        ImGui::TextColored(color, "%s", text.c_str());
    }

    void Label(const char* text) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", text);
        ImGui::SameLine(LABEL_W);
    }

    bool Combo(const char* label, int* selected, const char* items) {
        Label(label);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::PushID(label);
        const bool changed = ImGui::Combo("##v", selected, items);
        ImGui::PopID();
        return changed;
    }

    // snapshot state into config on page open
    // combo indices stay stable while user browses
    // otherwise each poll would reset selection to live value
    void OpenConfig() {
        const HidProfile& profile = device.Profile();
        const Extended& ext = view.ext;
        const ValueRef& low = profile[Fid::LowTransfer];
        const ValueRef& high = profile[Fid::HighTransfer];

        page = PageConfig;

        // known setting codes form contiguous ranges
        config.beeper = std::clamp<int>(ext.beeperCode - BEEPER_DISABLED, 0, BEEPER_MUTED - BEEPER_DISABLED);
        config.sens = std::clamp<int>(ext.sensitivity, SENSITIVITY_LOW, SENSITIVITY_HIGH);

        // combo indices start at descriptor minimum
        config.low = std::clamp(ext.lowTransferVoltage - low.logicalMin, 0, low.logicalMax - low.logicalMin);
        config.high = std::clamp(ext.highTransferVoltage - high.logicalMin, 0, high.logicalMax - high.logicalMin);

        // combo items are zero-separated
        config.lowItems.clear();
        for (int value = low.logicalMin; value <= low.logicalMax; ++value)
            config.lowItems += std::format("{} V", value) + '\0';

        config.highItems.clear();
        for (int value = high.logicalMin; value <= high.logicalMax; ++value)
            config.highItems += std::format("{} V", value) + '\0';
    }

    bool DrawConfig() {

        if (ImGui::Button("Back", {-FLT_MIN, BUTTON_H})) {
            page = PageMonitor;
            return false;
        }

        ImGui::Dummy({0, 8});

        const auto& ext = view.ext;
        const auto& profile = device.Profile();
        using Setting = UpsDevice::Setting;
        bool changed = false;

        if (ext.hasBeeper && Combo("Beeper", &config.beeper, "Disabled\0Enabled\0Muted\0"))
            changed |= device.SetValue(Setting::Beeper, 
                static_cast<uint8_t>(config.beeper + BEEPER_DISABLED));

        if (ext.hasSensitivity && Combo("Sensitivity", &config.sens, "Low\0Medium\0High\0"))
            changed |= device.SetValue(Setting::Sensitivity, 
                static_cast<uint8_t>(config.sens));

        if (ext.hasLowTransfer || ext.hasHighTransfer)
            ImGui::SeparatorText("Voltage");

        if (ext.hasLowTransfer && Combo("Low transfer", &config.low, config.lowItems.c_str()))
            changed |= device.SetValue(Setting::LowTransfer, 
                static_cast<uint16_t>(profile[Fid::LowTransfer].logicalMin + config.low));

        if (ext.hasHighTransfer && Combo("High transfer", &config.high, config.highItems.c_str()))
            changed |= device.SetValue(Setting::HighTransfer, 
                static_cast<uint16_t>(profile[Fid::HighTransfer].logicalMin + config.high));

        return changed;
    }

    void DrawValues() {
        for (int i = MetricBattery; i < DetailRatedPower; ++i) {
            const Item& item = view.items[i];
            const bool bar = i >= BarCharge;

            if (!bar && !item.visible)
                continue;

            if (bar)
                ImGui::Dummy({0, kBarGap[i - BarCharge]});

            Label(item.label);

            if (bar) {
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, item.color);
                ImGui::ProgressBar(item.value, {-FLT_MIN, BAR_HEIGHT}, item.text.c_str());
                ImGui::PopStyleColor();
            } else {
                Text(1.0f, item.text, item.color);
            }
        }

        ImGui::Dummy({0, 8});

        if (ImGui::Button("Details", {
                (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.0f,
                BUTTON_H }))
            page = PageDetails;

        ImGui::SameLine();
        if (ImGui::Button("Config", {-FLT_MIN, BUTTON_H}))
            OpenConfig();
    }

    // extra readings live on separate page
    // monitor page stays inside fixed window without scroll
    void DrawDetails() {
        if (ImGui::Button("Back", {-FLT_MIN, BUTTON_H})) {
            page = PageMonitor;
            return;
        }

        ImGui::Dummy({0, 8});
        ImGui::SeparatorText("Device details");

        // fit extra row inside fixed window
        if (view.items[DetailTransferReason].visible)
            ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding, 0.0f);

        for (int i = DetailRatedPower; i < ItemCount; ++i) {
            const Item& item = view.items[i];

            if (i == InfoTitle) {
                ImGui::Dummy({0, 8});
                ImGui::SeparatorText("Device info");
            }

            if (i < InfoTitle) {
                if (!item.visible)
                    continue;

                Label(item.label);
                Text(1.0f, item.text, item.color);
            } else {
                if (item.text.empty())
                    continue;

                Text(0.5f, item.text, item.color);
            }
        }

        if (view.items[DetailTransferReason].visible)
            ImGui::PopStyleVar();
    }

    // no title bar, so close button is drawn manually
    bool CloseButton() {
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - CLOSE_SIZE - ImGui::GetStyle().WindowPadding.x);
        ImGui::PushStyleColor(ImGuiCol_Button, { 0, 0, 0, 0 });
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, CLOSE_HOVER);

        const bool clicked = ImGui::SmallButton("X");
        ImGui::PopStyleColor(2);
        return clicked;
    }

} // namespace

    // ImGui state lives in memory only
    // no ini file written next to exe
    void UiInit(HWND window, ID3D11Device* gpu, ID3D11DeviceContext* context) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        auto& style = ImGui::GetStyle();
        style.WindowRounding = 0.0f;
        style.FrameRounding = 4.0f;
        style.WindowPadding = { 10, 10 };
        style.ItemSpacing = { 8, 4 };
        style.SeparatorTextAlign = { 0.5f, 0.5f };
        style.SeparatorTextBorderSize = 1.0f;
        style.Colors[ImGuiCol_WindowBg] = BG_WIN;
        style.Colors[ImGuiCol_Text] = TEXT_MAIN;
        style.Colors[ImGuiCol_TextDisabled] = TEXT_DIM;
        style.Colors[ImGuiCol_Separator] = BG_BAR;
        style.Colors[ImGuiCol_FrameBg] = BG_BAR;

        ImGui_ImplWin32_Init(window);
        ImGui_ImplDX11_Init(gpu, context);
    }

    // close HID device before touching GPU
    // HID handle must not outlive the render context
    void UiClose() {
        device.Disconnect();
        if (!ImGui::GetCurrentContext())
            return;

        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }

    // redraw every frame
    // poll HID bus only when POLL_MS elapsed
    // returns false when user closed window
    bool UiFrame() {
        const uint64_t now = GetTickCount64();
        if (now - lastPoll >= POLL_MS) {
            UpdateView(device.ReadState());
            lastPoll = now;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("##Main", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);

        const bool running = !CloseButton();

        ImGui::PushStyleColor(ImGuiCol_Text, view.status.color);
        ImGui::SeparatorText(view.status.text);
        ImGui::PopStyleColor();

        switch (page) {
        case PageConfig:
            if (DrawConfig()) {
                UpdateView(device.ReadState());
                lastPoll = GetTickCount64();
            }
            break;
        case PageDetails: DrawDetails(); break;
        case PageMonitor: DrawValues(); break;
        }

        ImGui::End();
        ImGui::Render();

        return running;
    }

} // namespace Ups
