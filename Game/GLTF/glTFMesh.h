#pragma once
#include "CoreMinimal.h"
#include "glm/glm.hpp"

namespace glTF
{
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
        MeshBatch mesh;
        std::vector<MeshInstance> instances;
    };
}
