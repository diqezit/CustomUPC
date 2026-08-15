#include "ui/MainWindow.h"
#include "utils/Logger.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <wrl/client.h>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include <filesystem>
#include <memory>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND, UINT, WPARAM, LPARAM);

namespace {

    using Microsoft::WRL::ComPtr;
    using Ups::Logger;
    using Ups::MainWindow;

    constexpr wchar_t kWindowClass[] = L"UpsMon";
    constexpr wchar_t kMutexName[] = L"UpsMonitor_SingleInstance";
    constexpr wchar_t kWindowTitle[] = L"UPS";
    constexpr int     kCloseBtnW = 40;
    constexpr DWORD   kFrameDelayMs = 16;

    // ============================================================================
    //                              HANDLE RAII
    // ============================================================================

    struct HandleCloser {
        void operator()(HANDLE h) const noexcept {
            if (h) CloseHandle(h);
        }
    };
    using UniqueHandle = std::unique_ptr<std::remove_pointer_t<HANDLE>, HandleCloser>;

    void OpenLog() {
        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        Logger::Instance().Open(
            (std::filesystem::path(exe).parent_path() / "ups_monitor.log").string());
    }

    POINT WindowOrigin() {
        return {
            GetSystemMetrics(SM_CXSCREEN)
                - static_cast<int>(Ups::Layout::WINDOW_WIDTH)
                - static_cast<int>(Ups::Layout::MARGIN),
            GetSystemMetrics(SM_CYSCREEN)
                - static_cast<int>(Ups::Layout::WINDOW_HEIGHT)
                - static_cast<int>(Ups::Layout::TASKBAR_H)
        };
    }

    // ============================================================================
    //                              D3D11 BACKEND
    // ============================================================================

    struct D3d11 {
        ComPtr<ID3D11Device>           device;
        ComPtr<ID3D11DeviceContext>    context;
        ComPtr<IDXGISwapChain>         swapChain;
        ComPtr<ID3D11RenderTargetView> rtv;

        bool Init(HWND hwnd) {
            DXGI_SWAP_CHAIN_DESC sd{};
            sd.BufferCount = 2;
            sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            sd.OutputWindow = hwnd;
            sd.SampleDesc.Count = 1;
            sd.Windowed = TRUE;
            sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

            if (FAILED(D3D11CreateDeviceAndSwapChain(
                nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                nullptr, 0, D3D11_SDK_VERSION,
                &sd, &swapChain, &device, nullptr, &context)))
            {
                return false;
            }
            return CreateRTV();
        }

        bool CreateRTV() {
            ComPtr<ID3D11Texture2D> backBuffer;
            if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
                return false;
            }
            return SUCCEEDED(device->CreateRenderTargetView(backBuffer.Get(), nullptr, &rtv));
        }

        void Resize() {
            if (!swapChain) return;
            rtv.Reset();
            swapChain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
            CreateRTV();
        }

        void Present() {
            if (!context || !rtv) return;

            constexpr float kClear[] = { 0.f, 0.f, 0.f, 0.f };
            context->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
            context->ClearRenderTargetView(rtv.Get(), kClear);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            swapChain->Present(1, 0);
        }

        void Shutdown() {
            rtv.Reset();
            swapChain.Reset();
            context.Reset();
            device.Reset();
        }
    };

    // ============================================================================
    //                              APPLICATION
    // ============================================================================

    class Application {
    public:
        int Run(HINSTANCE instance) {
            instance_ = instance;

            UniqueHandle mutex(CreateMutexW(nullptr, TRUE, kMutexName));
            if (GetLastError() == ERROR_ALREADY_EXISTS) {
                MessageBoxW(nullptr, L"Already running", L"UPS Monitor", MB_ICONINFORMATION);
                return 0;
            }

            OpenLog();
            LOG_INFO("Application starting...");

            if (!CreateHwnd() ||
                !d3d_.Init(hwnd_) ||
                !ui_.Init(hwnd_, d3d_.device.Get(), d3d_.context.Get()))
            {
                LOG_ERROR("Failed to create main window");
                Cleanup();
                Logger::Instance().Close();
                return 1;
            }

            ShowWindow(hwnd_, SW_SHOWDEFAULT);
            UpdateWindow(hwnd_);
            LOG_INFO("Main window created");

            Loop();

            LOG_INFO("Shutting down");
            Cleanup();
            Logger::Instance().Close();
            return 0;
        }

    private:
        HWND        hwnd_ = nullptr;
        HINSTANCE   instance_ = nullptr;
        D3d11       d3d_;
        MainWindow  ui_;

        bool CreateHwnd() {
            const WNDCLASSEXW wc{
                sizeof(wc), CS_CLASSDC, &WndProc, 0, 0, instance_,
                nullptr, LoadCursor(nullptr, IDC_ARROW),
                nullptr, nullptr, kWindowClass, nullptr
            };
            if (!RegisterClassExW(&wc)) {
                return false;
            }

            const POINT pos = WindowOrigin();
            hwnd_ = CreateWindowExW(
                WS_EX_TOPMOST, kWindowClass, kWindowTitle, WS_POPUP | WS_VISIBLE,
                pos.x, pos.y,
                static_cast<int>(Ups::Layout::WINDOW_WIDTH),
                static_cast<int>(Ups::Layout::WINDOW_HEIGHT),
                nullptr, nullptr, instance_, this);

            return hwnd_ != nullptr;
        }

        void Loop() {
            MSG msg{};
            while (ui_.IsRunning()) {
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                    if (msg.message == WM_QUIT) {
                        ui_.RequestClose();
                    }
                }
                ui_.Tick();
                d3d_.Present();
                Sleep(kFrameDelayMs);
            }
        }

        void Cleanup() {
            ui_.Shutdown();
            d3d_.Shutdown();
            if (hwnd_) {
                DestroyWindow(hwnd_);
                hwnd_ = nullptr;
            }
            UnregisterClassW(kWindowClass, instance_);
        }

        static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
            if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) {
                return 1;
            }

            if (msg == WM_NCCREATE) {
                auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                    reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            }

            auto* self = reinterpret_cast<Application*>(
                GetWindowLongPtrW(hwnd, GWLP_USERDATA));

            switch (msg) {
            case WM_NCHITTEST: {
                POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                ScreenToClient(hwnd, &pt);
                const bool caption =
                    pt.y < Ups::Layout::CAPTION_H &&
                    pt.x < Ups::Layout::WINDOW_WIDTH - kCloseBtnW;
                return caption ? HTCAPTION : HTCLIENT;
            }
            case WM_SIZE:
                if (self && self->d3d_.device && wp != SIZE_MINIMIZED) {
                    self->d3d_.Resize();
                }
                return 0;
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
    };

} // namespace

int WINAPI wWinMain(
    _In_ HINSTANCE instance,
    _In_opt_ HINSTANCE,
    _In_ LPWSTR,
    _In_ int)
{
    return Application().Run(instance);
}