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
#include "taskflow/core/executor.hpp"
#include "taskflow/taskflow.hpp"
#include "Common/interface/AdvancedMath.hpp"
#include "Graphics/GraphicsAccessories/interface/GraphicsAccessories.hpp"


static constexpr uint32_t VERSION = 9;

using namespace Diligent;

struct VertexPacked
{
    uint2 m_position;
    uint2 m_normaluv; // x 8 LMB y 8, z = cross(x,y)
    uint m_tangent;
};

struct Vertex
{
    float3 m_position;
    float3 m_normal;
    float2 m_uv;
    float3 m_tangent;
};

class Mesh {
public:
    struct Group
    {
        eastl::string m_name;
        eastl::vector<VertexPacked> m_vertices;
        eastl::vector<uint16_t> m_indices;
        eastl::vector<float3> m_verticesPosRaytrace; // used for raytracing
        eastl::vector<uint32_t> m_indicesRaytrace;
        eastl::vector<RefCntAutoPtr<ITexture>> m_textures;
        eastl::vector<unsigned char*> m_texturesData; // used to save textures on disk
        BoundBox m_aabb; // In local space

        RefCntAutoPtr<IPipelineState> m_pipeline;

        RefCntAutoPtr<IBuffer> m_meshVertexBuffer;
        RefCntAutoPtr<IBuffer> m_meshVertexBufferUnpacked;
        RefCntAutoPtr<IBuffer> m_meshIndexBuffer; // This should be index zith the pri;itiveindex zhen rqytrqcing

        // TODO @fsantoro uv + normal
        RefCntAutoPtr<IBuffer> m_meshRaytraceData;
    };

    void setTranslation(Vector3<float>& vector3);
    void setScale(float scale);
    void save();

    const char *getName();

public:

    inline static eastl::hash_map<Mesh*, uint32_t > meshLoaded;
    inline static std::atomic<uint32_t> idCount = 0;

    Mesh(RefCntAutoPtr<IRenderDevice> _device, const char* _path, bool _needsAfterLoadedActions = false, float3 _position = float3(0), float _scale = 1
            , float3 _angle = float3(0.0f));

    ~Mesh()
    {
        for(auto& mesh : m_meshes)
        {
            for(const auto* texData : mesh.m_texturesData)
            {
                delete[] texData;
            }
        }
    }

    bool operator<(Mesh* _other) const
    {
        return length(m_position) < length(_other->m_position);
    }

    //todo: add texture emplace with already loaded tex

    void addTexture(const char* _path, int index)
    {
        eastl::string str = _path;
        addTexture(str, index);
    }

    //todo: make a string_view version of this
    void addTexture(eastl::string& _path, Group& _group);
    void addTexture(eastl::string& _path, int index);

    //todo fsantoro, handle multiple mesh models

    void drawInspector();

    float4x4& getModel() { return m_model;}

    [[nodiscard]] bool isLoaded() const { return m_isLoaded; }
    void setIsLoaded(bool _isLoaded) { m_isLoaded = _isLoaded;}

    bool isTransparent() const;
    void setTransparent(bool mIsTransparent);

    //returns the first group hit
    bool isClicked(const float4x4& _mvp,
                   const float3&   RayOrigin,
                   const float3&   RayDirection,
                   float&          EnterDist,
                   float&          ExitDist);

    BoundBox getBoundingBox();

    float3& getTranslation() { return m_position;}
    float getScale() { return m_scale;}
    float4x4 getRotation() { return m_rotation.ToMatrix();}
    //void setRotation(const float4x4& _rotation) { m_rotation.MakeQuaternion()}

    eastl::vector<Group>& getGroups(){ return m_meshes;}

private:
    uint32_t m_id;

    bool m_isLoaded;
    bool m_isTransparent = false;

    tf::Executor m_executor;
    tf::Taskflow m_taskflow;

    float4x4 m_model;
    float3 m_position;
    float m_scale;
    float3 m_angle;

    Quaternion<float> m_rotation = Quaternion<float>(0, 0, 0, 1);

    bool m_isSelected;

    RefCntAutoPtr<IRenderDevice> m_device;

    eastl::string m_basePath;
    eastl::string m_name;
    eastl::string m_flatbufferPath; // todo: make sure we need this

    eastl::vector<Group> m_meshes;

    eastl::unordered_map<eastl::string, RefCntAutoPtr<ITexture>> m_texturesLoaded;

    void recursivelyLoadNode(aiNode *pNode, const aiScene *pScene);

    Group loadGroupFrom(const aiMesh& mesh, const aiScene *pScene);

    void LoadFromPath(const char *_path);
};


#endif //GRAPHICSPLAYGROUND_MESH_H
