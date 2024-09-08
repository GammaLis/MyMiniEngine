#pragma once
#include <cinttypes>
#include <limits>
#include "VectorMath.h"

namespace MyDirectX
{
    static constexpr uint32_t INVALID_INDEX = std::numeric_limits<uint32_t>::max();
    static constexpr uint32_t MaxFrameBufferCount = 3;

    inline Math::Vector4 GetSizeAndInvSize(float w, float h) 
    {
        return Math::Vector4(w, h, 1.0f / w, 1.0f / h);
    }
}
