#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

namespace Ups {

    // =========================================================================
    //                                UTF Helpers
    // =========================================================================

    inline std::string WideToUtf8(const std::wstring& ws) {
        if (ws.empty())
            return {};

        const int needed = WideCharToMultiByte(
            CP_UTF8, 0,
            ws.data(), static_cast<int>(ws.size()),
            nullptr, 0,
            nullptr, nullptr);

        if (needed <= 0)
            return {};

        std::string out(static_cast<size_t>(needed), '\0');

        const int written = WideCharToMultiByte(
            CP_UTF8, 0,
            ws.data(), static_cast<int>(ws.size()),
            out.data(), needed,
            nullptr, nullptr);

        if (written <= 0)
            return {};

        out.resize(static_cast<size_t>(written));
        return out;
    }

} // namespace Ups
