//
// Created by fab on 05/06/2022.
//

#ifndef GRAPHICSPLAYGROUND_PIPELINESTATE_HPP
#define GRAPHICSPLAYGROUND_PIPELINESTATE_HPP

#include "Shader.h"
#include "Common/interface/RefCntAutoPtr.hpp"
#include "PipelineState.h"
#include "RenderDevice.h"

#include <EASTL/string.h>
#include <EASTL/vector.h>

using namespace Diligent;

class PipelineState
{
public:

    struct VarStruct
    {
        SHADER_TYPE m_type;
        const char* m_name;
        IDeviceObject* m_object;
    };

    PipelineState(RefCntAutoPtr<IRenderDevice> _device, const char* _name, PIPELINE_TYPE _type, const char* _shaderPath, eastl::vector<VarStruct> _staticVars = {}
    , eastl::vector<VarStruct> _dynamicVars = {}, GraphicsPipelineDesc _graphicsDesc = {}, eastl::vector<LayoutElement> _layoutElements = {});

    ~PipelineState();

    void setStaticVars(const eastl::vector<VarStruct>& _vars );
    void setDynamicVars(const eastl::vector<VarStruct>& _vars );

    inline void setShaderResource(SHADER_TYPE _type, const char* _name, IDeviceObject* _object)
    {
        if(auto* pVar = m_SRB->GetVariableByName(_type, _name))
        {
            pVar->Set(_object);
        }
    }

    void reload();

    IShaderResourceBinding& getSRB() { return *m_SRB;}

    [[nodiscard]] RefCntAutoPtr<IPipelineState> getPipeline() const { return m_pipeline;}

private:

    RefCntAutoPtr<IRenderDevice> m_device;

    Diligent::RefCntAutoPtr<Shader> m_pipelineShader;

    PIPELINE_TYPE m_type;

    //todo: just keep a PipelineInfoStruct* around, and reinterpret cast when needed
    ComputePipelineStateCreateInfo m_computeInfo;
    GraphicsPipelineStateCreateInfo m_graphicInfo;

    eastl::vector<LayoutElement> m_layoutElements;
    RefCntAutoPtr<IPipelineState> m_pipeline;
    eastl::vector<RefCntAutoPtr<IShader>> m_shaderStages;
    RefCntAutoPtr<IShaderResourceBinding> m_SRB;

    eastl::vector<VarStruct> m_staticVars;
    eastl::vector<VarStruct> m_dynamicVars;

    bool createPipeline(const PIPELINE_TYPE &_type, PipelineStateCreateInfo *PSO);
};


#endif //GRAPHICSPLAYGROUND_PIPELINESTATE_HPP
