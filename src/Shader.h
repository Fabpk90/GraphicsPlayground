#pragma once
#include <array>

#include "Graphics/GraphicsEngine/interface/Shader.h"
#include "Common/interface/RefCntAutoPtr.hpp"
#include "PipelineState.h"

class Shader
{
public:
	enum class EShaderStage { Vertex, Pixel, Compute };

	Shader(const char* _path, Diligent::PIPELINE_TYPE _type);

    void Release();

	bool reload();
	Diligent::RefCntAutoPtr<Diligent::IShader> getShaderStage(EShaderStage _stage) const;
private:
	std::array<Diligent::RefCntAutoPtr<Diligent::IShader>, 2> m_shaders; // todo: do we only need 2 max ?
	std::string m_path;
	Diligent::PIPELINE_TYPE m_type;

	Diligent::ShaderCreateInfo m_info;
};
