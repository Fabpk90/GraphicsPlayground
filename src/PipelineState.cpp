//
// Created by fab on 05/06/2022.
//

#include "PipelineState.hpp"
#include "Tracy.hpp"

PipelineState::PipelineState(RefCntAutoPtr<IRenderDevice> _device, const char* _name, PIPELINE_TYPE _type, const char* _shaderPath, eastl::vector<VarStruct> _staticVars, eastl::vector<VarStruct> _dynamicVars
                             , GraphicsPipelineDesc _graphicsDesc
, eastl::vector<LayoutElement> _layoutElements)
: m_device(eastl::move(_device)), m_type(_type), m_layoutElements(eastl::move(_layoutElements))
, m_staticVars(eastl::move(_staticVars)), m_dynamicVars(eastl::move(_dynamicVars))
{
    m_pipelineShader.Attach(new Shader(_shaderPath, _type));
    PipelineStateCreateInfo* PSO;

    if(_type == Diligent::PIPELINE_TYPE_GRAPHICS)
    {
        m_graphicInfo.GraphicsPipeline = _graphicsDesc;
        PSO = &m_graphicInfo;
    }
    else
    {
        PSO = &m_computeInfo;
    }

    PSO->PSODesc.Name = _name;

    createPipeline( _type, PSO);
}

bool PipelineState::createPipeline(const PIPELINE_TYPE &_type, PipelineStateCreateInfo *PSO)
{
    ZoneScopedN("Load Pipeline");
    if(_type == PIPELINE_TYPE_GRAPHICS)
    {
        m_type = PIPELINE_TYPE_GRAPHICS;

        m_graphicInfo.pVS = m_pipelineShader->getShaderStage(Shader::EShaderStage::Vertex).RawPtr<IShader>();
        m_graphicInfo.pPS = m_pipelineShader->getShaderStage(Shader::EShaderStage::Pixel).RawPtr<IShader>();

        m_graphicInfo.GraphicsPipeline.InputLayout.LayoutElements = m_layoutElements.data();
        m_graphicInfo.GraphicsPipeline.InputLayout.NumElements = m_layoutElements.size();

        m_shaderStages.push_back(m_graphicInfo.pVS);
        m_shaderStages.push_back(m_graphicInfo.pPS);
    }
    else if (_type == PIPELINE_TYPE_COMPUTE)
    {
        m_type = PIPELINE_TYPE_COMPUTE;
        m_computeInfo.pCS = m_pipelineShader->getShaderStage(Shader::EShaderStage::Compute).RawPtr<IShader>();

        m_shaderStages.push_back(m_computeInfo.pCS);
    }

    eastl::vector<ShaderResourceVariableDesc> vars;
    eastl::vector<ImmutableSamplerDesc> samplersDesc;

    SamplerDesc SamLinearClampDesc
    {
            FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
            TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP
    };

    for (auto* shader : m_shaderStages)
    {
        if(!shader) return false;
        for (int i = 0; i < shader->GetResourceCount(); ++i)
        {
            ShaderResourceDesc desc;
            shader->GetResourceDesc(i, desc);

            const auto& shaderDesc = shader->GetDesc();

            if(desc.Type == SHADER_RESOURCE_TYPE_TEXTURE_SRV)
            {
                samplersDesc.emplace_back(ImmutableSamplerDesc (shaderDesc.ShaderType, desc.Name, SamLinearClampDesc));
            }

            //todo: this is suboptimal, let the user choose it
            //I'm doing this because I'm reusing this PSO for all meshes
            SHADER_RESOURCE_VARIABLE_TYPE type = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC;

            if(desc.Type == SHADER_RESOURCE_TYPE_CONSTANT_BUFFER)
            {
                type = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
            }

            vars.emplace_back(shaderDesc.ShaderType,  desc.Name, type);
        }
    }

    PSO->PSODesc.ResourceLayout.Variables    = vars.data();
    PSO->PSODesc.ResourceLayout.NumVariables = vars.size();

    PSO->PSODesc.ResourceLayout.ImmutableSamplers    = samplersDesc.data();
    PSO->PSODesc.ResourceLayout.NumImmutableSamplers = samplersDesc.size();

    if(_type == PIPELINE_TYPE_GRAPHICS)
    {
        m_device->CreateGraphicsPipelineState(m_graphicInfo, &m_pipeline);
    }
    else if(_type == PIPELINE_TYPE_COMPUTE)
    {
        m_device->CreateComputePipelineState(m_computeInfo, &m_pipeline);
    }

    setStaticVars(m_staticVars);
    m_pipeline->CreateShaderResourceBinding(&m_SRB, true);
    setDynamicVars(m_dynamicVars);

    return true;
}

void PipelineState::reload()
{
    if(!m_pipelineShader->reload()) return;

    m_pipeline = RefCntAutoPtr<IPipelineState>();
    m_shaderStages.clear();

    //TODO: refactor this, it's a bit of a mess, to much copied pasted code
    PipelineStateCreateInfo* PSO;

    if(m_type == Diligent::PIPELINE_TYPE_GRAPHICS)
    {
        PSO = &m_graphicInfo;
    }
    else
    {
        PSO = &m_computeInfo;
    }

    createPipeline(m_type, PSO);

    std::cout << "Correctly reloaded PSO " << PSO->PSODesc.Name << std::endl;
}

void PipelineState::setStaticVars(const eastl::vector<VarStruct> &_vars)
{
    for (auto& var : _vars)
    {
        //checks if the var is still needed by the shader
        if(auto* pVar = m_pipeline->GetStaticVariableByName(var.m_type, var.m_name))
        {
            pVar->Set(var.m_object);
        }
        else
        {
            std::cout << "The var" << var.m_name << " is set but not used in the shader" << std::endl;
        }
    }
}

void PipelineState::setDynamicVars(const eastl::vector<VarStruct> &_vars)
{
    for (auto& var : _vars)
    {
        m_SRB->GetVariableByName(var.m_type, var.m_name)->Set(var.m_object);
    }
}

PipelineState::~PipelineState()
{
    for(auto* shader : m_shaderStages)
    {
        shader->Release();
    }
}