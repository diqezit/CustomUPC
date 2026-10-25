#include "MainWindow.h"
#include "Logger.h"

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include <windowsx.h>
#include <wrl/client.h>
#include <filesystem>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND, UINT, WPARAM, LPARAM);

namespace {

    using Microsoft::WRL::ComPtr;

    constexpr wchar_t WINDOW_CLASS[] = L"UpsMon";
    constexpr wchar_t MUTEX_NAME[] = L"UpsMonitor_SingleInstance";
    constexpr wchar_t LOG_NAME[] = L"ups_monitor.log";

    constexpr int WIDTH = 300;
    constexpr int HEIGHT = 330;
    constexpr int MARGIN = 16;
    constexpr int TASKBAR_H = 50;
    constexpr int CAPTION_H = 50;
    constexpr int CLOSE_W = 40;
    constexpr int FRAME_MS = 50;

    struct D3d {
        ComPtr<ID3D11RenderTargetView> target;
        ComPtr<IDXGISwapChain> swap;
        ComPtr<ID3D11DeviceContext> context;
        ComPtr<ID3D11Device> gpu;
    } d3d;

    void OpenLog() {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(nullptr, path, MAX_PATH);

        const auto logPath =
            std::filesystem::path(path).parent_path() / LOG_NAME;
        LogOpen(logPath.c_str());
    }

    bool CreateTarget() {
        ComPtr<ID3D11Texture2D> buffer;
        if (FAILED(d3d.swap->GetBuffer(0, IID_PPV_ARGS(&buffer))))
            return false;

        return SUCCEEDED(d3d.gpu->CreateRenderTargetView(
            buffer.Get(), nullptr, &d3d.target));
    }

    bool InitD3d(HWND window) {
        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferCount = 2;
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.OutputWindow = window;
        desc.SampleDesc.Count = 1;
        desc.Windowed = TRUE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        const HRESULT status = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            0,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            &desc,
            &d3d.swap,
            &d3d.gpu,
            nullptr,
            &d3d.context);

        if (FAILED(status))
            return false;

        return CreateTarget();
    }

    void ResizeD3d() {
        d3d.target.Reset();

        if (SUCCEEDED(d3d.swap->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0)))
            CreateTarget();
    }

    void Render() {
        if (!d3d.target)
            return;

        constexpr float clear[] = { 0, 0, 0, 0 };
        d3d.context->OMSetRenderTargets(
            1, d3d.target.GetAddressOf(), nullptr);
        d3d.context->ClearRenderTargetView(d3d.target.Get(), clear);

        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        d3d.swap->Present(1, 0);
    }

    LRESULT CALLBACK WindowProc(
        HWND window, UINT message, WPARAM wp, LPARAM lp) {
        if (ImGui_ImplWin32_WndProcHandler(window, message, wp, lp))
            return 1;

        switch (message) {
        case WM_NCHITTEST: {
            POINT point{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ScreenToClient(window, &point);

            if (point.y < CAPTION_H && point.x < WIDTH - CLOSE_W)
                return HTCAPTION;

            return HTCLIENT;
        }
        case WM_SIZE:
            if (d3d.gpu && wp != SIZE_MINIMIZED)
                ResizeD3d();

            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }

        return DefWindowProcW(window, message, wp, lp);
    }

    HWND CreateHwnd(HINSTANCE instance) {
        const WNDCLASSEXW cls{
            .cbSize = sizeof(cls),
            .style = CS_CLASSDC,
            .lpfnWndProc = WindowProc,
            .hInstance = instance,
            .hCursor = LoadCursor(nullptr, IDC_ARROW),
            .lpszClassName = WINDOW_CLASS
        };

        if (!RegisterClassExW(&cls))
            return nullptr;

        const int x = GetSystemMetrics(SM_CXSCREEN) - WIDTH - MARGIN;
        const int y = GetSystemMetrics(SM_CYSCREEN) - HEIGHT - TASKBAR_H;

        return CreateWindowExW(
            WS_EX_TOPMOST,
            WINDOW_CLASS,
            L"UPS",
            WS_POPUP | WS_VISIBLE,
            x, y, WIDTH, HEIGHT,
            nullptr,
            nullptr,
            instance,
            nullptr);
    }

    void Loop() {
        MSG message{};

        while (true) {
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message == WM_QUIT)
                    return;

                TranslateMessage(&message);
                DispatchMessageW(&message);
            }

            if (!Ups::UiFrame())
                return;

            Render();
            Sleep(FRAME_MS);
        }
    }

    // release graphics resources before destroying swap chain window
    void CloseApp(HWND window, HINSTANCE instance) {
        Ups::UiClose();
        d3d = {};

        if (window)
            DestroyWindow(window);

        UnregisterClassW(WINDOW_CLASS, instance);
    }

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, MUTEX_NAME);
    if (!mutex)
        return 1;

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(
            nullptr, L"Already running", L"UPS Monitor", MB_ICONINFORMATION);
        CloseHandle(mutex);
        return 0;
    }

    OpenLog();
    LOG_INFO("Application starting...");

    int exitCode = 0;
    HWND window = CreateHwnd(instance);
    if (window && InitD3d(window)) {
        Ups::UiInit(window, d3d.gpu.Get(), d3d.context.Get());
        LOG_INFO("Main window created");
        Loop();
        LOG_INFO("Shutting down");
    }
    else {
        LOG_ERROR("Failed to create main window");
        exitCode = 1;
    }

    CloseApp(window, instance);
    LogClose();
    CloseHandle(mutex);

    return exitCode;
}