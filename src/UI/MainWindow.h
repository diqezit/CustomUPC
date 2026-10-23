#pragma once

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "core/UpsDevice.h"
#include "utils/Utf.h"

#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <utility>

namespace Ups {

    // ============================================================================
    //                          UI LAYOUT CONFIGURATION
    // ============================================================================

    namespace Layout {
        constexpr float WINDOW_WIDTH = 300.0f;
        constexpr float WINDOW_HEIGHT = 480.0f;
        constexpr float MARGIN = 16.0f;
        constexpr float TASKBAR_H = 50.0f;
        constexpr float CAPTION_H = 50.0f;
        constexpr float CLOSE_SIZE = 20.0f;
        constexpr float LABEL_W = 128.0f;
        constexpr float BAR_HEIGHT = 14.0f;
        constexpr float BAR_ROUNDING = 4.0f;
        constexpr float ROW_SPACING = 4.0f;
        constexpr float FOOTER_H = 60.0f;
        constexpr float BUTTON_H = 20.0f;
        constexpr float GAP_BAR = 2.0f;
        constexpr float GAP_SECTION = 12.0f;
        constexpr float GAP_CTRL = 8.0f;
        constexpr float GAP_FOOTER = 6.0f;

        constexpr ULONGLONG POLL_INTERVAL_MS = 1000;
    }

    // ============================================================================
    //                              UI THEME
    // ============================================================================

    namespace Theme {
        constexpr ImVec4 BG_WIN = { 0.13f, 0.13f, 0.15f, 1.0f };
        constexpr ImVec4 BG_BAR = { 0.22f, 0.22f, 0.25f, 1.0f };
        constexpr ImVec4 TEXT_MAIN = { 0.92f, 0.92f, 0.94f, 1.0f };
        constexpr ImVec4 TEXT_SUB = { 0.70f, 0.70f, 0.75f, 1.0f };
        constexpr ImVec4 TEXT_DIM = { 0.50f, 0.50f, 0.55f, 1.0f };
        constexpr ImVec4 BTN_CLOSE_H = { 0.80f, 0.30f, 0.30f, 1.0f };
        constexpr ImVec4 COL_OK = { 0.26f, 0.75f, 0.50f, 1.0f };
        constexpr ImVec4 COL_WARN = { 0.95f, 0.70f, 0.20f, 1.0f };
        constexpr ImVec4 COL_CRIT = { 0.85f, 0.35f, 0.35f, 1.0f };
        constexpr ImVec4 COL_INFO = { 0.30f, 0.65f, 0.95f, 1.0f };

        struct StatusLook {
            const char* text;
            ImVec4      color;
        };

        [[nodiscard]] inline ImVec4 ColorOf(Health h) {
            switch (h) {
            case Health::Good:     return COL_OK;
            case Health::Warning:  return COL_WARN;
            case Health::Critical: return COL_CRIT;
            default:               return TEXT_DIM;
            }
        }

        [[nodiscard]] inline ImVec4 ColorOf(AvrStatus s) {
            return s == AvrStatus::Normal ? COL_OK : COL_INFO;
        }

        [[nodiscard]] inline StatusLook StatusOf(PowerStatus s) {
            switch (s) {
            case PowerStatus::Disconnected: return { "DISCONNECTED", COL_CRIT };
            case PowerStatus::OnBattery:    return { "BATTERY MODE", COL_WARN };
            case PowerStatus::Charging:     return { "CHARGING", COL_INFO };
            case PowerStatus::Overload:     return { "OVERLOAD", COL_CRIT };
            case PowerStatus::BadBattery:   return { "REPLACE BATTERY", COL_CRIT };
            case PowerStatus::Boost:        return { "AVR BOOST", COL_OK };
            case PowerStatus::Trim:         return { "AVR TRIM", COL_OK };
            default:                        return { "ONLINE", COL_OK };
            }
        }

        inline void Apply() {
            auto& s = ImGui::GetStyle();
            s.WindowRounding = 0.0f;
            s.FrameRounding = Layout::BAR_ROUNDING;
            s.WindowPadding = { 10, 10 };
            s.ItemSpacing = { 8, Layout::ROW_SPACING };
            s.SeparatorTextAlign = { 0.5f, 0.5f };
            s.SeparatorTextBorderSize = 1.0f;

            s.Colors[ImGuiCol_WindowBg] = BG_WIN;
            s.Colors[ImGuiCol_Text] = TEXT_MAIN;
            s.Colors[ImGuiCol_TextDisabled] = TEXT_DIM;
            s.Colors[ImGuiCol_Separator] = BG_BAR;
            s.Colors[ImGuiCol_FrameBg] = BG_BAR;
            s.Colors[ImGuiCol_PlotHistogram] = COL_OK;
        }
    }

    // ============================================================================
    //                              UI HELPERS
    // ============================================================================

    template<size_t N>
    void CopyText(char(&dst)[N], const char* src) {
        std::snprintf(dst, N, "%s", src ? src : "");
    }

    template<size_t N>
    void CopyText(char(&dst)[N], const std::string& src) {
        CopyText(dst, src.c_str());
    }

    struct Line {
        const char* label = "";
        char        text[32]{};
        ImVec4      color = Theme::TEXT_DIM;
        float       frac = 0.0f;
        bool        visible = false;

        template<class T>
        void Set(
            const char* l,
            const T& t,
            const ImVec4& c,
            bool vis = true,
            float f = 0.0f)
        {
            label = l;
            color = c;
            visible = vis;
            frac = f;
            text[0] = '\0';
            if (vis) {
                CopyText(text, t);
            }
        }
    };

    struct UI {
        static void Gap(float h) {
            ImGui::Dummy({ 0, h });
        }

        static void FillWidth() {
            ImGui::SetNextItemWidth(-FLT_MIN);
        }

        // 0 = left, 0.5 = center, 1 = right
        static void TextAligned(float align, const char* text, const ImVec4& color) {
            const float slack = ImGui::GetContentRegionAvail().x
                - ImGui::CalcTextSize(text).x;
            if (slack > 0.0f) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + slack * align);
            }
            ImGui::TextColored(color, "%s", text);
        }

        static void LabelCol(const char* text) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", text);
            ImGui::SameLine(Layout::LABEL_W);
        }

        static void MetricRow(const Line& item) {
            if (!item.visible) {
                return;
            }
            LabelCol(item.label);
            TextAligned(1.0f, item.text, item.color);
        }

        static bool WideButton(const char* label) {
            return ImGui::Button(label, { -FLT_MIN, Layout::BUTTON_H });
        }

        static bool Combo(const char* label, int* sel, const char* items) {
            LabelCol(label);
            FillWidth();
            ImGui::PushID(label);
            const bool changed = ImGui::Combo("##v", sel, items);
            ImGui::PopID();
            return changed;
        }

        static void Bar(const Line& item) {
            const float frac = std::clamp(item.frac, 0.0f, 1.0f);
            char overlay[8];
            std::snprintf(overlay, sizeof(overlay), "%.0f%%", frac * 100.0f);

            LabelCol(item.label);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, item.color);
            ImGui::ProgressBar(frac, { -FLT_MIN, Layout::BAR_HEIGHT }, overlay);
            ImGui::PopStyleColor();
        }

        template<size_t N>
        static void DrawMetrics(const Line(&items)[N]) {
            for (const Line& item : items) {
                MetricRow(item);
            }
        }

        template<size_t N>
        static void DrawBars(const Line(&bars)[N]) {
            for (size_t i = 0; i < N; ++i) {
                if (i > 0) {
                    Gap(Layout::GAP_BAR);
                }
                Bar(bars[i]);
            }
        }

        template<size_t N>
        static void DrawFooter(const Line(&lines)[N], int count) {
            const float y = ImGui::GetWindowHeight() - Layout::FOOTER_H;
            if (ImGui::GetCursorPosY() < y) {
                ImGui::SetCursorPosY(y);
            }

            ImGui::Separator();
            Gap(Layout::GAP_FOOTER);

            const int n = (count < static_cast<int>(N)) ? count : static_cast<int>(N);
            for (int i = 0; i < n; ++i) {
                TextAligned(0.5f, lines[i].text, lines[i].color);
            }
        }

        static bool CloseButton() {
            ImGui::SetCursorPosX(
                ImGui::GetWindowWidth()
                - Layout::CLOSE_SIZE
                - ImGui::GetStyle().WindowPadding.x
            );

            ImGui::PushStyleColor(ImGuiCol_Button, { 0, 0, 0, 0 });
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::BTN_CLOSE_H);
            const bool clicked = ImGui::SmallButton("X");
            ImGui::PopStyleColor(2);
            return clicked;
        }

        static void StatusHeader(PowerStatus s) {
            const auto st = Theme::StatusOf(s);
            ImGui::PushStyleColor(ImGuiCol_Text, st.color);
            ImGui::SeparatorText(st.text);
            ImGui::PopStyleColor();
        }

        static void BeginFrame() {
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            ImGui::SetNextWindowPos({ 0, 0 });
            ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
            ImGui::Begin(
                "##Main", nullptr,
                ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoScrollbar
            );
        }

        static void EndFrame() {
            ImGui::End();
            ImGui::Render();
        }
    };

    // ============================================================================
    //                              DISPLAY CACHE
    // ============================================================================

    // Formatted once per device poll. Render only reads these buffers.
    struct DisplayCache {
        enum Metric : int {
            BatteryVoltage, InputVoltage, Runtime, OutputVoltage,
            Avr, Sensitivity, Beeper, MetricCount
        };
        enum Bar : int { Charge, Load, BarCount };

        Line        metrics[MetricCount]{};
        Line        bars[BarCount]{};
        Line        ident[3]{};
        int         identCount = 0;
        PowerStatus status = PowerStatus::Disconnected;

        void Rebuild(const UpsState& s) {
            FillMetrics(s);
            FillBars(s);
            FillIdent(s);
            status = GetPowerStatus(s);
        }

    private:
        void Put(
            Metric id,
            const char* label,
            const std::string& text,
            Health health)
        {
            metrics[id].Set(label, text, Theme::ColorOf(health));
        }

        void Opt(
            Metric id,
            const char* label,
            const std::string& text,
            bool show,
            const ImVec4& color = Theme::TEXT_MAIN)
        {
            metrics[id].Set(label, text, color, show);
        }

        void AddIdent(const std::string& text, const ImVec4& color) {
            ident[identCount++].Set("", text, color);
        }

        void FillMetrics(const UpsState& s) {
            const bool liveBatt = HasValidBatteryVoltage(s);
            const bool liveIn = HasValidInputVoltage(s) && !s.onBattery;
            const AvrStatus avr = s.ext.hasAvr
                ? ParseAvrStatus(s.ext.avrRaw)
                : AvrStatus::Unknown;

            Put(BatteryVoltage, "Battery Voltage",
                liveBatt ? Formatter::FormatVoltage(s.batteryVoltage) : "--",
                liveBatt ? GetBatteryHealth(s) : Health::Unknown);
            Put(InputVoltage, "Input Voltage",
                liveIn ? Formatter::FormatVoltage(s.inputVoltage) : "--",
                GetInputHealth(s));
            Put(Runtime, "Est. Runtime",
                Formatter::FormatRuntime(s.runtimeSeconds), GetRuntimeHealth(s));

            Opt(OutputVoltage, "Output Voltage",
                Formatter::FormatVoltage(s.ext.outputVoltage), s.ext.hasOutput);
            Opt(Avr, "AVR Status",
                Formatter::FormatAvrStatus(avr),
                avr != AvrStatus::Unknown, Theme::ColorOf(avr));
            Opt(Sensitivity, "Sensitivity",
                Formatter::SensitivityToString(s.ext.sensitivity),
                s.ext.hasSensitivity, Theme::TEXT_SUB);
            Opt(Beeper, "Beeper",
                Formatter::BeeperToString(s.ext.beeperCode),
                s.ext.hasBeeper, Theme::TEXT_SUB);
        }

        void FillBars(const UpsState& s) {
            bars[Charge].Set("Charge", "",
                Theme::ColorOf(GetChargeHealth(s)), true, s.GetChargeNormalized());
            bars[Load].Set("Load", "",
                Theme::ColorOf(GetLoadHealth(s)), true, s.GetLoadNormalized());
        }

        void FillIdent(const UpsState& s) {
            const auto mfr = WideToUtf8(s.manufacturer);
            const auto prod = WideToUtf8(s.product);
            const auto ser = WideToUtf8(s.serial);

            identCount = 0;
            if (!mfr.empty()) {
                AddIdent(mfr, Theme::TEXT_MAIN);
            }
            if (!prod.empty() && prod != mfr) {
                AddIdent(prod, Theme::TEXT_SUB);
            }
            else if (mfr.empty()) {
                AddIdent("UPS Device", Theme::TEXT_MAIN);
            }
            if (!ser.empty()) {
                AddIdent(ser, Theme::TEXT_DIM);
            }
        }
    };

    // ============================================================================
    //                              DEVICE VIEW
    // ============================================================================

    struct DeviceView {
        DisplayCache cache;
        Extended     ext{};

        void Refresh(UpsDevice& device) {
            const UpsState s = device.ReadState();
            cache.Rebuild(s);
            ext = s.ext;
            lastUpdate_ = GetTickCount64();
        }

        void Poll(UpsDevice& device) {
            if (GetTickCount64() - lastUpdate_ < Layout::POLL_INTERVAL_MS) {
                return;
            }
            Refresh(device);
        }

    private:
        ULONGLONG lastUpdate_ = 0;
    };

    // ============================================================================
    //                              CONFIG PANEL
    // ============================================================================

    // Combo indexes are local to the panel
    //
    // The device stores a code, so each setting maps its index back to that code
    struct ConfigPanel {
        [[nodiscard]] bool IsOpen() const noexcept { return open_; }

        void Open(const Extended& e) {
            open_ = true;
            beeper_.Load(e.hasBeeper, e.beeperCode);
            sens_.Load(e.hasSensitivity, e.sensitivity);
            low_.Load(e.hasLowTransfer, e.lowTransferVoltage);
            high_.Load(e.hasHighTransfer, e.highTransferVoltage);
        }

        bool Draw(UpsDevice& device) {
            if (UI::WideButton("Back")) {
                open_ = false;
                return false;
            }

            UI::Gap(Layout::GAP_CTRL);
            bool changed = false;

            changed |= beeper_.Draw([&](uint16_t v) {
                return device.SetBeeper(static_cast<uint8_t>(v));
            });

            changed |= sens_.Draw([&](uint16_t v) {
                return device.SetSensitivity(static_cast<uint8_t>(v));
            });

            if (low_.available || high_.available) {
                ImGui::SeparatorText("Voltage");

                changed |= low_.Draw([&](uint16_t v) {
                    return device.SetLowTransfer(v);
                });

                changed |= high_.Draw([&](uint16_t v) {
                    return device.SetHighTransfer(v);
                });
            }
            return changed;
        }

    private:
        struct Setting {
            const char* label = "";
            std::string items;
            int         first = 0;
            int         last = 0;
            int         sel = 0;
            bool        available = false;

            Setting(const char* l, std::string i, int f, int lv)
                : label(l), items(std::move(i)), first(f), last(lv) {}

            void Load(bool present, uint16_t value) {
                available = present;
                if (!available) return;

                // UPS can report a value outside the short list
                // so snap to nearest option instead of leaving it blank
                sel = std::clamp(static_cast<int>(value) - first, 0, last - first);
            }

            template<class Fn>
            bool Draw(Fn&& set) {
                if (!available) return false;
                if (UI::Combo(label, &sel, items.c_str()))
                    return set(static_cast<uint16_t>(first + sel));
                return false;
            }
        };

        static std::string Items(std::initializer_list<const char*> names) {
            std::string items;
            for (const char* name : names) {
                items += name;
                items.push_back('\0');
            }
            items.push_back('\0');
            return items;
        }

        static std::string VoltageItems(int first, int last) {
            std::string items;
            for (int v = first; v <= last; ++v) {
                items += std::to_string(v);
                items += " V";
                items.push_back('\0');
            }
            items.push_back('\0');
            return items;
        }

        bool    open_ = false;
        Setting beeper_{ "Beeper", Items({ "Disabled", "Enabled", "Muted" }), 1, 3 };
        Setting sens_{ "Sensitivity", Items({ "Low", "Medium", "High" }), 0, 2 };

        // official PowerChute page limits these ranges to whole volts
        Setting low_{ "Low transfer", VoltageItems(140, 150), 140, 150 };
        Setting high_{ "High transfer", VoltageItems(290, 300), 290, 300 };
    };

    // ============================================================================
    //                              MONITOR PANEL
    // ============================================================================

    struct MonitorPanel {
        static void Draw(const DeviceView& view, ConfigPanel& config) {
            UI::DrawMetrics(view.cache.metrics);

            UI::Gap(Layout::GAP_SECTION);
            UI::DrawBars(view.cache.bars);

            UI::Gap(Layout::GAP_CTRL);
            if (UI::WideButton("Config")) {
                config.Open(view.ext);
            }
        }
    };

    // ============================================================================
    //                              MAIN WINDOW
    // ============================================================================

    class MainWindow {
    public:
        ~MainWindow() { Shutdown(); }

        bool Init(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context) {
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGui::GetIO().IniFilename = nullptr;

            Theme::Apply();
            ImGui_ImplWin32_Init(hwnd);
            ImGui_ImplDX11_Init(device, context);
            device_.Connect();
            imguiReady_ = true;
            return true;
        }

        void Shutdown() {
            if (!imguiReady_) {
                return;
            }
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            imguiReady_ = false;
        }

        void Tick() {
            view_.Poll(device_);
            UI::BeginFrame();
            Draw();
            UI::EndFrame();
        }

        [[nodiscard]] bool IsRunning() const { return running_; }
        void RequestClose() { running_ = false; }

    private:
        bool        imguiReady_ = false;
        bool        running_ = true;
        UpsDevice   device_;
        DeviceView  view_{};
        ConfigPanel config_{};

        void Draw() {
            if (UI::CloseButton()) {
                running_ = false;
            }

            UI::StatusHeader(view_.cache.status);

            if (config_.IsOpen()) {
                if (config_.Draw(device_))
                    view_.Refresh(device_);
            }
            else {
                MonitorPanel::Draw(view_, config_);
            }

            UI::DrawFooter(view_.cache.ident, view_.cache.identCount);
        }
    };

} // namespace Ups