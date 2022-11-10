//
// Created by FanyMontpell on 03/09/2021.
//

#define GLOB_MEASURE_TIME 1
#define AI_CONFIG_GLOBAL_SCALE_FACTOR_KEY 1

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "Mesh.h"
#include "TextureUtilities.h"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "imgui.h"
#include "meshoptimizer.h"
#include "assimp/DefaultLogger.hpp"
#include "tracy/Tracy.hpp"
#include "Engine.h"
#include "assimp/ProgressHandler.hpp"
#include <fstream>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <stb/stb_image.h>
#include "Common/interface/BasicMath.hpp"


using namespace Diligent;

class ProgressHandler : public Assimp::ProgressHandler
{
public:

    explicit ProgressHandler(const char* _name) {
        m_id = Engine::instance->addImportProgress(_name);
    };
    ~ProgressHandler() override {
        Engine::instance->removeImportProgress(m_id);
    }

    bool Update(float percentage) override
    {
        Engine::instance->updateImportProgress(m_id, percentage);
        return true;
    }

private:
    uint32_t m_id;
};

Mesh::Mesh(RefCntAutoPtr<IRenderDevice> _device, const char *_path, bool _needsAfterLoadedActions, float3 _position, float _scale, float3 _angle)
: m_path(_path), m_position(_position), m_scale(_scale), m_device(eastl::move(_device)), m_angle(_angle), m_id(idCount++)
{
    ZoneScoped;
    //ZoneScopedN("Loading Mesh");
    ZoneName(m_path.c_str(), m_path.size());
    m_model = float4x4::Scale(m_scale) * float4x4::Translation(m_position);

    auto index = m_path.find_last_of('/') + 1; // +1 to include the /
    m_path = m_path.substr(0, index);

    {
        std::ifstream file(_path);

        if(file.good())
        {
            file.close();
            Assimp::Importer importer;
            ProgressHandler handler(_path);
            importer.SetProgressHandler(&handler);
            const aiScene* scene;
            {
                ZoneNamedN(loading, "Loading File", true);
                scene = importer.ReadFile(_path,
                                          aiProcess_OptimizeMeshes | aiProcess_OptimizeGraph | aiProcess_GenNormals
                                          | aiProcess_GenBoundingBoxes| aiProcess_Triangulate | aiProcess_GlobalScale);

            }

            m_meshes.reserve(scene->mNumMeshes);

            recursivelyLoadNode(scene->mRootNode, scene);

            importer.SetProgressHandler(nullptr);
            importer.FreeScene();
        }
    }

    for(Group& grp : m_meshes)
    {
        BufferDesc VertBuffDesc;
        VertBuffDesc.Name = "Mesh vertex buffer";
        VertBuffDesc.Usage = USAGE_IMMUTABLE;
        VertBuffDesc.BindFlags = BIND_VERTEX_BUFFER;
        VertBuffDesc.Size = sizeof(VertexPacked) * grp.m_vertices.size();
        BufferData VBData;
        VBData.pData    = grp.m_vertices.data();
        VBData.DataSize = grp.m_vertices.size() * sizeof (VertexPacked);// AVOID THIS PLEEEASE

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
    m_executor.silent_async([&](){
      ZoneNamedN(loading, "Loading Vertices and Indices", true);
      ZoneTextV(loading, m_path.c_str(), m_path.size());
      group.m_vertices.reserve(mesh.mNumVertices);
      group.m_indices.reserve(mesh.mNumFaces);

      eastl::vector<Vertex> vertices; // NOT packed
      vertices.reserve(mesh.mNumVertices);
        for(int i = 0; i < mesh.mNumVertices; ++i)
        {
            Vertex v{};

            v.m_position.x = mesh.mVertices[i].x;
            v.m_position.y = mesh.mVertices[i].y;
            v.m_position.z = mesh.mVertices[i].z;
            if(mesh.HasNormals())
            {
                v.m_normal.x = mesh.mNormals[i].x;
                v.m_normal.y = mesh.mNormals[i].y;
                v.m_normal.z = mesh.mNormals[i].z;
            }

            //this query whether we have UV0
            float2 uvs;
            if(mesh.HasTextureCoords(0))
            {
                uvs.x = mesh.mTextureCoords[0][i].x;
                uvs.y = mesh.mTextureCoords[0][i].y;
            }
            else
                //this is called for cerberus as well, it SHOULDN'T
                uvs.x = uvs.y = 0;

            v.m_uv = uvs;

            vertices.emplace_back(v);
        }

      for (int i = 0; i < mesh.mNumFaces; ++i)
      {
          auto& face = mesh.mFaces[i];

          for (int j = 0; j < face.mNumIndices; ++j)
          {
              group.m_indices.emplace_back(face.mIndices[j]);
          }
      }
      ZoneNamedN(optim, "Optimizations", true);
      ZoneTextV(optim, m_path.c_str(), m_path.size());
     size_t index_count = group.m_indices.size();
      eastl::vector<unsigned int> remap(index_count); // allocate temporary memory for the remap table of indices
      size_t vertex_count = meshopt_generateVertexRemap(&remap[0], &group.m_indices[0], index_count, &vertices[0], index_count, sizeof(Vertex));
      eastl::vector<Vertex> verticesToBeRemapped(vertex_count);
      eastl::vector<uint> indicesToBeRemapped(index_count);

      meshopt_remapIndexBuffer(&indicesToBeRemapped[0],  &group.m_indices[0], index_count, &remap[0]);
      meshopt_remapVertexBuffer(&verticesToBeRemapped[0], &vertices[0], index_count, sizeof(Vertex), &remap[0]);

      const size_t oldVertexCount = vertices.size();
      const size_t oldIndexCount = group.m_indices.size();

      vertices = verticesToBeRemapped;
      group.m_indices = indicesToBeRemapped;
#if defined(_DEBUG)
      std::cout << "Previous vertex count " << oldVertexCount << " new vertex count " << vertices.size() << "\n"
      << "Previous indices count " << oldIndexCount << " new indices count " << group.m_indices.size() << "\n"
      << "Improvements: vertex " <<  (float)oldVertexCount / vertices.size() << " index " <<  (float)oldIndexCount / group.m_indices.size() << std::endl;
#endif
      meshopt_optimizeVertexCache(&group.m_indices[0], &group.m_indices[0], index_count, vertex_count);

      meshopt_optimizeOverdraw(&group.m_indices[0], &group.m_indices[0], index_count,(&vertices[0].m_position.x), vertex_count, sizeof(Vertex), 1.05f);
      meshopt_optimizeVertexFetch( &vertices[0], &group.m_indices[0], index_count,  &vertices[0], vertex_count, sizeof(Vertex));

          ZoneNamedN(packing, "Quantization", true);
          ZoneTextV(packing, m_path.c_str(), m_path.size());
          group.m_vertices.reserve(vertices.size());
          for(const auto& v : vertices)
          {
              VertexPacked vertPacked;
              vertPacked.m_position.x = meshopt_quantizeHalf(v.m_position.x);
              vertPacked.m_position.x = meshopt_quantizeHalf(v.m_position.y) |  vertPacked.m_position.x << 16;
              vertPacked.m_position.y = meshopt_quantizeHalf(v.m_position.z);

              // Z = cross(x, y)
              vertPacked.m_normaluv.x = meshopt_quantizeHalf(v.m_normal.x) << 16 | meshopt_quantizeHalf(v.m_normal.y);

              vertPacked.m_normaluv.y = meshopt_quantizeHalf(v.m_uv.x) << 16 | meshopt_quantizeHalf(v.m_uv.y);

              group.m_vertices.emplace_back(vertPacked);
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

void Mesh::drawInspector()
{
    static bool hasChanged = false;
    ImGui::PushID(m_id);
    ImGui::Text("%s", m_path.c_str());
        if(ImGui::DragFloat3("Translation", m_position.Data()))
        {
            hasChanged = true;
        }
        if(ImGui::DragFloat("Scale", &m_scale))
        {
            hasChanged = true;
        }
        if(ImGui::DragFloat3("Rotation", m_angle.Data(), 1.0f, 1.0f, 360.0f))
        {
            hasChanged = true;
            m_angle /= 360.0f;

            m_rotation *= Diligent::Quaternion::RotationFromAxisAngle(float3(1, 0, 0), m_angle.x);
            m_rotation *= Diligent::Quaternion::RotationFromAxisAngle(float3(0, 1, 0), m_angle.y);
            m_rotation *= Diligent::Quaternion::RotationFromAxisAngle(float3(0, 0, 1), m_angle.z);
        }
    ImGui::PopID();
    if(hasChanged)
    {
        //Update matrix
        //todo fsantoro: cache this somewhere -> m_rotation.ToMatrix()
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


void Mesh::setTranslation(Vector3<float>& vector3)
{
    m_position = vector3;
}

void Mesh::setScale(float scale)
{
    m_scale = scale;
}

BoundBox Mesh::getBoundingBox()
{
    BoundBox aabb;
    aabb.Min = float3(eastl::numeric_limits<float>::max());
    aabb.Max = float3(eastl::numeric_limits<float>::min());

    for(auto& mesh : m_meshes)
    {
        BoundBox b = mesh.m_aabb;;
        aabb.Min = std::min(b.Min * m_scale, aabb.Min);
        aabb.Max = std::max(b.Max * m_scale, aabb.Max);
    }

    return aabb;
}
