//
// Created by FanyMontpell on 03/09/2021.
//

#ifndef GRAPHICSPLAYGROUND_MESH_H
#define GRAPHICSPLAYGROUND_MESH_H

#define PLATFORM_WIN32 1

#include <assimp/scene.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/unordered_map.h>
#include "FirstPersonCamera.hpp"
#include "RenderDevice.h"
#include "Common/interface/RefCntAutoPtr.hpp"

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
        eastl::vector<Vertex> m_vertices;
        eastl::vector<uint> m_indices;
        eastl::vector<RefCntAutoPtr<ITexture>> m_textures;

        //This is not really optimal, as the data doesn't need to be both on CPU AND GPU
        RefCntAutoPtr<IBuffer> m_meshVertexBuffer;
        RefCntAutoPtr<IBuffer> m_meshIndexBuffer;
    };

public:
    Mesh(RefCntAutoPtr<IRenderDevice> _device, const char* _path, float3 _position = float3(0), float3 _scale = float3(1)
            , float3 _angle = float3(0.0f));

    Group& getGroup() { return m_meshes[0];}

    void addTexture(const char* _path, int index)
    {
        eastl::string str = _path;
        addTexture(str, index);
    }
    void addTexture(eastl::string& _path, Group& _group);
    void addTexture(eastl::string& _path, int index);

    void draw(RefCntAutoPtr<IDeviceContext> _context, IShaderResourceBinding* _srb, FirstPersonCamera &_camera,
         RefCntAutoPtr<IBuffer> _bufferMatrices,
         eastl::unordered_map<eastl::string, RefCntAutoPtr<ITexture>> &_defaultTextures);

    void drawInspector();

    float4x4& getModel() { return m_model;}

private:
    float4x4 m_model;
    float3 m_position;
    float3 m_scale;
    float3 m_angle;

    Quaternion m_rotation;

    RefCntAutoPtr<IRenderDevice> m_device;

    eastl::string m_path;

    eastl::vector<Group> m_meshes;

    eastl::unordered_map<eastl::string, RefCntAutoPtr<ITexture>> m_texturesLoaded;

    void recursivelyLoadNode(aiNode *pNode, const aiScene *pScene);

    Group loadGroupFrom(const aiMesh& mesh, const aiScene *pScene);
};


#endif //GRAPHICSPLAYGROUND_MESH_H
