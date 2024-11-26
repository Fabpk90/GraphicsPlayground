//
// Created by fab on 31/05/2022.
//

#ifndef GRAPHICSPLAYGROUND_RAYTRACING_HPP
#define GRAPHICSPLAYGROUND_RAYTRACING_HPP

#include <EASTL/vector.h>
#include "BottomLevelAS.h"
#include "RenderDevice.h"
#include "Common/interface/RefCntAutoPtr.hpp"
#include "Common/interface/BasicMath.hpp"
#include "Mesh.h"

using namespace Diligent;
class RayTracing
{

    //todo fsantoro: pack this
    struct alignas(16) PrimaryRayPayload
    {
        float3 m_color;
        float m_depth;
        uint32_t  m_iterations;
    };

    struct alignas(16) Constants
    {
        float4x4 m_cameraInvViewProjection;
        float4x4 m_cameraToWorld;
        float4 m_params; //x width y height z near w far
        float4 m_lightPos;
        float4 m_camPos;
        float3 m_lightColor;
        uint   m_maxRecursion;
    };

public:
    RayTracing(RefCntAutoPtr<Diligent::IRenderDevice> _device, RefCntAutoPtr<IDeviceContext> _context);

    void addMeshToRayTrace(Mesh* _mesh);
    void createBlasIfNeeded();
    void render(RefCntAutoPtr<IDeviceContext>& _context, int height, int width);

private:
    void computeBLAS(RefCntAutoPtr<Diligent::IRenderDevice> _device, RefCntAutoPtr<IDeviceContext> _context, const Mesh::Group& _grp);
    /*
     * We need
     * 0: A mesh desc
     * 1: BLAS
     * 2: TLAS
     * 3: Shader binding table (hit shader, any hit, miss, closest)
     * 4: RTPSO
     */

    RefCntAutoPtr<IRenderDevice>  m_device;
    RefCntAutoPtr<IDeviceContext> m_immediateContext;

    std::mutex addMutex;

    eastl::vector<Mesh*> m_meshToAdd;

    eastl::vector<RefCntAutoPtr<IBottomLevelAS>> BLASes;
    RefCntAutoPtr<ITopLevelAS> TLAS;

    RefCntAutoPtr<IBuffer> m_scratchBuffer;

    RefCntAutoPtr<IPipelineState> m_pso;
    RefCntAutoPtr<IShaderBindingTable> m_sbt;
    RefCntAutoPtr<IShaderResourceBinding> m_srb;

    RefCntAutoPtr<IShader> m_rayGenShader;
    RefCntAutoPtr<IShader> m_rayMissShader;
    RefCntAutoPtr<IShader> m_rayShadowMissShader;

    RefCntAutoPtr<IShader> m_triangleShader;

    RefCntAutoPtr<IBuffer> m_bufferConstants;

    RefCntAutoPtr<IBuffer> m_bufferGeometryIndicesTable;
    //TODO @fsantoro one day handle deallocating meshes
    eastl::vector<const Mesh::Group*> m_meshGroups; //Basically all BLASes data

    void createRayTracingPipeline();

    void createSBT(RefCntAutoPtr<IRenderDevice> _device, RefCntAutoPtr<IDeviceContext> _context);

    void updateBLAS(RefCntAutoPtr<IRenderDevice> _device);

    void createTLAS(const eastl::vector<RefCntAutoPtr<IBottomLevelAS>>& _blases, RefCntAutoPtr<Diligent::IRenderDevice> _device, RefCntAutoPtr<IDeviceContext> _context);

    void UpdateGeometryBuffer();
};


#endif //GRAPHICSPLAYGROUND_RAYTRACING_HPP
