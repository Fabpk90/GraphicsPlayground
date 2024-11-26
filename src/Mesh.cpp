//
// Created by FanyMontpell on 03/09/2021.
//
#define FORCE_LOADING_FROM_DISK 0

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "Mesh.h"
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
#include <filesystem>
#include "Common/interface/BasicMath.hpp"
#include "../bin/flatbuffers/generated/mesh_generated.h"


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
: m_position(_position), m_scale(_scale), m_device(eastl::move(_device)), m_angle(_angle), m_id(idCount++)
{
    ZoneScoped;
    //ZoneScopedN("Loading Mesh");
    eastl::string path(_path);
    ZoneName(path.c_str(), path.size());
    m_model = float4x4::Scale(m_scale) * float4x4::Translation(m_position);

    auto index = path.find_last_of('.');
    auto nameWithoutExt = path.substr(0, index);
    m_name = nameWithoutExt;
    m_flatbufferPath = nameWithoutExt + ".mesh";
    index = path.find_last_of('/') + 1; // +1 to include the /
    m_basePath = path.substr(0, index);


    {
        std::ifstream filefbs(m_flatbufferPath.c_str(), std::ios_base::binary);
#if FORCE_LOADING_FROM_DISK == 1
        if(false)
#else
        if(filefbs.good())
#endif
        {
            filefbs.seekg(0, std::ios::end);
            size_t length = filefbs.tellg();
            filefbs.seekg(0, std::ios::beg);

            eastl::vector<char> buffer(length);
            filefbs.read(buffer.data(), length);

            auto staticMesh = FlatBuffers::GetStaticMesh(buffer.data());
            auto meshes = staticMesh->meshes();

            if(meshes->Get(0)->version() != VERSION)
            {
                LoadFromPath(_path);
                save();
            }
            else
            {
                std::cout << "loading " << _path << " from flatbuffers" << std::endl;


                const unsigned int o = meshes->size();
                m_meshes.resize(o);

                for(int i = 0; i < meshes->size(); ++i)
                {
                    auto mesh = meshes->Get(i);

                    auto& verticesUnpacked = m_meshes[i].m_verticesPosRaytrace;
                    verticesUnpacked.resize(mesh->vertex_unpacked()->size());

                    auto& indicesUnpacked = m_meshes[i].m_indicesRaytrace;
                    indicesUnpacked.resize(mesh->indices_unpacked()->size());

                    auto& vertices = m_meshes[i].m_vertices;
                    vertices.resize(mesh->vertex()->size());

                    auto& indices = m_meshes[i].m_indices;
                    indices.resize(mesh->indices()->size());

                    memcpy_s(verticesUnpacked.data(),mesh->vertex_unpacked()->size() * sizeof(float3), mesh->vertex_unpacked()->data(), mesh->vertex_unpacked()->size() * sizeof(float3));
                    memcpy_s(indicesUnpacked.data(),mesh->indices_unpacked()->size() * sizeof(uint32_t), mesh->indices_unpacked()->data(), mesh->indices_unpacked()->size() * sizeof(uint32_t));
                    memcpy_s(vertices.data(),mesh->vertex()->size() * sizeof(VertexPacked), mesh->vertex()->data(), mesh->vertex()->size() * sizeof(VertexPacked));
                    memcpy_s(indices.data(),mesh->indices()->size() * sizeof(uint16_t), mesh->indices()->data(), mesh->indices()->size()* sizeof(uint16_t));

                    m_meshes[i].m_aabb.Min = float3(mesh->aabb_min()->x(), mesh->aabb_min()->y(), mesh->aabb_min()->z());
                    m_meshes[i].m_aabb.Max = float3(mesh->aabb_max()->x(), mesh->aabb_max()->y(), mesh->aabb_max()->z());

                    for (int indexTexture = 0; indexTexture < mesh->textures()->size(); ++indexTexture)
                    {
                        auto texture = mesh->textures()->Get(indexTexture);
                        TextureDesc desc;
                        desc.Width = texture->dims()->x();
                        desc.Height = texture->dims()->y();
                        desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
                        desc.Name = texture->name()->c_str();
                        TEXTURE_FORMAT format;
                        int stride = 0;
                        switch (texture->type())
                        {
                            case FlatBuffers::TextureType_Albedo:
                                format = TEX_FORMAT_RGBA8_UNORM_SRGB;
                                stride = 4;
                                break;
                            case FlatBuffers::TextureType_Normal:
                                format = TEX_FORMAT_RGBA8_UNORM;
                                stride = 4;
                                break;
                            case FlatBuffers::TextureType_Roughness:
                                format = TEX_FORMAT_R8_SNORM;
                                stride = 1;
                                break;
                        }
                        desc.Format = format;
                        desc.BindFlags = BIND_SHADER_RESOURCE;

                        TextureSubResData subData;
                        subData.pData = texture->data();
                        subData.Stride = stride * sizeof (char) * desc.Width;

                        TextureData texData;
                        texData.NumSubresources = 1;
                        texData.pSubResources = &subData;
                        //todo fsantoro handle caching textures when loading from flatbuffers
                        RefCntAutoPtr<ITexture> tex;
                        m_device->CreateTexture(desc, &texData, &tex);

                        m_meshes[i].m_textures.push_back(tex);
                    }

                    m_meshes[i].m_name = mesh->name()->c_str();
                }

                m_scale = staticMesh->scale();
            }
        }
        else
        {
            LoadFromPath(_path);
            save();
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
        IndexBuffDesc.Name = "Mesh index buffer";
        IndexBuffDesc.Usage = USAGE_IMMUTABLE;
        IndexBuffDesc.BindFlags = BIND_INDEX_BUFFER;
        IndexBuffDesc.Size =  grp.m_indices.size() * sizeof(uint16_t);
        BufferData IBData;
        IBData.pData    = grp.m_indices.data();
        IBData.DataSize = grp.m_indices.size() * sizeof (uint16_t);// AVOID THIS PLEEEASE

        m_device->CreateBuffer(IndexBuffDesc, &IBData, &grp.m_meshIndexBuffer);
    }

    m_isLoaded = !_needsAfterLoadedActions;
}

void Mesh::LoadFromPath(const char *_path)
{
    std::ifstream file(_path);

    if(file.good())
    {
        file.close();

        Assimp::Importer importer;

        importer.SetPropertyInteger(AI_CONFIG_PP_SLM_VERTEX_LIMIT, 65534); // splitting to have only uint16 indices

        ProgressHandler handler(_path);
        importer.SetProgressHandler(&handler);
        const aiScene* scene;
        {
            ZoneNamedN(loading, "Loading File", true);

            uint32_t flags =  aiProcess_SortByPType | aiProcess_GenUVCoords | aiProcess_Triangulate | aiProcess_CalcTangentSpace | aiProcess_SplitLargeMeshes | aiProcess_OptimizeMeshes | aiProcess_OptimizeGraph | aiProcess_GenSmoothNormals
                              | aiProcess_GenBoundingBoxes  | aiProcess_GlobalScale;

            assert(importer.ValidateFlags(flags));
            scene = importer.ReadFile(_path, flags);
            importer.ApplyPostProcessing(aiProcess_CalcTangentSpace);
        }

        m_meshes.reserve(scene->mNumMeshes);

        recursivelyLoadNode(scene->mRootNode, scene);

        importer.SetProgressHandler(nullptr);
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

    m_executor.silent_async([&](){
        ZoneScopedN("Loading Textures");
        ZoneText(m_basePath.c_str(), m_basePath.size());
        //we handle only one material per mesh for now
        if(mesh.mMaterialIndex > 0)

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
      ZoneTextV(loading, m_basePath.c_str(), m_basePath.size());

      eastl::vector<Vertex> vertices; // NOT packed
      vertices.reserve(mesh.mNumVertices);
        for(int i = 0; i < mesh.mNumVertices; ++i)
        {
            Vertex v{};

            v.m_position.x = mesh.mVertices[i].x;
            v.m_position.y = mesh.mVertices[i].y;
            v.m_position.z = mesh.mVertices[i].z;

            group.m_verticesPosRaytrace.emplace_back(v.m_position);
            if(mesh.HasNormals())
            {
                v.m_normal.x = mesh.mNormals[i].x;
                v.m_normal.y = mesh.mNormals[i].y;
                v.m_normal.z = mesh.mNormals[i].z;

                if(!mesh.HasTangentsAndBitangents())
                    assert(false);
                else
                {
                    v.m_tangent.x = mesh.mTangents[i].x;
                    v.m_tangent.y = mesh.mTangents[i].y;
                    v.m_tangent.z = mesh.mTangents[i].z;
                }
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
              group.m_indicesRaytrace.emplace_back(face.mIndices[j]);
              group.m_indices.emplace_back(face.mIndices[j]);
          }
      }

      ZoneNamedN(optim, "Optimizations", true);
      ZoneTextV(optim, m_basePath.c_str(), m_basePath.size());
     size_t index_count = group.m_indices.size();
      eastl::vector<unsigned int> remap(index_count); // allocate temporary memory for the remap table of indices
      size_t vertex_count = meshopt_generateVertexRemap(&remap[0], &group.m_indices[0], index_count, &vertices[0], index_count, sizeof(Vertex));
      eastl::vector<Vertex> verticesToBeRemapped(vertex_count);
      eastl::vector<uint16_t> indicesToBeRemapped(index_count);

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
          ZoneTextV(packing, m_basePath.c_str(), m_basePath.size());
          group.m_vertices.reserve(vertices.size());
          for(const auto& v : vertices)
          {
              VertexPacked vertPacked;
              vertPacked.m_position.x = meshopt_quantizeHalf(v.m_position.x);
              vertPacked.m_position.x = meshopt_quantizeHalf(v.m_position.y) | vertPacked.m_position.x << 16;
              vertPacked.m_position.y = meshopt_quantizeHalf(v.m_position.z);

              // Z = cross(x, y)
              vertPacked.m_normaluv.x = meshopt_quantizeHalf(v.m_normal.x) << 16 | meshopt_quantizeHalf(v.m_normal.y);

              vertPacked.m_normaluv.y = meshopt_quantizeHalf(v.m_uv.x) << 16 | meshopt_quantizeHalf(v.m_uv.y);

              vertPacked.m_tangent = meshopt_quantizeHalf(v.m_tangent.x) << 16 | meshopt_quantizeHalf(v.m_tangent.y);

              group.m_vertices.emplace_back(vertPacked);
          }
    });

    group.m_aabb.Min = float3(mesh.mAABB.mMin.x, mesh.mAABB.mMin.y, mesh.mAABB.mMin.z);
    group.m_aabb.Max = float3(mesh.mAABB.mMax.x, mesh.mAABB.mMax.y, mesh.mAABB.mMax.z);

    group.m_name = mesh.mName.C_Str();

    m_executor.wait_for_all();

    return group;
}

void Mesh::addTexture(eastl::string& _path, Group& _group)
{
    auto pos = _path.find('\\');
    if( pos != eastl::string::npos)
    {
        _path[pos] = '/';
    }
    eastl::string pathToTex = m_basePath + _path;

    RefCntAutoPtr<ITexture> tex;

    /*if(m_texturesLoaded.find(pathToTex) != m_texturesLoaded.end())
    {
        tex = m_texturesLoaded[pathToTex];
        std::cout << "Found the texture... loading " << _path.c_str() << " Tex" << std::endl;
    }
    else*/
    {
        int width, height, cmps;
        stbi_info(pathToTex.c_str(), &width, &height, &cmps);

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
            auto* data = stbi_load(pathToTex.c_str(), &width, &height, &chnls, 4);
            TextureSubResData subResData;
            subResData.pData = data;
            subResData.Stride = sizeof(unsigned char) * 4 * width;

            TextureData textureData;
            textureData.NumSubresources = 1;
            textureData.pSubResources = &subResData;

            m_device->CreateTexture(desc, &textureData, &tex);

            auto* dataSaved = new unsigned char[height * width * 4];
            memcpy_s(dataSaved, sizeof(unsigned char) * width * height * 4, data, sizeof(unsigned char) * width * height * 4);
            _group.m_texturesData.push_back(dataSaved);

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
    ImGui::Text("%s", m_basePath.c_str());
        if(ImGui::DragFloat3("Translation", m_position.Data()))
        {
            hasChanged = true;
        }
        if(ImGui::DragFloat("Scale", &m_scale))
        {
            hasChanged = true;
        }
        if(ImGui::DragFloat3("Rotation", m_angle.Data(), 1.0f, 1.0f))
        {
            hasChanged = true;
            float3 angles = m_angle * (PI / 180.0f); // TODO fsantoro: add this as an helper (DegreesToRadians)

            m_rotation = Diligent::Quaternion<float>::RotationFromAxisAngle(float3(1, 0, 0), angles.x);
            m_rotation *= Diligent::Quaternion<float>::RotationFromAxisAngle(float3(0, 1, 0), angles.y);
            m_rotation *= Diligent::Quaternion<float>::RotationFromAxisAngle(float3(0, 0, 1), angles.z);
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

//TODO fsantoro: refactor this to use the same code as the one used on lighting

bool Mesh::isClicked(const float4x4& _mvp, const float3 &RayOrigin, const float3 &RayDirection, float &EnterDist,
                     float &ExitDist)
{
    for(const auto& grp : m_meshes)
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
        BoundBox b = mesh.m_aabb;
        aabb.Min = std::min(b.Min * m_scale, aabb.Min);
        aabb.Max = std::max(b.Max * m_scale, aabb.Max);
    }

    return aabb;
}

void Mesh::save()
{
    flatbuffers::FlatBufferBuilder builder(m_meshes[0].m_vertices.size() * sizeof(VertexPacked));
    eastl::vector<flatbuffers::Offset<FlatBuffers::Mesh>> meshesfbs;
    FlatBuffers::Vec3 aabbMinStaticMesh(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
    FlatBuffers::Vec3 aabbMaxStaticMesh(std::numeric_limits<float>::min(), std::numeric_limits<float>::min(), std::numeric_limits<float>::min());

    for (auto & mesh : m_meshes)
    {
        auto vecVerticesUnpacked = builder.CreateVectorOfStructs(reinterpret_cast<FlatBuffers::Vertex*>(mesh.m_verticesPosRaytrace.data()), mesh.m_verticesPosRaytrace.size());
        auto vecIndicesUnpacked = builder.CreateVector(mesh.m_indicesRaytrace.data(), mesh.m_indicesRaytrace.size());
        auto vecVertices = builder.CreateVectorOfStructs(reinterpret_cast<FlatBuffers::VertexPacked*>(mesh.m_vertices.data()), mesh.m_vertices.size());
        auto vecIndices = builder.CreateVector(mesh.m_indices.data(), mesh.m_indices.size());
        auto vecTexture = eastl::vector<flatbuffers::Offset<FlatBuffers::Texture>>();
        for (int texIndex = 0; texIndex < mesh.m_textures.size(); ++texIndex)
        {
            auto& texture = mesh.m_textures[texIndex];
            auto texDesc = texture->GetDesc();

            auto vecTexData = builder.CreateVector(mesh.m_texturesData[texIndex], texDesc.Width * texDesc.Height * 4);
            auto nameTex = builder.CreateString(texDesc.Name);
            auto texType = texDesc.Format == TEX_FORMAT_RGBA8_UNORM_SRGB ? FlatBuffers::TextureType::TextureType_Albedo :FlatBuffers::TextureType::TextureType_Normal;
            auto dims = FlatBuffers::uint2(texDesc.Width, texDesc.Height);

            auto textureFbs = FlatBuffers::TextureBuilder(builder);
            textureFbs.add_data(vecTexData);
            textureFbs.add_name(nameTex);
            textureFbs.add_type(texType);
            textureFbs.add_dims(&dims);

            vecTexture.push_back(textureFbs.Finish());
        }

        auto vecTexFbs = builder.CreateVector(vecTexture.data(), vecTexture.size());
        auto aabbMin = FlatBuffers::Vec3(mesh.m_aabb.Min.x, mesh.m_aabb.Min.y, mesh.m_aabb.Min.z);
        auto aabbMax = FlatBuffers::Vec3(mesh.m_aabb.Max.x, mesh.m_aabb.Max.y, mesh.m_aabb.Max.z);
        auto nameFbs = builder.CreateString(mesh.m_name.c_str());
        auto meshFbs = FlatBuffers::MeshBuilder(builder);
        meshFbs.add_textures(vecTexFbs);

        meshFbs.add_version(VERSION);
        meshFbs.add_vertex_unpacked(vecVerticesUnpacked);
        meshFbs.add_indices_unpacked(vecIndicesUnpacked);
        meshFbs.add_vertex(vecVertices);
        meshFbs.add_indices(vecIndices);
        meshFbs.add_aabb_min(&aabbMin);
        meshFbs.add_aabb_max(&aabbMax);

        meshFbs.add_name(nameFbs);

        meshesfbs.push_back(meshFbs.Finish());
    }

    auto meshVectorFbs = builder.CreateVector(meshesfbs.data(), meshesfbs.size());
    auto staticMeshBuilder = FlatBuffers::StaticMeshBuilder(builder);
    staticMeshBuilder.add_meshes(meshVectorFbs);
    staticMeshBuilder.add_aabb_max(&aabbMaxStaticMesh);
    staticMeshBuilder.add_aabb_min(&aabbMinStaticMesh);
    staticMeshBuilder.add_scale(m_scale);

    builder.Finish(staticMeshBuilder.Finish());

    auto* pointerBuffer = builder.GetBufferPointer();
    auto sizeBuffer = builder.GetSize();

    std::ofstream fileWriter = std::ofstream(m_flatbufferPath.c_str(), std::ios_base::binary);

    if (fileWriter.good())
    {
        fileWriter.write(reinterpret_cast<char *>(pointerBuffer), sizeBuffer);
    }
}

const char *Mesh::getName()
{
    return m_name.c_str();
}

