//
// Created by fab on 31/05/2022.
//
#define NOMINMAX
#include "RayTracing.hpp"
#include "BottomLevelAS.h"
#include "Common/interface/BasicMath.hpp"
#include "DeviceContext.h"
#include "Engine.h"
#include "GraphicsTypesX.hpp"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "GPUMarkerScoped.hpp"


struct PrimitiveTable {
    uint32_t  m_triangleBufferIndex;
    uint32_t  m_uvNormalBufferIndex;
    uint32_t  m_albedoTextureIndex;
    uint32_t pad;
};

RayTracing::RayTracing(RefCntAutoPtr<Diligent::IRenderDevice> _device, RefCntAutoPtr<IDeviceContext> _context):
m_device(_device), m_immediateContext(_context)
{
   const Diligent::float3 CubePos[24] = {Diligent::float3(-1, -1, -1),
                                          Diligent::float3(1, -1, -1),
                                          Diligent::float3(1, 1, -1),
                                          Diligent::float3(-1, 1, -1),
                                          Diligent::float3(-1, -1, 1),
                                          Diligent::float3(1, -1, 1),
                                          Diligent::float3(1, 1, 1),
                                          Diligent::float3(-1, 1, 1)

            };
    const Diligent::Uint16   Indices[36] = { 0, 1, 3, 3, 1, 2,
                                           1, 5, 2, 2, 5, 6,
                                           5, 4, 6, 6, 4, 7,
                                           4, 0, 7, 7, 0, 3,
                                           3, 2, 7, 7, 2, 6,
                                           4, 5, 0, 0, 5, 1};

    RefCntAutoPtr<IBuffer> vertexBuffer;
    RefCntAutoPtr<IBuffer> indexBuffer;

    {
        BufferDesc desc;
        desc.BindFlags = Diligent::BIND_RAY_TRACING | Diligent::BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = CPU_ACCESS_NONE;
        desc.ElementByteStride = sizeof(float3);
        desc.Size = sizeof(CubePos);
        desc.Name = "Cube Verts";
        desc.Usage = Diligent::USAGE_IMMUTABLE;

        BufferData data;
        data.DataSize = sizeof(CubePos);
        data.pData = CubePos;

        _device->CreateBuffer(desc, &data, &vertexBuffer);
    }

    {
        BufferDesc desc;
        desc.BindFlags = Diligent::BIND_RAY_TRACING | Diligent::BIND_INDEX_BUFFER;
        desc.CPUAccessFlags = CPU_ACCESS_NONE;
        desc.ElementByteStride = sizeof(uint16_t);
        desc.Size = sizeof(Indices);
        desc.Name = "Cube index";
        desc.Usage = Diligent::USAGE_IMMUTABLE;

        BufferData data;
        data.DataSize = sizeof(Indices);
        data.pData = Indices;

        _device->CreateBuffer(desc, &data, &indexBuffer);
    }

    Diligent::BLASTriangleDesc triangleDesc;
    triangleDesc.GeometryName = "Cube";
    triangleDesc.IndexType = Diligent::VT_UINT16;
    triangleDesc.MaxVertexCount = _countof(CubePos);
    triangleDesc.MaxPrimitiveCount = _countof(Indices) / 3;
    triangleDesc.VertexComponentCount = 3;
    triangleDesc.VertexValueType = Diligent::VT_FLOAT32;

    Diligent::BottomLevelASDesc BLASDesc;
    BLASDesc.Name = "Cube BLAS";
    BLASDesc.Flags = Diligent::RAYTRACING_BUILD_AS_PREFER_FAST_TRACE;
    BLASDesc.pTriangles = &triangleDesc;
    BLASDesc.TriangleCount = 1;

    BLASes.resize(1);
    _device->CreateBLAS(BLASDesc, &BLASes[0]);

    {
        BufferDesc desc;
        desc.Size = BLASes[0]->GetScratchBufferSizes().Build;
        desc.Name = "Scratch Buffer";
        desc.BindFlags = BIND_RAY_TRACING;

        _device->CreateBuffer(desc, nullptr, &m_scratchBuffer);
    }

    Diligent::BLASBuildTriangleData triangleData;
    triangleData.Flags = Diligent::RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    triangleData.GeometryName = "Cube";
    triangleData.pVertexBuffer = vertexBuffer;
    triangleData.VertexStride = sizeof(float3);
    triangleData.VertexCount = triangleDesc.MaxVertexCount;
    triangleData.VertexValueType = triangleDesc.VertexValueType;
    triangleData.VertexComponentCount = triangleDesc.VertexComponentCount;
    triangleData.pIndexBuffer = indexBuffer;
    triangleData.PrimitiveCount = triangleDesc.MaxPrimitiveCount;
    triangleData.IndexType = triangleDesc.IndexType;

    Diligent::BuildBLASAttribs attribs;
    attribs.pBLAS = BLASes[0];
    attribs.pTriangleData = &triangleData;
    attribs.TriangleDataCount = 1;
    attribs.pScratchBuffer = m_scratchBuffer;
    attribs.BLASTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    attribs.GeometryTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    attribs.ScratchBufferTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;

    _context->BuildBLAS(attribs);

    createTLAS(BLASes, _device, _context);
    createRayTracingPipeline();
    createSBT(_device, _context);

    m_srb->GetVariableByName(Diligent::SHADER_TYPE_RAY_GEN, "g_TLAS")->Set(TLAS);

    auto& gbuffer = Engine::instance->getGBuffer();
    m_srb->GetVariableByName(Diligent::SHADER_TYPE_RAY_GEN, "g_ColorBuffer")->Set(gbuffer.getTextureOfType(GBuffer::EGBufferType::Output)->GetDefaultView(Diligent::TEXTURE_VIEW_UNORDERED_ACCESS));

    {
        BufferDesc desc;
        desc.Size = sizeof(RayTracing::Constants);
        desc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
        desc.Usage = Diligent::USAGE_DYNAMIC;
        desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
        desc.Name = "Constants Buffer";
        desc.ElementByteStride = sizeof(RayTracing::Constants);

        _device->CreateBuffer(desc, nullptr, &m_bufferConstants);
    }
}

void RayTracing::addMeshToRayTrace(Mesh* _mesh)
{
    std::scoped_lock lock(addMutex);
    m_meshToAdd.push_back(_mesh);
}

void RayTracing::createBlasIfNeeded()
{
    //TODO @fsantoro change the name of this function, it does more than it says on the tin

    std::scoped_lock lock(addMutex);

    if(m_meshToAdd.empty())
        return;

    for (auto* mesh: m_meshToAdd)
    {
        const auto& grps = mesh->getGroups();
        for(const auto& grp : grps)
        {
            computeBLAS(m_device, m_immediateContext, grp);
            m_meshGroups.push_back(&grp);
        }
    }

    m_meshToAdd.clear();

    createTLAS(BLASes, m_device, m_immediateContext);
    m_srb->GetVariableByName(Diligent::SHADER_TYPE_RAY_GEN, "g_TLAS")->Set(TLAS);

    m_sbt->ResetHitGroups();

    // The name corresponds the name given in the pipe creation desc
    m_sbt->BindRayGenShader("Main");
    m_sbt->BindMissShader("PrimaryMiss", 0);
    m_sbt->BindMissShader("ShadowMiss", 1);
    for (auto& blas: BLASes)
    {
        m_sbt->BindHitGroupForInstance(TLAS, blas->GetDesc().Name, 0, "CubeHit");
    }

    //
    m_sbt->BindHitGroupForTLAS(TLAS, 1, "CubeHit");

    UpdateGeometryBuffer();

    m_immediateContext->UpdateSBT(m_sbt);
}

void RayTracing::createRayTracingPipeline()
{
    ShaderCreateInfo shaderCI;
    shaderCI.pShaderSourceStreamFactory = Engine::instance->getShaderStreamFactory();
    shaderCI.ShaderCompiler = Diligent::SHADER_COMPILER_DXC;
    shaderCI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
    shaderCI.HLSLVersion = {6, 5};

    {
        shaderCI.FilePath = "raytrace/rt_gen.hlsl";
        shaderCI.EntryPoint = "main";
        shaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_RAY_GEN;
        shaderCI.Desc.Name = "RT Gen Shader";

        m_device->CreateShader(shaderCI, &m_rayGenShader);
    }

    {
        shaderCI.FilePath = "raytrace/rt_miss.hlsl";
        shaderCI.EntryPoint = "main";
        shaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_RAY_MISS;
        shaderCI.Desc.Name = "RT Miss Shader";

        m_device->CreateShader(shaderCI, &m_rayMissShader);

        shaderCI.FilePath = "raytrace/rt_shadow_miss.hlsl";
        shaderCI.Desc.Name = "RT Shadow Miss Shader";

        m_device->CreateShader(shaderCI, &m_rayShadowMissShader);
    }

    {
        shaderCI.FilePath = "raytrace/rt_triangle.hlsl";
        shaderCI.EntryPoint = "main";
        shaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_RAY_CLOSEST_HIT;
        shaderCI.Desc.Name = "RT Triangle Shader";

        m_device->CreateShader(shaderCI, &m_triangleShader);
    }

    RayTracingPipelineStateCreateInfoX pipelineStateCreateInfo;
    pipelineStateCreateInfo.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_RAY_TRACING;

    pipelineStateCreateInfo.AddGeneralShader("Main", m_rayGenShader);
    pipelineStateCreateInfo.AddGeneralShader("PrimaryMiss", m_rayMissShader);
    pipelineStateCreateInfo.AddGeneralShader("ShadowMiss", m_rayShadowMissShader);

    pipelineStateCreateInfo.AddTriangleHitShader("CubeHit", m_triangleShader);

    pipelineStateCreateInfo.RayTracingPipeline.MaxRecursionDepth = 1; // Allow primary ray to gen other rays but that's all
    pipelineStateCreateInfo.RayTracingPipeline.ShaderRecordSize = 0;
    pipelineStateCreateInfo.MaxAttributeSize = sizeof(float2);
    pipelineStateCreateInfo.MaxPayloadSize = sizeof(PrimaryRayPayload);

    pipelineStateCreateInfo.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC;

    m_device->CreateRayTracingPipelineState(pipelineStateCreateInfo, &m_pso);

    m_pso->CreateShaderResourceBinding(&m_srb, true);
}

void RayTracing::createSBT(RefCntAutoPtr<IRenderDevice> _device, RefCntAutoPtr<IDeviceContext> _context)
{
    ShaderBindingTableDesc sbtDesc;
    sbtDesc.Name = "SBT";
    sbtDesc.pPSO = m_pso;

    _device->CreateSBT(sbtDesc, &m_sbt);

    // The name corresponds the name given in the pipe creation desc
    m_sbt->BindRayGenShader("Main");
    m_sbt->BindMissShader("PrimaryMiss", 0);
    m_sbt->BindMissShader("ShadowMiss", 1);
    for (auto& blas: BLASes)
    {
        m_sbt->BindHitGroupForInstance(TLAS, blas->GetDesc().Name, 0, "CubeHit");
    }

    //
    m_sbt->BindHitGroupForTLAS(TLAS, 1, "CubeHit");

    _context->UpdateSBT(m_sbt);
}

void RayTracing::render(RefCntAutoPtr<IDeviceContext>& _context, int height, int width)
{
    GPUScopedMarker("RayTracing");
    // first update the tlas instance's transform
    // then tracerays

    _context->SetPipelineState(m_pso);
    //m_srb->GetVariableByName(Diligent::SHADER_TYPE_RAY_GEN, "g_ColorBuffer")->Set()


    {
        auto& cam = Engine::instance->getCamera();
        auto& gbuffer = Engine::instance->getGBuffer();
        auto& desc = gbuffer.getTextureOfType(GBuffer::EGBufferType::Output)->GetDesc();

        MapHelper<RayTracing::Constants> mappedBuffer(_context, m_bufferConstants, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
        mappedBuffer->m_maxRecursion = 1; // we start at 0 (on ray gen)
        mappedBuffer->m_camPos = cam.GetPos();
        mappedBuffer->m_lightPos = float4(0);
        mappedBuffer->m_cameraInvViewProjection = (cam.GetViewMatrix() * cam.GetProjMatrix()).Inverse().Transpose();
        mappedBuffer->m_cameraToWorld = (cam.GetViewMatrix() * cam.GetProjMatrix()).Inverse().Transpose();
        mappedBuffer->m_params = float4(desc.Width, desc.Height, cam.GetProjAttribs().NearClipPlane, cam.GetProjAttribs().FarClipPlane);
        mappedBuffer->m_lightColor = float4(1, 1, 0, 0);
    }

    m_srb->GetVariableByName(Diligent::SHADER_TYPE_RAY_GEN, "Constants")->Set(m_bufferConstants);
    m_srb->GetVariableByName(Diligent::SHADER_TYPE_RAY_MISS, "Constants")->Set(m_bufferConstants);
    _context->CommitShaderResources(m_srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    TraceRaysAttribs attribs;
    attribs.DimensionX = width;
    attribs.DimensionY = height;
    attribs.pSBT = m_sbt;

    _context->TraceRays(attribs);
}

void RayTracing::computeBLAS(RefCntAutoPtr<Diligent::IRenderDevice> _device, RefCntAutoPtr<IDeviceContext> _context, const Mesh::Group& _grp)
{
    Diligent::BLASTriangleDesc triangleDesc;
    triangleDesc.GeometryName = _grp.m_name.c_str();
    triangleDesc.IndexType = Diligent::VT_UINT32;
    triangleDesc.MaxVertexCount = _grp.m_verticesPosRaytrace.size();
    triangleDesc.MaxPrimitiveCount = _grp.m_indices.size() / 3;
    triangleDesc.VertexComponentCount = 3;
    triangleDesc.VertexValueType = Diligent::VT_FLOAT32;

    Diligent::BottomLevelASDesc BLASDesc;
    BLASDesc.Name = _grp.m_name.c_str(); // This is the instance name
    BLASDesc.Flags = Diligent::RAYTRACING_BUILD_AS_PREFER_FAST_TRACE;
    BLASDesc.pTriangles = &triangleDesc;
    BLASDesc.TriangleCount = 1;

    RefCntAutoPtr<IBottomLevelAS> blas;
    _device->CreateBLAS(BLASDesc, &blas);

    RefCntAutoPtr<IBuffer> bufferIndices;
    RefCntAutoPtr<IBuffer> bufferVertices;

    {
        BufferDesc desc;
        desc.Size = sizeof(float3) * _grp.m_verticesPosRaytrace.size();
        desc.Name = "Raytrace Triangle pos Buffer";
        desc.BindFlags = Diligent::BIND_RAY_TRACING;

        BufferData data;
        data.DataSize = desc.Size;
        data.pData = _grp.m_verticesPosRaytrace.data();

        _device->CreateBuffer(desc, &data, &bufferVertices);
    }

    {
        BufferDesc desc;
        desc.Size = sizeof(uint32_t) * _grp.m_indicesRaytrace.size();
        desc.Name = "Raytrace Indices Buffer";
        desc.BindFlags = Diligent::BIND_RAY_TRACING;

        BufferData data;
        data.DataSize = desc.Size;
        data.pData = _grp.m_indicesRaytrace.data();

        _device->CreateBuffer(desc, &data, &bufferIndices);
    }

    if(blas->GetScratchBufferSizes().Build > m_scratchBuffer->GetDesc().Size)
    {
        m_scratchBuffer.Release();
        BufferDesc desc;
        desc.Size = blas->GetScratchBufferSizes().Build;
        desc.Name = "Scratch Buffer";
        desc.BindFlags = BIND_RAY_TRACING;

        _device->CreateBuffer(desc, nullptr, &m_scratchBuffer);
    }


    Diligent::BLASBuildTriangleData triangleData;
    triangleData.Flags = Diligent::RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    triangleData.GeometryName = _grp.m_name.c_str();
    triangleData.pVertexBuffer = bufferVertices;
    triangleData.VertexStride = sizeof(float3);
    triangleData.VertexCount = triangleDesc.MaxVertexCount;
    triangleData.VertexValueType = triangleDesc.VertexValueType;
    triangleData.VertexComponentCount = triangleDesc.VertexComponentCount;
    triangleData.pIndexBuffer = bufferIndices;
    triangleData.PrimitiveCount = triangleDesc.MaxPrimitiveCount;
    triangleData.IndexType = triangleDesc.IndexType;

    Diligent::BuildBLASAttribs attribs;
    attribs.pBLAS = blas;
    attribs.pTriangleData = &triangleData;
    attribs.TriangleDataCount = 1;
    attribs.pScratchBuffer = m_scratchBuffer;
    attribs.BLASTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    attribs.GeometryTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    attribs.ScratchBufferTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;

    _context->BuildBLAS(attribs);
    std::cout << "Creating a blas names: "<< blas->GetDesc().Name << std::endl;

    BLASes.push_back(blas);
}

void RayTracing::updateBLAS(RefCntAutoPtr<IRenderDevice> _device)
{

}

void RayTracing::createTLAS(const eastl::vector<RefCntAutoPtr<IBottomLevelAS>>& _blases, RefCntAutoPtr<Diligent::IRenderDevice> _device, RefCntAutoPtr<IDeviceContext> _context)
{
    if(_blases.empty())
        return;

    TLAS = nullptr;

    {
        TopLevelASDesc desc;
        desc.Flags = Diligent::RAYTRACING_BUILD_AS_PREFER_FAST_TRACE;
        desc.Name = "TLAS";
        desc.MaxInstanceCount = _blases.size();
        _device->CreateTLAS(desc, &TLAS);
    }
    eastl::vector<TLASBuildInstanceData> instancesData;
    for(auto& blas : BLASes)
    {
        TLASBuildInstanceData instanceData;
        instanceData.pBLAS = blas;
        instanceData.InstanceName = blas->GetDesc().Name;
        instanceData.CustomId = 0;
        instanceData.Mask = RAYTRACING_GEOMETRY_FLAG_OPAQUE;

        instancesData.emplace_back(instanceData);
    }

    {
        if(!m_scratchBuffer)
        {
            BufferDesc desc;
            desc.Size = TLAS->GetScratchBufferSizes().Build;
            desc.Name = "Scratch Buffer";
            desc.BindFlags = BIND_RAY_TRACING;

            _device->CreateBuffer(desc, nullptr, &m_scratchBuffer);
        }
        else if(TLAS->GetScratchBufferSizes().Build > m_scratchBuffer->GetDesc().Size)
        {
            m_scratchBuffer = nullptr;

            BufferDesc desc;
            desc.Size = TLAS->GetScratchBufferSizes().Build;
            desc.Name = "Scratch Buffer";
            desc.BindFlags = BIND_RAY_TRACING;

            _device->CreateBuffer(desc, nullptr, &m_scratchBuffer);
        }
    }

    // Stores the tlas data
    RefCntAutoPtr<IBuffer> instanceBuffer;

    {
        BufferDesc desc;
        desc.Size = TLAS_INSTANCE_DATA_SIZE * BLASes.size(); // assume no instancing at all
        desc.Name = "Instance TLAS Buffer";
        desc.BindFlags = BIND_RAY_TRACING;

        _device->CreateBuffer(desc, nullptr, &instanceBuffer);
    }


    BuildTLASAttribs buildAttribs;
    buildAttribs.pScratchBuffer = m_scratchBuffer;
    buildAttribs.pTLAS = TLAS;
    buildAttribs.pInstances = instancesData.data();
    buildAttribs.pInstanceBuffer = instanceBuffer;
    buildAttribs.InstanceCount = instancesData.size();
    buildAttribs.BindingMode = HIT_GROUP_BINDING_MODE_PER_INSTANCE;
    buildAttribs.HitGroupStride = 2; // primary and shadow

    // Allow engine to change resource states.
    buildAttribs.TLASTransitionMode           = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    buildAttribs.BLASTransitionMode           = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    buildAttribs.InstanceBufferTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    buildAttribs.ScratchBufferTransitionMode  = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;

    _context->BuildTLAS(buildAttribs);
}

void RayTracing::UpdateGeometryBuffer()
{

    {
        BufferDesc desc;
        desc.Size = sizeof(PrimitiveTable) * BLASes.size(); // assume no instancing at all
        desc.Name = "GeometryStuff";
        desc.BindFlags = BIND_SHADER_RESOURCE;

        eastl::vector<PrimitiveTable> vectorData;
        vectorData.resize(BLASes.size());

        for (int i = 0; i < BLASes.size(); ++i) {
            const Mesh::Group& grp = *m_meshGroups[i];

            PrimitiveTable table;
            //table.m_uvNormalBufferIndex =

        }

        BufferData data;
        data.DataSize = desc.Size;


        m_device->CreateBuffer(desc, nullptr, &m_bufferGeometryIndicesTable);
    }
}
