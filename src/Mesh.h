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

using namespace Diligent;

struct Vertex
{
    float3 m_position;
    float3 m_normal;
    float2 m_uv;
};

class Mesh {
public:
    struct Group
    {
        std::vector<Vertex> m_vertices;
        std::vector<uint> m_indices;
        std::vector<RefCntAutoPtr<ITexture>> m_textures;
        BoundBox m_aabb;

        //This is not really optimal, as the data doesn't need to be both on CPU AND GPU
        RefCntAutoPtr<IBuffer> m_meshVertexBuffer;
        RefCntAutoPtr<IBuffer> m_meshIndexBuffer;
    };

public:
    Mesh(RefCntAutoPtr<IRenderDevice> _device, const char* _path, bool _needsAfterLoadedActions = false, float3 _position = float3(0), float3 _scale = float3(1)
            , float3 _angle = float3(0.0f));

    void addTexture(const char* _path, int index)
    {
        eastl::string str = _path;
        addTexture(str, index);
    }

    //todo: make a string_view version of this
    void addTexture(eastl::string& _path, Group& _group);
    void addTexture(eastl::string& _path, int index);

    void draw(RefCntAutoPtr<IDeviceContext> _context, IShaderResourceBinding* _srb, FirstPersonCamera &_camera,
         RefCntAutoPtr<IBuffer> _bufferMatrices,
         eastl::unordered_map<eastl::string, RefCntAutoPtr<ITexture>> &_defaultTextures);

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

    float3& getTranslation() { return m_position;}
    float3& getScale() { return m_scale;}
    float4x4 getRotation() { return m_rotation.ToMatrix();}
    //void setRotation(const float4x4& _rotation) { m_rotation.MakeQuaternion()}

private:
    bool m_isLoaded;
    bool m_isTransparent = false;

    tf::Executor m_executor;
    tf::Taskflow m_taskflow;

    float4x4 m_model;
    float3 m_position;
    float3 m_scale;
    float3 m_angle;

    Quaternion m_rotation;

    BoundBox m_aabb;
    bool m_isSelected;

    RefCntAutoPtr<IRenderDevice> m_device;

    eastl::string m_path;

    eastl::vector<Group> m_meshes;

    eastl::unordered_map<eastl::string, RefCntAutoPtr<ITexture>> m_texturesLoaded;

    void recursivelyLoadNode(aiNode *pNode, const aiScene *pScene);

    Group loadGroupFrom(const aiMesh& mesh, const aiScene *pScene);
};


#endif //GRAPHICSPLAYGROUND_MESH_H
