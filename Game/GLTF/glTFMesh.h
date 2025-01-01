#pragma once
#include "CoreMinimal.h"
#include "TextureManager.h"
#include "glm/glm.hpp"

namespace MyDirectX
{
    class Texture;
}

namespace glTF
{
    enum class EAlphaMode : uint8_t
    {
        Opaque,
        Mask,
        Transparent,
    };

    struct BaseMaterial
    {
        using Texture = MyDirectX::Texture;
        using EDefaultTexture = MyDirectX::EDefaultTexture;

        enum ETextureType : uint32_t
        {
            BaseColor,
            MetallicRoughness,
            Normal,
            Emissive,
            Occlusion,

            Count,
        };

        static constexpr uint32_t kTextureNum = 5;
        static constexpr EDefaultTexture kDefaultTexture[ETextureType::Count] = {
            EDefaultTexture::kMagenta2D,
            EDefaultTexture::kWhiteOpaque2D,
            EDefaultTexture::kDefaultNormalMap,
            EDefaultTexture::kBlackOpaque2D,
            EDefaultTexture::kWhiteOpaque2D
        };
        static constexpr uint32_t kInvalidStart = 0x00010000;
        static bool IsTextureValid(uint32_t index);
        static uint32_t EncodeDefaultTexture(EDefaultTexture texture);
        static EDefaultTexture DecodeDefaultTexture(uint32_t index);
        static uint32_t DecodeDefaultTextureIndex(uint32_t index);

        std::string name;
        EAlphaMode alphaMode = EAlphaMode::Opaque;
        float alphaCutoff = 0.5f;
        bool bDoubleSided = false;

#if 0
        const Texture *baseColorTex = nullptr;
        const Texture *metallicRoughnessTex = nullptr;
        const Texture *normalTex = nullptr;
        const Texture *occlusionTex = nullptr;
        const Texture *emissiveTex = nullptr;
#elif 0
        const Texture* textures[ETextureType::Count] = {};
#else
        // TODO: optimize: use 'start + count' 
        uint32_t textures[ETextureType::Count] = {};
#endif
        void ResetDefault();
    };
        
    struct Vertex
    {
        glm::vec3 p;
#if USE_VERTEX_COMPRESSION
        glm::u8vec4 n;
        glm::u16vec2 uv;
#else
        glm::vec3 n;
        glm::vec2 uv;
#endif
    };
    extern std::vector<D3D12_INPUT_ELEMENT_DESC> kInputElements;

    struct BatchElement
    {
        uint32_t vertexOffset;
        uint32_t vertexCount;
        uint32_t indexOffset;
        uint32_t indexCount;
        uint32_t materialIndex;
    };
	
    struct MeshBatch
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        // std::vector<uint32_t> meshletData;
        // std::vector<Meshlet> meshlets;

        std::vector<BatchElement> batchElements;
    };

    // Instance into one MeshBatch's batchElements
    struct MeshInstance
    {
        glm::mat4 transform;
        uint32_t elementIndex;
        // TODO: duplicate
        uint32_t materialIndex;
    };

    struct DrawObject
    {
        std::string name;
        
        MeshBatch mesh;
        std::vector<MeshInstance> instances;
        // TODO: put materials elsewhere ???
        std::vector<const MyDirectX::ManagedTexture*> textures;
        std::vector<BaseMaterial> materials;
    };
}
