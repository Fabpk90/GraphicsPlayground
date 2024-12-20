#pragma once
#include <EASTL/array.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

#include "Graphics/GraphicsEngine/interface/Shader.h"
#include "Common/interface/RefCntAutoPtr.hpp"
#include "PipelineState.h"
#include "util/meow_hash_x64_aesni.h"
#include "Graphics/GraphicsTools/interface/ShaderMacroHelper.hpp"
class Shader
{
public:
	enum class EShaderStage { Vertex, Pixel, Compute };

	Shader(const char* _path, Diligent::PIPELINE_TYPE _type, eastl::vector<eastl::pair<eastl::string, eastl::string>>  _macros = {});
    ~Shader();

    void Release();

	bool reload();
	[[nodiscard]] Diligent::RefCntAutoPtr<Diligent::IShader> getShaderStage(EShaderStage _stage) const;
private:
	eastl::array<Diligent::RefCntAutoPtr<Diligent::IShader>, 2> m_shaders; // todo: do we only need 2 max ?
	eastl::string m_path;
	Diligent::PIPELINE_TYPE m_type;
    eastl::vector<eastl::pair<eastl::string, eastl::string>> m_macros;
    Diligent::ShaderMacroHelper m_macroHelper;

	Diligent::ShaderCreateInfo m_info;
    eastl::array<meow_u128, 2> m_hashes;
};
