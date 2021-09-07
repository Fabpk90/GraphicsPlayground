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
    };

public:
    Mesh(const char* _path, float3 _position = float3(0), float3 _scale = float3(1));

    const Group& getGroup() { return m_meshes[0];}

private:
    float4x4 m_model;
    float3 m_position;
    float3 m_scale;

    std::string m_path;

    std::vector<Group> m_meshes;

    void recursivelyLoadNode(aiNode *pNode, const aiScene *pScene);

    Group loadGroupFrom(const aiMesh& mesh, const aiScene *pScene);
};


#endif //GRAPHICSPLAYGROUND_MESH_H
