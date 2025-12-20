#pragma once

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "core/UpsDevice.h"
#include "utils/Utf.h"

#include <d3d11.h>
#include <wrl/client.h>
#include <windowsx.h>
#include <string>
#include <algorithm>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND, UINT, WPARAM, LPARAM
);

namespace Ups {

    using Microsoft::WRL::ComPtr;

    // ============================================================================
    //                          UI LAYOUT CONFIGURATION
    // ============================================================================

    namespace Layout {
        constexpr float WINDOW_WIDTH = 300.0f;
        constexpr float WINDOW_HEIGHT = 480.0f;
        constexpr float MARGIN = 16.0f;
        constexpr float TASKBAR_H = 50.0f;
        constexpr float CLOSE_SIZE = 20.0f;
        constexpr float BAR_HEIGHT = 14.0f;
        constexpr float BAR_ROUNDING = 4.0f;
        constexpr float ROW_SPACING = 4.0f;
    }

    // ============================================================================
    //                              UI THEME
    // ============================================================================

    namespace Theme {
        constexpr ImVec4 BG_WIN = { 0.13f, 0.13f, 0.15f, 1.0f };
        constexpr ImVec4 BG_BAR = { 0.22f, 0.22f, 0.25f, 1.0f };
        constexpr ImVec4 SEPARATOR = { 0.22f, 0.22f, 0.25f, 1.0f };
        constexpr ImVec4 TEXT_MAIN = { 0.92f, 0.92f, 0.94f, 1.0f };
        constexpr ImVec4 TEXT_SUB = { 0.70f, 0.70f, 0.75f, 1.0f };
        constexpr ImVec4 TEXT_DIM = { 0.50f, 0.50f, 0.55f, 1.0f };
        constexpr ImVec4 BTN_CLOSE_H = { 0.80f, 0.30f, 0.30f, 1.0f };
        constexpr ImVec4 COL_OK = { 0.26f, 0.75f, 0.50f, 1.0f };
        constexpr ImVec4 COL_WARN = { 0.95f, 0.70f, 0.20f, 1.0f };
        constexpr ImVec4 COL_CRIT = { 0.85f, 0.35f, 0.35f, 1.0f };
        constexpr ImVec4 COL_INFO = { 0.30f, 0.65f, 0.95f, 1.0f };

        [[nodiscard]] inline ImVec4 GetHealthColor(Health h) {
            switch (h) {
            case Health::Good:     return COL_OK;
            case Health::Warning:  return COL_WARN;
            case Health::Critical: return COL_CRIT;
            default:               return TEXT_DIM;
            }
        }

        [[nodiscard]] inline ImVec4 GetStatusColor(PowerStatus s) {
            switch (s) {
            case PowerStatus::Disconnected:
            case PowerStatus::Overload:
            case PowerStatus::BadBattery:  return COL_CRIT;
            case PowerStatus::OnBattery:   return COL_WARN;
            case PowerStatus::Charging:    return COL_INFO;
            default:                       return COL_OK;
            }
        }
    }

    // ============================================================================
    //                              UI HELPERS
    // ============================================================================

    struct UI {
        static void CenterText(const char* text, const ImVec4& color) {
            const float w = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX((w - ImGui::CalcTextSize(text).x) * 0.5f);
            ImGui::TextColored(color, "%s", text);
        }

        static void MetricRow(
            const char* label,
            const std::string& value,
            const ImVec4& valColor)
        {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(Theme::TEXT_DIM, "%s", label);

            ImGui::TableSetColumnIndex(1);
            const float w = ImGui::CalcTextSize(value.c_str()).x;
            ImGui::SetCursorPosX(
                ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - w
            );
            ImGui::TextColored(valColor, "%s", value.c_str());
        }

        static void ProgressBar(
            const char* label,
            float fraction,
            const ImVec4& color)
        {
            ImGui::BeginGroup();

            ImGui::PushStyleColor(ImGuiCol_Text, Theme::TEXT_DIM);
            ImGui::Text("%-6s", label);
            ImGui::PopStyleColor();
            ImGui::SameLine();

            const float availW = ImGui::GetContentRegionAvail().x;
            const float percentW = ImGui::CalcTextSize("100%").x;
            const float barW = availW - percentW - 8.0f;

            ImVec2 pos = ImGui::GetCursorScreenPos();
            pos.y += (ImGui::GetTextLineHeight() - Layout::BAR_HEIGHT) * 0.5f;

            ImDrawList* dl = ImGui::GetWindowDrawList();

            // Background
            dl->AddRectFilled(
                pos,
                { pos.x + barW, pos.y + Layout::BAR_HEIGHT },
                ImGui::ColorConvertFloat4ToU32(Theme::BG_BAR),
                Layout::BAR_ROUNDING
            );

            // Foreground
            const float fillW = barW * std::clamp(fraction, 0.0f, 1.0f);
            if (fillW > 1.0f) {
                dl->AddRectFilled(
                    pos,
                    { pos.x + fillW, pos.y + Layout::BAR_HEIGHT },
                    ImGui::ColorConvertFloat4ToU32(color),
                    Layout::BAR_ROUNDING
                );
            }

            ImGui::Dummy({ barW, ImGui::GetTextLineHeight() });
            ImGui::SameLine();
            ImGui::TextColored(color, "%3.0f%%", fraction * 100.0f);

            ImGui::EndGroup();
        }

        [[nodiscard]] static const char* StatusToString(PowerStatus s) {
            switch (s) {
            case PowerStatus::Disconnected: return "DISCONNECTED";
            case PowerStatus::OnBattery:    return "BATTERY MODE";
            case PowerStatus::Charging:     return "CHARGING";
            case PowerStatus::Overload:     return "OVERLOAD";
            case PowerStatus::BadBattery:   return "REPLACE BATTERY";
            case PowerStatus::Boost:        return "AVR BOOST";
            case PowerStatus::Trim:         return "AVR TRIM";
            default:                        return "ONLINE";
            }
        }
    };

    // ============================================================================
    //                              MAIN WINDOW
    // ============================================================================

    class MainWindow {
    public:
        ~MainWindow() { Cleanup(); }

        bool Create(HINSTANCE hInst) {
            hInst_ = hInst;

            WNDCLASSEXW wc = {
                sizeof(wc),
                CS_CLASSDC,
                WndProc,
                0, 0,
                hInst,
                nullptr,
                LoadCursor(nullptr, IDC_ARROW),
                nullptr, nullptr,
                L"UpsMon",
                nullptr
            };
            RegisterClassExW(&wc);

            const int x = GetSystemMetrics(SM_CXSCREEN)
                - static_cast<int>(Layout::WINDOW_WIDTH)
                - static_cast<int>(Layout::MARGIN);

            const int y = GetSystemMetrics(SM_CYSCREEN)
                - static_cast<int>(Layout::WINDOW_HEIGHT)
                - static_cast<int>(Layout::TASKBAR_H);

            hwnd_ = CreateWindowExW(
                WS_EX_TOPMOST,
                wc.lpszClassName,
                L"UPS",
                WS_POPUP | WS_VISIBLE,
                x, y,
                static_cast<int>(Layout::WINDOW_WIDTH),
                static_cast<int>(Layout::WINDOW_HEIGHT),
                nullptr, nullptr,
                hInst,
                this
            );

            if (!hwnd_ || !InitD3D()) return false;

            InitImGui();
            device_.Connect();

            return true;
        }

        void Show() const {
            ShowWindow(hwnd_, SW_SHOWDEFAULT);
            UpdateWindow(hwnd_);
        }

        bool ProcessFrame() {
            MSG msg;
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
                if (msg.message == WM_QUIT) return false;
            }

            if (GetTickCount64() - lastUpdate_ >= 1000) {
                state_ = device_.ReadState();
                lastUpdate_ = GetTickCount64();
            }

            Render();
            return running_;
        }

    private:
        HWND      hwnd_ = nullptr;
        HINSTANCE hInst_ = nullptr;
        bool      running_ = true;

        ComPtr<ID3D11Device>           d3dDev_;
        ComPtr<ID3D11DeviceContext>    d3dCtx_;
        ComPtr<IDXGISwapChain>         swapChain_;
        ComPtr<ID3D11RenderTargetView> rtv_;

        UpsDevice device_;
        UpsState  state_{};
        ULONGLONG lastUpdate_ = 0;

        bool InitD3D() {
            DXGI_SWAP_CHAIN_DESC sd{};
            sd.BufferCount = 2;
            sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            sd.OutputWindow = hwnd_;
            sd.SampleDesc.Count = 1;
            sd.Windowed = TRUE;
            sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

            HRESULT hr = D3D11CreateDeviceAndSwapChain(
                nullptr,
                D3D_DRIVER_TYPE_HARDWARE,
                nullptr,
                0,
                nullptr, 0,
                D3D11_SDK_VERSION,
                &sd,
                &swapChain_,
                &d3dDev_,
                nullptr,
                &d3dCtx_
            );

            if (FAILED(hr)) return false;

            CreateRenderTarget();
            return true;
        }

        void CreateRenderTarget() {
            ComPtr<ID3D11Texture2D> buf;
            if (SUCCEEDED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&buf)))) {
                d3dDev_->CreateRenderTargetView(buf.Get(), nullptr, &rtv_);
            }
        }

        void InitImGui() {
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGui::GetIO().IniFilename = nullptr;

            auto& s = ImGui::GetStyle();
            s.WindowRounding = 0.0f;
            s.FrameRounding = Layout::BAR_ROUNDING;
            s.CellPadding = { 4, 2 };
            s.WindowPadding = { 10, 10 };
            s.ItemSpacing = { 8, Layout::ROW_SPACING };

            s.Colors[ImGuiCol_WindowBg] = Theme::BG_WIN;
            s.Colors[ImGuiCol_Text] = Theme::TEXT_MAIN;

            ImGui_ImplWin32_Init(hwnd_);
            ImGui_ImplDX11_Init(d3dDev_.Get(), d3dCtx_.Get());
        }

        void Render() {
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            ImGui::SetNextWindowPos({ 0, 0 });
            ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);

            ImGui::Begin(
                "##Main",
                nullptr,
                ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoScrollbar
            );

            DrawHeader();
            DrawMetrics();

            ImGui::Dummy({ 0, 12 });
            ImGui::Indent(4);

            UI::ProgressBar(
                "Charge",
                state_.GetChargeNormalized(),
                Theme::GetHealthColor(GetChargeHealth(state_))
            );

            ImGui::Dummy({ 0, 2 });

            UI::ProgressBar(
                "Load",
                state_.GetLoadNormalized(),
                Theme::GetHealthColor(GetLoadHealth(state_))
            );

            ImGui::Unindent(4);

            DrawFooter();

            ImGui::End();
            ImGui::Render();

            static const float clear[] = { 0.0f, 0.0f, 0.0f, 0.0f };
            d3dCtx_->OMSetRenderTargets(1, rtv_.GetAddressOf(), nullptr);
            d3dCtx_->ClearRenderTargetView(rtv_.Get(), clear);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            swapChain_->Present(1, 0);
        }

        void DrawHeader() {
            const float avail = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX(avail - Layout::CLOSE_SIZE + 10);

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::BTN_CLOSE_H);

            if (ImGui::Button("X", { Layout::CLOSE_SIZE, Layout::CLOSE_SIZE })) {
                running_ = false;
            }

            ImGui::PopStyleColor(2);

            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 6.0f);

            const PowerStatus status = GetPowerStatus(state_);
            UI::CenterText(UI::StatusToString(status), Theme::GetStatusColor(status));

            ImGui::Dummy({ 0, 4 });
            ImGui::PushStyleColor(ImGuiCol_Separator, Theme::SEPARATOR);
            ImGui::Separator();
            ImGui::PopStyleColor();
            ImGui::Dummy({ 0, 8 });
        }

        void DrawMetrics() {
            constexpr auto tableFlags =
                ImGuiTableFlags_SizingStretchProp |
                ImGuiTableFlags_PadOuterX;

            if (!ImGui::BeginTable("##M", 2, tableFlags)) return;

            ImGui::TableSetupColumn("L", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("R", ImGuiTableColumnFlags_WidthFixed, 90.0f);

            // Battery Voltage
            UI::MetricRow(
                "Battery Voltage",
                Formatter::FormatVoltage(state_.batteryVoltage),
                Theme::GetHealthColor(GetBatteryHealth(state_))
            );

            // Input Voltage
            const std::string inV = (HasValidInputVoltage(state_) && !state_.onBattery)
                ? Formatter::FormatVoltage(state_.inputVoltage)
                : "--";
            UI::MetricRow(
                "Input Voltage",
                inV,
                Theme::GetHealthColor(GetInputHealth(state_))
            );

            // Runtime
            UI::MetricRow(
                "Est. Runtime",
                Formatter::FormatRuntime(state_.runtimeSeconds),
                Theme::GetHealthColor(GetRuntimeHealth(state_))
            );

            // Output Voltage
            if (state_.ext.hasOutput) {
                UI::MetricRow(
                    "Output Voltage",
                    Formatter::FormatVoltage(state_.ext.outputVoltage),
                    Theme::TEXT_MAIN
                );
            }

            // Frequency
            if (state_.ext.hasInputFreq) {
                UI::MetricRow(
                    "Frequency",
                    Formatter::FormatFrequency(state_.ext.inputFrequency),
                    Theme::TEXT_MAIN
                );
            }

            // AVR Status
            if (state_.ext.hasAvr) {
                const AvrStatus avr = ParseAvrStatus(
                    state_.ext.avrRaw,
                    state_.ext.avrPowerFlags
                );

                if (avr != AvrStatus::Unknown) {
                    const ImVec4 c = (avr == AvrStatus::Normal)
                        ? Theme::COL_OK
                        : Theme::COL_INFO;
                    UI::MetricRow("AVR Status", Formatter::FormatAvrStatus(avr), c);
                }
            }

            // Sensitivity
            if (state_.ext.hasSensitivity) {
                UI::MetricRow(
                    "Sensitivity",
                    Formatter::SensitivityToString(state_.ext.sensitivity),
                    Theme::TEXT_SUB
                );
            }

            // Beeper
            if (state_.ext.hasBeeper) {
                UI::MetricRow(
                    "Beeper",
                    Formatter::BeeperToString(state_.ext.beeperCode),
                    Theme::TEXT_SUB
                );
            }

            ImGui::EndTable();
        }

        void DrawFooter() {
            const float availY = ImGui::GetContentRegionAvail().y;
            const float spacing = (availY > 60.0f) ? availY - 60.0f : 12.0f;
            ImGui::Dummy({ 0, spacing });

            ImGui::PushStyleColor(ImGuiCol_Separator, Theme::SEPARATOR);
            ImGui::Separator();
            ImGui::PopStyleColor();
            ImGui::Dummy({ 0, 6 });

            const std::string man = WideToUtf8(state_.manufacturer);
            const std::string prd = WideToUtf8(state_.product);
            const std::string ser = WideToUtf8(state_.serial);

            if (!man.empty()) {
                UI::CenterText(man.c_str(), Theme::TEXT_MAIN);
            }

            if (!prd.empty() && prd != man) {
                UI::CenterText(prd.c_str(), Theme::TEXT_SUB);
            }
            else if (man.empty()) {
                UI::CenterText("UPS Device", Theme::TEXT_MAIN);
            }

            if (!ser.empty()) {
                UI::CenterText(ser.c_str(), Theme::TEXT_DIM);
            }
        }

        void Cleanup() {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();

            if (hwnd_) DestroyWindow(hwnd_);
            UnregisterClassW(L"UpsMon", hInst_);
        }

        static LRESULT CALLBACK WndProc(
            HWND hWnd,
            UINT msg,
            WPARAM wp,
            LPARAM lp)
        {
            if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wp, lp)) {
                return 1;
            }

            if (msg == WM_NCCREATE) {
                auto* cs = reinterpret_cast<CREATESTRUCT*>(lp);
                SetWindowLongPtrW(
                    hWnd,
                    GWLP_USERDATA,
                    reinterpret_cast<LONG_PTR>(cs->lpCreateParams)
                );
            }

            auto* self = reinterpret_cast<MainWindow*>(
                GetWindowLongPtrW(hWnd, GWLP_USERDATA)
                );

            switch (msg) {
            case WM_NCHITTEST: {
                POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                ScreenToClient(hWnd, &pt);

                const bool inCaption = pt.y < 50 &&
                    pt.x < Layout::WINDOW_WIDTH - 40;
                return inCaption ? HTCAPTION : HTCLIENT;
            }

            case WM_SIZE:
                if (self && self->d3dDev_ && wp != SIZE_MINIMIZED) {
                    self->rtv_.Reset();
                    self->swapChain_->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
                    self->CreateRenderTarget();
                }
                return 0;

            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            }

            return DefWindowProcW(hWnd, msg, wp, lp);
        }
    };

} // namespace Ups