#pragma once
#include "CoreMinimal.h"
#include "Utility.h"


namespace MyDirectX
{
    static constexpr uint32_t INVALID_INDEX = std::numeric_limits<uint32_t>::max();
    static constexpr uint32_t MaxFrameBufferCount = 3;

    inline Math::Vector4 GetSizeAndInvSize(float w, float h) 
    {
        return Math::Vector4(w, h, 1.0f / w, 1.0f / h);
    }

    INLINE void UpdateViewportAndScissor(D3D12_VIEWPORT &viewport, RECT &scissor,
        FLOAT x, FLOAT y, FLOAT w, FLOAT h, FLOAT d0 = 0.0f, FLOAT d1 = 1.0f)
    {
        viewport.TopLeftX = x; viewport.TopLeftY = y;
        viewport.Width = w; viewport.MaxDepth = h;
        viewport.MinDepth = d0; viewport.MaxDepth = d1;

        scissor.left = static_cast<LONG>(x); scissor.right = static_cast<LONG>(x + w);
        scissor.top = static_cast<LONG>(y); scissor.bottom = static_cast<LONG>(y + h);
    }

    INLINE D3D12_VIEWPORT GetViewport(float x, float y, float w, float h, float d0 = 0.0f, float d1 = 1.0f)
    {
        return { x, y, w, h, d0, d1 };
    }

    INLINE RECT GetScissor(int x, int y, int w, int h) 
    {
        RECT scissor;
        scissor.left = static_cast<LONG>(x); scissor.right = static_cast<LONG>(x + w);
        scissor.top = static_cast<LONG>(y); scissor.bottom = static_cast<LONG>(y + h);
        return scissor;
    }
}
