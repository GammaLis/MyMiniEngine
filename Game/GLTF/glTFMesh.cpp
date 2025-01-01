#include "glTFMesh.h"
#include "Graphics.h"
#include "TextureManager.h"

namespace glTF
{
    using namespace MyDirectX;

    bool BaseMaterial::IsTextureValid(uint32_t index)
    {
        return index < kInvalidStart;
    }

    uint32_t BaseMaterial::EncodeDefaultTexture(EDefaultTexture texture)
    {
        return static_cast<uint32_t>(texture) + kInvalidStart;
    }

    EDefaultTexture BaseMaterial::DecodeDefaultTexture(uint32_t index)
    {
        return static_cast<EDefaultTexture>(DecodeDefaultTextureIndex(index));
    }

    uint32_t BaseMaterial::DecodeDefaultTextureIndex(uint32_t index)
    {
        ASSERT(index >= kInvalidStart && index < kInvalidStart + static_cast<uint32_t>(EDefaultTexture::kNumDefaultTextures));
        return index - kInvalidStart;
    }
    
    void BaseMaterial::ResetDefault()
    {
        for (uint32_t i = 0; i < ETextureType::Count; i++)
        {
            textures[i] = EncodeDefaultTexture(kDefaultTexture[i]);
        }
    }
    
    std::vector<D3D12_INPUT_ELEMENT_DESC> kInputElements =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA },
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA },
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA },
    };
}
