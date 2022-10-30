//
// Created by fab on 27/09/2022.
//

#ifndef GRAPHICSPLAYGROUND_GPUMARKERSCOPED_HPP
#define GRAPHICSPLAYGROUND_GPUMARKERSCOPED_HPP

// todo @fsantoro exclude when in retail #if defined
#define USE_PIX 1

#include <EASTL/string.h>
#include "Engine.h"
#define S1(x) x
#define PREFIX() scoped
#define GPUScopedMarker(name) GPUMarkerScoped PREFIX()S1(__LINE__)(name);

class GPUMarkerScoped{
public:
    explicit GPUMarkerScoped(eastl::string _name)
    {
        Engine::instance->getContext()->BeginDebugGroup(_name.c_str());
    }

    ~GPUMarkerScoped()
    {
        Engine::instance->getContext()->EndDebugGroup();
    }
};
#endif //GRAPHICSPLAYGROUND_GPUMARKERSCOPED_HPP
