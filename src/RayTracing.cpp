//
// Created by fab on 31/05/2022.
//

#include "RayTracing.hpp"
#include "BottomLevelAS.h"
#include "Common/interface/BasicMath.hpp"

RayTracing::RayTracing(Diligent::IRenderDevice *_device)
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

    Diligent::BLASTriangleDesc triangleDesc;
    triangleDesc.GeometryName = "Cube";
    triangleDesc.IndexType = Diligent::VT_INT16;
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
}