//
// Created by FanyMontpell on 03/09/2021.
//

#include "Mesh.h"
#include "TextureUtilities.h"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "imgui.h"
#include "Tracy.hpp"
#include <fstream>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <stb/stb_image.h>

using namespace Diligent;

Mesh::Mesh(RefCntAutoPtr<IRenderDevice> _device, const char *_path,bool _needsAfterLoadedActions, float3 _position, float3 _scale, float3 _angle)
: m_path(_path), m_position(_position), m_scale(_scale), m_device(eastl::move(_device)), m_angle(_angle)
{
    ZoneScopedN("Loading Mesh");
    ZoneText(m_path.c_str(), m_path.size());
    m_model = float4x4::Scale(m_scale) * float4x4::Translation(m_position);

    auto index = m_path.find_last_of('/') + 1; // +1 to include the /
    m_path = m_path.substr(0, index);

    {
        std::ifstream file(_path);

        if(file.good())
        {
            Assimp::Importer importer;
            const aiScene* scene = importer.ReadFile(_path, aiProcessPreset_TargetRealtime_MaxQuality | aiProcess_GenBoundingBoxes);

            m_meshes.reserve(scene->mNumMeshes);

            recursivelyLoadNode(scene->mRootNode, scene);

            importer.FreeScene();
        }
    }

    for(Group& grp : m_meshes)
    {
        BufferDesc VertBuffDesc;
        VertBuffDesc.Name = "Mesh vertex buffer";
        VertBuffDesc.Usage = USAGE_IMMUTABLE;
        VertBuffDesc.BindFlags = BIND_VERTEX_BUFFER;
        VertBuffDesc.Size = sizeof(Vertex) * grp.m_vertices.size();
        BufferData VBData;
        VBData.pData    = grp.m_vertices.data();
        VBData.DataSize = grp.m_vertices.size() * sizeof (Vertex);// AVOID THIS PLEEEASE

        m_device->CreateBuffer(VertBuffDesc, &VBData, &grp.m_meshVertexBuffer);

        BufferDesc IndexBuffDesc;
        IndexBuffDesc.Name = "Mesh vertex buffer";
        IndexBuffDesc.Usage = USAGE_IMMUTABLE;
        IndexBuffDesc.BindFlags = BIND_INDEX_BUFFER;
        IndexBuffDesc.Size =  grp.m_indices.size() * sizeof(uint);
        BufferData IBData;
        IBData.pData    = grp.m_indices.data();
        IBData.DataSize = grp.m_indices.size() * sizeof (uint);// AVOID THIS PLEEEASE

        m_device->CreateBuffer(IndexBuffDesc, &IBData, &grp.m_meshIndexBuffer);
    }

    m_isLoaded = !_needsAfterLoadedActions;
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

      m_executor.silent_async([&](){
          ZoneScopedN("Loading Vertices");

          ZoneText(m_path.c_str(), m_path.size());
            for(int i = 0; i < mesh.mNumVertices; ++i)
            {
                Vertex v{};

                v.m_position.x = mesh.mVertices[i].x;
                v.m_position.y = mesh.mVertices[i].y;
                v.m_position.z = mesh.mVertices[i].z;

                v.m_normal.x = mesh.mNormals[i].x;
                v.m_normal.y = mesh.mNormals[i].y;
                v.m_normal.z = mesh.mNormals[i].z;

                //this query whether we have UV0
                if(mesh.HasTextureCoords(0))
                {
                    v.m_uv.x = mesh.mTextureCoords[0][i].x;
                    v.m_uv.y = mesh.mTextureCoords[0][i].y;
                }
                else
                    //this is called for cerberus as well, it SHOULDN'T
                    v.m_uv.x = v.m_uv.y = 0;

                group.m_vertices.emplace_back(v);
            }
        });

      m_executor.silent_async([&](){
          ZoneScopedN("Loading Indices");
          ZoneText(m_path.c_str(), m_path.size());
            for (int i = 0; i < mesh.mNumFaces; ++i)
            {
                auto& face = mesh.mFaces[i];

                for (int j = 0; j < face.mNumIndices; ++j)
                {
                    group.m_indices.emplace_back(face.mIndices[j]);
                }
            }
        });

      m_executor.silent_async([&](){
          ZoneScopedN("Loading Textures");
          ZoneText(m_path.c_str(), m_path.size());
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
                            eastl::string str = texPath.C_Str();
                            addTexture(str, group);
                        }
                    }
                }
            }
        });

    group.m_aabb.Min = float3(mesh.mAABB.mMin.x, mesh.mAABB.mMin.y, mesh.mAABB.mMin.z);
    group.m_aabb.Max = float3(mesh.mAABB.mMax.x, mesh.mAABB.mMax.y, mesh.mAABB.mMax.z);

    m_executor.wait_for_all();

    return group;
}

void Mesh::addTexture(eastl::string& _path, Group& _group)
{
    eastl::string pathToTex = m_path + _path;

    RefCntAutoPtr<ITexture> tex;

    if(m_texturesLoaded.find(pathToTex) != m_texturesLoaded.end())
    {
        tex = m_texturesLoaded[pathToTex];
        std::cout << "Found the texture... loading " << _path.c_str() << " Tex" << std::endl;
    }
    else
    {
        int width, height, cmps;
        stbi_info(pathToTex.c_str(), &width, &height, &cmps);

        TextureLoadInfo info;
        info.Name = _path.c_str();
        info.IsSRGB = pathToTex.find("_A") != eastl::string::npos;

       // CreateTextureFromFile(pathToTex.c_str(), info, m_device, &tex);

      //  if(!tex)
        {
            TextureDesc desc;
            desc.Name = _path.c_str();
            desc.Height = height;
            desc.Width = width;
            desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
            desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;

            desc.Format = pathToTex.find("_A") != eastl::string::npos ? Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB
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
        std::cout << "Creating " << _path.c_str() << " Tex" << std::endl;
    }

    _group.m_textures.emplace_back(tex);
}

void Mesh::addTexture(eastl::string &_path, int index)
{
    addTexture(_path, m_meshes[index]);
}

void Mesh::draw(RefCntAutoPtr<IDeviceContext> _context, IShaderResourceBinding* _srb, FirstPersonCamera& _camera
, RefCntAutoPtr<IBuffer> _bufferMatrices, eastl::unordered_map<eastl::string, RefCntAutoPtr<ITexture>>& _defaultTextures)
{

    {
        // Map the buffer and write current world-view-projection matrix
        MapHelper<float4x4> CBConstants(_context, _bufferMatrices, MAP_WRITE, MAP_FLAG_DISCARD);
        *CBConstants = (m_model * _camera.GetViewMatrix() * _camera.GetProjMatrix()).Transpose();
    }


    for (Group& grp : m_meshes)
    {
        if(grp.m_textures.empty())
        {
            if(isTransparent())
            {
                _srb->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureAlbedo")->
                        Set(_defaultTextures["redTransparent"]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
            }
            else
            {
                _srb->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureAlbedo")->
                        Set(_defaultTextures["albedo"]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
            }

            if(auto* pVar = _srb->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureNormal"))
            {
                pVar->Set(_defaultTextures["normal"]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
            }
        }
        else
        {
            _srb->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureAlbedo")->
                    Set(grp.m_textures[0]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));

            if(grp.m_textures.size() > 1)
            {
                _srb->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureNormal")->
                        Set(grp.m_textures[1]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
            }
        }

        Uint64  offset = 0;
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

void Mesh::drawInspector()
{
    static bool hasChanged = false;
    ImGui::PushID(m_path.c_str());
    ImGui::Text("%s", m_path.c_str());
        if(ImGui::DragFloat3("Translation", m_position.Data()))
        {
            hasChanged = true;
        }
        if(ImGui::DragFloat3("Scale", m_scale.Data()))
        {
            hasChanged = true;
        }
        if(ImGui::DragFloat3("Rotation", m_angle.Data()))
        {
            hasChanged = true;
            //todo: UPDATE QUATERNION
        }
    ImGui::PopID();
    if(hasChanged)
    {
        //Update matrix
        m_model = float4x4::Scale(m_scale) * m_rotation.ToMatrix() * float4x4::Translation(m_position);
    }
}

bool Mesh::isTransparent() const
{
    return m_isTransparent;
}

void Mesh::setTransparent(bool mIsTransparent)
{
    m_isTransparent = mIsTransparent;
}

bool Mesh::isClicked(const float4x4& _mvp, const float3 &RayOrigin, const float3 &RayDirection, float &EnterDist,
                     float &ExitDist)
{
    for( auto& grp : m_meshes)
    {
        const BoundBox box = grp.m_aabb.Transform(m_model);
        if(IntersectRayAABB(RayOrigin, RayDirection, box, EnterDist, ExitDist))
            return true;
    }

    return false;
}
