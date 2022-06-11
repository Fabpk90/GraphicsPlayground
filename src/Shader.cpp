#define NOMINMAX
#include "Shader.h"

#include "Engine.h"

Shader::Shader(const char* _path, PIPELINE_TYPE _type) : m_path(_path), m_type(_type)
{
	m_info.pShaderSourceStreamFactory = Engine::instance->getShaderStreamFactory();
	// Tell the system that the shader source code is in HLSL.
	// For OpenGL, the engine will convert this into GLSL under the hood
    m_info.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
	// OpenGL backend requires emulated combined HLSL texture samplers (g_Texture + g_Texture_sampler combination)
    m_info.UseCombinedTextureSamplers = true;

    reload();
}

bool Shader::reload()
{
    auto& device = Engine::instance->getDevice();

    if(m_type == Diligent::PIPELINE_TYPE_GRAPHICS)
    {
        char path[512];

        sprintf_s(path, 512, "%s/vs.hlsl", m_path.data());

        // Create a vertex shader
        RefCntAutoPtr<IShader> pVS;
        {
            m_info.Desc.ShaderType = SHADER_TYPE_VERTEX;
            m_info.EntryPoint = "main";
            m_info.Desc.Name = "Mesh vertex shader";
            m_info.FilePath = path;
            device->CreateShader(m_info, &pVS);
        }

        sprintf_s(path, 512, "%s/ps.hlsl", m_path.data());
        // Create a pixel shader
        RefCntAutoPtr<IShader> pPS;
        {
            m_info.Desc.ShaderType = SHADER_TYPE_PIXEL;
            m_info.EntryPoint = "main";
            m_info.Desc.Name = "Mesh pixel shader";
            m_info.FilePath = path;
            device->CreateShader(m_info, &pPS);
        }

        if(!pVS || !pPS) return false;

        m_shaders[0] = pVS;
        m_shaders[1] = pPS;

    }
    else if (m_type == Diligent::PIPELINE_TYPE_COMPUTE)
    {
        char path[512];

        sprintf_s(path, 512, "%s/cs.hlsl", m_path.data());

        // Create a vertex shader
        RefCntAutoPtr<IShader> pCS;
        {
            m_info.Desc.ShaderType = SHADER_TYPE_COMPUTE;
            m_info.EntryPoint = "main";
            m_info.Desc.Name = "Mesh vertex shader";
            m_info.FilePath = path;

            device->CreateShader(m_info, &pCS);
        }

        if(!pCS) return false;

        m_shaders[0] = pCS;
    }

    return true;
}

RefCntAutoPtr<IShader> Shader::getShaderStage(const EShaderStage _stage) const
{
	switch (_stage) {
    case EShaderStage::Vertex: return m_shaders[0];
    case EShaderStage::Pixel: return m_shaders[1];
    case EShaderStage::Compute: return m_shaders[0];
	}

    return {};
}

void Shader::Release()
{

}
