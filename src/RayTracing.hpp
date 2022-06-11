//
// Created by fab on 31/05/2022.
//

#ifndef GRAPHICSPLAYGROUND_RAYTRACING_HPP
#define GRAPHICSPLAYGROUND_RAYTRACING_HPP


#include <vector>
#include "BottomLevelAS.h"
#include "RenderDevice.h"

class RayTracing
{
public:
    RayTracing(Diligent::IRenderDevice* _device);

    void render();
private:
    /*
     * We need
     * 0: A mesh desc
     * 1: BLAS
     * 2: TLAS
     * 3: Shader binding table (hit shader, any hit, miss, closest)
     * 4: RTPSO
     */
    std::vector<Diligent::IBottomLevelAS*> BLASes;
};


#endif //GRAPHICSPLAYGROUND_RAYTRACING_HPP
