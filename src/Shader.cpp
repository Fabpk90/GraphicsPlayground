#define NOMINMAX
#include "Shader.h"

#include <utility>
#include "Engine.h"

Shader::Shader(const char* _path, PIPELINE_TYPE _type, eastl::vector<eastl::pair<eastl::string, eastl::string>>  _macros) : m_path(_path), m_type(_type)
, m_hashes({0, 0}), m_macros(eastl::move(_macros))
{
	m_info.pShaderSourceStreamFactory = Engine::instance->getShaderStreamFactory();
	// Tell the system that the shader source code is in HLSL.
	// For OpenGL, the engine will convert this into GLSL under the hood
    m_info.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    m_info.ShaderCompiler = Diligent::SHADER_COMPILER_DXC;
    m_info.CompileFlags = SHADER_COMPILE_FLAG_ENABLE_UNBOUNDED_ARRAYS;
    m_info.HLSLVersion = ShaderVersion{ 6, 5 };
    m_info.Desc.UseCombinedTextureSamplers = true;

    m_macroHelper.AddShaderMacro("USE_PACKED_VERTEX", Engine::instance->AreVerticesPacked() ? "1" : "0");
    m_macroHelper.AddShaderMacro("HEAP_MAX_TEXTURES", Engine::HEAP_MAX_TEXTURES);
    m_macroHelper.AddShaderMacro("HEAP_MAX_BUFFERS", Engine::HEAP_MAX_BUFFERS);
    for (const auto& macro: m_macros)
    {
        m_macroHelper.AddShaderMacro(macro.first.c_str(), macro.second.c_str());
    }
    m_info.Macros = m_macroHelper;

    reload();
}

bool Shader::reload()
{
    auto& device = Engine::instance->getDevice();

    bool hasNotSameHash = true;

    if(m_type == Diligent::PIPELINE_TYPE_GRAPHICS)
    {
        char path[512];

        //todo fix me !
        sprintf_s(path, 512, "%s/vs.hlsl", m_path.data());
        eastl::string str = "shader/";
        str += path;

        std::ifstream t(str.c_str());

        if(t.good())
        {
            t.seekg(0, std::ios::end);
            size_t size = t.tellg();
            eastl::string buffer(size, ' ');
            t.seekg(0);
            t.read(&buffer[0], size);
            meow_u128 hash = MeowHash(MeowDefaultSeed, buffer.size(), buffer.data());

            if(!MeowHashesAreEqual(hash, m_hashes[0]))
            {
                m_hashes[0] = hash;
                RefCntAutoPtr<IShader> pVS;
                {
                    m_info.Desc.ShaderType = SHADER_TYPE_VERTEX;
                    m_info.EntryPoint = "main";
                    m_info.Desc.Name = path;
                    m_info.Source = buffer.c_str();
                    m_info.SourceLength = buffer.size();
                    device->CreateShader(m_info, &pVS);
                }
                m_shaders[0] = pVS;
                std::cout << "Compiled " << path << " successfully" << std::endl;
            }
            else
            {
                //std::cout << "Hashes are equal for " << path << std::endl;
                hasNotSameHash = false;
            }
        }
        else
        {
            return false;
        }

        sprintf_s(path, 512, "%s/ps.hlsl", m_path.data());

        str = "shader/";
        str += path;

        t = std::ifstream(str.c_str());
        if(t.good())
        {
            t.seekg(0, std::ios::end);
            size_t size = t.tellg();
            eastl::string buffer(size, ' ');
            t.seekg(0);
            t.read(&buffer[0], size);
            meow_u128 hash = MeowHash(MeowDefaultSeed, buffer.size(), buffer.data());

            if(!MeowHashesAreEqual(hash, m_hashes[1]))
            {
                m_hashes[1] = hash;
                RefCntAutoPtr<IShader> pPS;
                {
                    m_info.Desc.ShaderType = SHADER_TYPE_PIXEL;
                    m_info.EntryPoint = "main";
                    m_info.Desc.Name = path;
                    m_info.Source = buffer.c_str();
                    m_info.SourceLength = buffer.size();
                    device->CreateShader(m_info, &pPS);
                }

                m_shaders[1] = pPS;
                std::cout << "Compiled " << path << " successfully" << std::endl;
            }
            else
            {
                //std::cout << "Hashes are equal for " << path << std::endl;
                hasNotSameHash = false;
            }
        }
        else
        {
            return false;
        }
    }
    else if (m_type == Diligent::PIPELINE_TYPE_COMPUTE)
    {
        char path[512];

        sprintf_s(path, 512, "%s/cs.hlsl", m_path.data());
        eastl::string str = "shader/";
        str += path;

        std::ifstream t(str.c_str());
        if(t.good())
        {
            t.seekg(0, std::ios::end);
            size_t size = t.tellg();
            eastl::string buffer(size, ' ');
            t.seekg(0);
            t.read(&buffer[0], size);
            meow_u128 hash = MeowHash(MeowDefaultSeed, buffer.size(), buffer.data());

            if (!MeowHashesAreEqual(hash, m_hashes[0]))
            {
                m_hashes[0] = hash;
                t.close();
                // Create a vertex shader
                RefCntAutoPtr<IShader> pCS;
                {
                    m_info.Desc.ShaderType = SHADER_TYPE_COMPUTE;
                    m_info.EntryPoint = "main";
                    m_info.Desc.Name = path;
                    m_info.Source = buffer.c_str();
                    m_info.SourceLength = buffer.size();

                    device->CreateShader(m_info, &pCS);
                    std::cout << "Compiled " << path << " successfully" << std::endl;
                    m_shaders[0] = pCS;
                }
            }
            else
            {
                //std::cout << "Hashes are equal for " << path << std::endl;
                hasNotSameHash = false;
            }
        }
        else
        {
            return false;
        }
    }

    return hasNotSameHash;
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
    //needed for ref counting
#ifdef _DEBUG
    std::cout << "Deleting shader: " << m_path.c_str() << std::endl;
#endif
}

Shader::~Shader()
{
#ifdef _DEBUG
    std::cout << "Deleting shader: " << m_path.c_str() << std::endl;
#endif
}
