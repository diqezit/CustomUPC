#pragma once
#include "ui/MainWindow.h"
#include "utils/Logger.h"
#include <filesystem>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace Ups {

    class Application {
    public:
        int Run(_In_ HINSTANCE hInstance) {
            HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"UpsMonitor_SingleInstance");
            if (GetLastError() == ERROR_ALREADY_EXISTS) {
                MessageBoxW(nullptr, L"Already running", L"UPS Monitor", MB_ICONINFORMATION);
                return 0;
            }

            wchar_t path[MAX_PATH];
            GetModuleFileNameW(nullptr, path, MAX_PATH);
            Logger::Instance().Open(
                (std::filesystem::path(path).parent_path() / "ups_monitor.log").string()
            );
            LOG_INFO("Application starting...");

            window_ = std::make_unique<MainWindow>();
            if (!window_->Create(hInstance)) {
                LOG_ERROR("Failed to create main window");
                return 1;
            }

            window_->Show();
            LOG_INFO("Main window created");

            while (window_->ProcessFrame()) {
                Sleep(16);
            }

            LOG_INFO("Shutting down");
            Logger::Instance().Close();
            if (hMutex) CloseHandle(hMutex);
            return 0;
        }

    private:
        std::unique_ptr<MainWindow> window_;
    };

} // namespace Ups

int WINAPI wWinMain(
    _In_ HINSTANCE hInst,
    _In_opt_ HINSTANCE,
    _In_ LPWSTR,
    _In_ int
) {
    return Ups::Application().Run(hInst);
}