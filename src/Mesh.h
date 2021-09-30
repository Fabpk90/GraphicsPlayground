//
// Created by FanyMontpell on 03/09/2021.
//

#ifndef GRAPHICSPLAYGROUND_MESH_H
#define GRAPHICSPLAYGROUND_MESH_H

#define PLATFORM_WIN32 1

#include <diligent/include/Common/interface/BasicMath.hpp>
#include <assimp/scene.h>
#include <diligent/include/Graphics/GraphicsEngine/interface/Texture.h>
#include <diligent/include/Common/interface/RefCntAutoPtr.hpp>
#include <diligent/include/Graphics/GraphicsEngine/interface/ShaderResourceBinding.h>
#include <diligent/include/Graphics/GraphicsEngine/interface/RenderDevice.h>
#include "FirstPersonCamera.hpp"

using namespace Diligent;

struct Vertex
{
    float3 m_position;
    float3 m_normal;
    float2 m_uv;
};

class Mesh {
    struct Group
    {
        std::vector<Vertex> m_vertices;
        std::vector<uint> m_indices;
        std::vector<RefCntAutoPtr<ITexture>> m_textures;

        //This is not really optimal, as the data doesn't need to be both on CPU AND GPU
        RefCntAutoPtr<IBuffer> m_meshVertexBuffer;
        RefCntAutoPtr<IBuffer> m_meshIndexBuffer;
    };

public:
    Mesh(RefCntAutoPtr<IRenderDevice> _device, const char* _path, float3 _position = float3(0), float3 _scale = float3(1));

    Group& getGroup() { return m_meshes[0];}

    void addTexture(const char* _path, int index)
    {
        std::string str = _path;
        addTexture(str, index);
    }
    void addTexture(std::string& _path, Group& _group);
    void addTexture(std::string& _path, int index);

    void draw(RefCntAutoPtr<IDeviceContext> _context, RefCntAutoPtr<IShaderResourceBinding> _srb, FirstPersonCamera& _camera,
              RefCntAutoPtr<IBuffer>& _bufferMatrices);

    float4x4& getModel() { return m_model;}

private:
    float4x4 m_model;
    float3 m_position;
    float3 m_scale;

    RefCntAutoPtr<IRenderDevice> m_device;

    std::string m_path;

    std::vector<Group> m_meshes;

    std::unordered_map<std::string, RefCntAutoPtr<ITexture>> m_texturesLoaded;

    void recursivelyLoadNode(aiNode *pNode, const aiScene *pScene);

    Group loadGroupFrom(const aiMesh& mesh, const aiScene *pScene);
};


#endif //GRAPHICSPLAYGROUND_MESH_H
