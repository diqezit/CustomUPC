#pragma once

#include <windows.h>
#include <d3d11.h>

namespace Ups {

    void UiInit(HWND window, ID3D11Device* gpu, ID3D11DeviceContext* context);
    void UiClose();
    bool UiFrame();

} // namespace Ups