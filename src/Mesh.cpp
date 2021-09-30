//
// Created by FanyMontpell on 03/09/2021.
//

#include "Mesh.h"
#include <fstream>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <diligent/include/TextureLoader/interface/TextureLoader.h>
#include <diligent/include/TextureLoader/interface/TextureUtilities.h>
#include <stb/stb_image.h>
#include <diligent/include/Graphics/GraphicsTools/interface/MapHelper.hpp>

using namespace Diligent;

Mesh::Mesh(RefCntAutoPtr<IRenderDevice> _device, const char *_path, float3 _position, float3 _scale)
: m_path(_path), m_position(_position), m_scale(_scale), m_device(_device)
{
    m_model = float4x4::Scale(m_scale) * float4x4::Translation(m_position);

    auto index = m_path.find_last_of('/') + 1; // +1 to include the /
    m_path = m_path.substr(0, index);

    std::ifstream file(_path);

    if(file.good())
    {
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(_path, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals);

        m_meshes.reserve(scene->mNumMeshes);

        recursivelyLoadNode(scene->mRootNode, scene);

        importer.FreeScene();
    }

    for(Group& grp : m_meshes)
    {
        BufferDesc VertBuffDesc;
        VertBuffDesc.Name          = "Mesh vertex buffer";
        VertBuffDesc.Usage         = USAGE_IMMUTABLE;
        VertBuffDesc.BindFlags     = BIND_VERTEX_BUFFER;
        VertBuffDesc.uiSizeInBytes          = grp.m_vertices.size() * sizeof(Vertex);
        BufferData VBData;
        VBData.pData    = grp.m_vertices.data();
        VBData.DataSize = grp.m_vertices.size() * sizeof (Vertex);// AVOID THIS PLEEEASE

        m_device->CreateBuffer(VertBuffDesc, &VBData, &grp.m_meshVertexBuffer);

        BufferDesc IndexBuffDesc;
        IndexBuffDesc.Name          = "Mesh vertex buffer";
        IndexBuffDesc.Usage         = USAGE_IMMUTABLE;
        IndexBuffDesc.BindFlags     = BIND_INDEX_BUFFER;
        IndexBuffDesc.uiSizeInBytes          =  grp.m_indices.size() * sizeof(uint);
        BufferData IBData;
        IBData.pData    = grp.m_indices.data();
        IBData.DataSize = grp.m_indices.size() * sizeof (uint);// AVOID THIS PLEEEASE

        m_device->CreateBuffer(IndexBuffDesc, &IBData, &grp.m_meshIndexBuffer);
    }


}

void Mesh::recursivelyLoadNode(aiNode *pNode, const aiScene *pScene)
{
    for(int i = 0; i < pNode->mNumMeshes; i++)
    {
        m_meshes.emplace_back(loadGroupFrom(*pScene->mMeshes[pNode->mMeshes[i]], pScene));
    }

    for(int i = 0; i < pNode->mNumChildren; i++)
    {
        recursivelyLoadNode(pNode->mChildren[i], pScene);
    }
}


Mesh::Group Mesh::loadGroupFrom(const aiMesh& mesh, const aiScene *pScene)
{
    Group group;

    group.m_vertices.reserve(mesh.mNumVertices);
    group.m_indices.reserve(mesh.mNumFaces);

    for(int i = 0; i < mesh.mNumVertices; ++i)
    {
        Vertex v{};

        v.m_position.x = mesh.mVertices[i].x;
        v.m_position.y = mesh.mVertices[i].y;
        v.m_position.z = mesh.mVertices[i].z;

        v.m_normal.x = mesh.mNormals[i].x;
        v.m_normal.y = mesh.mNormals[i].y;
        v.m_normal.z = mesh.mNormals[i].z;

        v.m_uv.x = mesh.mTextureCoords[0][i].x;
        v.m_uv.y = mesh.mTextureCoords[0][i].y;

        group.m_vertices.emplace_back(v);
    }

    for (int i = 0; i < mesh.mNumFaces; ++i)
    {
        auto& face = mesh.mFaces[i];

        for (int j = 0; j < face.mNumIndices; ++j)
        {
            group.m_indices.emplace_back(face.mIndices[j]);
        }
    }

    //we handle only one material per mesh for now
    if(mesh.mMaterialIndex >= 0)
    {
        auto mat = pScene->mMaterials[mesh.mMaterialIndex];

        for(int typeID = 1; typeID < aiTextureType_UNKNOWN; ++typeID)
        {
            auto type = static_cast<aiTextureType>(typeID);

            for (int i = 0; i < mat->GetTextureCount(type); ++i)
            {
                aiString texPath;

                if(mat->GetTexture(type, i, &texPath) == aiReturn_SUCCESS)
                {
                    std::string str = texPath.C_Str();
                    addTexture(str, group);
                }
            }
        }
    }

    return group;
}

void Mesh::addTexture(std::string& _path, Group& _group)
{
    std::string pathToTex = m_path + _path;

    RefCntAutoPtr<ITexture> tex;

    if(m_texturesLoaded.find(pathToTex) != m_texturesLoaded.end())
    {
        tex = m_texturesLoaded[pathToTex];
        std::cout << "Loading " << _path << " Tex" << std::endl;
    }
    else
    {
        int width, height, cmps;
        stbi_info(pathToTex.c_str(), &width, &height, &cmps);

        TextureLoadInfo info;
        info.Name = _path.c_str();
        info.IsSRGB = pathToTex.find("_A") != std::string::npos;

        CreateTextureFromFile(pathToTex.c_str(), info, m_device, &tex);

        if(!tex)
        {
            TextureDesc desc;
            desc.Name = _path.c_str();
            desc.Height = height;
            desc.Width = width;
            desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
            desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;

            desc.Format = pathToTex.find("_A") != std::string::npos ? Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB
                    : Diligent::TEX_FORMAT_RGBA8_UNORM;

            int x, y, chnls;
            auto* data = stbi_load(pathToTex.c_str(), &x, &y, &chnls, 4);
            TextureSubResData subResData;
            subResData.pData = data;
            subResData.Stride = sizeof(char) * 4 * width;

            TextureData textureData;
            textureData.NumSubresources = 1;
            textureData.pSubResources = &subResData;

            m_device->CreateTexture(desc, &textureData, &tex);

            stbi_image_free(data);
        }

        m_texturesLoaded[pathToTex] = tex;
        std::cout << "Creating " << _path << " Tex" << std::endl;
    }

    _group.m_textures.emplace_back(tex);
}

void Mesh::addTexture(std::string &_path, int index)
{
    addTexture(_path, m_meshes[index]);
}

void Mesh::draw(RefCntAutoPtr<IDeviceContext> _context, RefCntAutoPtr<IShaderResourceBinding> _srb, FirstPersonCamera& _camera
, RefCntAutoPtr<IBuffer>& _bufferMatrices)
{

    // Map the buffer and write current world-view-projection matrix
    MapHelper<float4x4> CBConstants(_context, _bufferMatrices, MAP_WRITE, MAP_FLAG_DISCARD);
    *CBConstants = (m_model * _camera.GetViewMatrix() * _camera.GetProjMatrix()).Transpose();

    for (Group& grp : m_meshes)
    {
        _srb->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureAlbedo")->
                Set(grp.m_textures[0]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));

        _srb->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureNormal")->
                Set(grp.m_textures[1]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));

        Uint32   offset   = 0;
        IBuffer* pBuffs[] = {grp.m_meshVertexBuffer};
        _context->SetVertexBuffers(0, 1, pBuffs, &offset, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                             SET_VERTEX_BUFFERS_FLAG_RESET);
        _context->SetIndexBuffer(grp.m_meshIndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);


        _context->CommitShaderResources(_srb, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);


        DrawIndexedAttribs DrawAttrs; // This is an indexed draw call
        DrawAttrs.IndexType  = VT_UINT32; // Index type
        DrawAttrs.NumIndices = grp.m_indices.size();
        // Verify the state of vertex and index buffers as well as consistence of
        // render targets and correctness of draw command arguments
        DrawAttrs.Flags = DRAW_FLAG_VERIFY_ALL;
        _context->DrawIndexed(DrawAttrs);
    }
}
