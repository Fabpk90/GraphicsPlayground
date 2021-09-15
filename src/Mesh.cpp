//
// Created by FanyMontpell on 03/09/2021.
//

#include "Mesh.h"
#include "Engine.h"
#include <fstream>
#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <diligent/include/TextureLoader/interface/TextureLoader.h>
#include <diligent/include/TextureLoader/interface/TextureUtilities.h>
#include <stb/stb_image.h>

using namespace Diligent;

Mesh::Mesh(const char *_path, float3 _position, float3 _scale)
: m_path(_path)
{
    auto index = m_path.find_last_of('/') + 1; // +1 to include the /
    m_path = m_path.substr(0, index);

    std::ifstream file(_path);

    if(!file.bad())
    {
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(_path, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals);

        m_meshes.reserve(scene->mNumMeshes);

        recursivelyLoadNode(scene->mRootNode, scene);

        importer.FreeScene();
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
    RefCntAutoPtr<ITexture> tex;
    std::string pathToTex = m_path + _path;

    int width, height, nrChannels;
    unsigned char *data = stbi_load(pathToTex.c_str(), &width, &height, &nrChannels, 0);

    if(data)
    {
        TextureDesc texDesc;
        texDesc.Name = _path.c_str();
        texDesc.Width = width;
        texDesc.Height = height;
        texDesc.Type = Diligent::RESOURCE_DIM_TEX_2D;
        texDesc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
        texDesc.Usage = Diligent::USAGE_IMMUTABLE;
        texDesc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB;

        TextureSubResData resData;
        resData.pData = data;

        TextureData initData;
        initData.pSubResources = &resData;
        initData.NumSubresources = 1;

        Engine::instance->getDevice()->CreateTexture(texDesc, &initData, &tex);

        _group.m_textures.emplace_back(tex);

        std::cout << "Creating " << _path << " Tex" << std::endl;
    }

    stbi_image_free(data);
}

void Mesh::addTexture(std::string &_path, int index)
{
    addTexture(_path, m_meshes[index]);
}
