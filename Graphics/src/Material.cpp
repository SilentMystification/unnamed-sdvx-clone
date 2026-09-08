#include "stdafx.h"
#include "Material_Impl.hpp"
#include "OpenGL.hpp"
#include <Graphics/ResourceManagers.hpp>
#include "RenderQueue.hpp"

namespace Graphics
{
	const char* builtInShaderVariableNames[] =
	{
		"world",
		"proj",
		"camera",
		"billboard",
		"viewport",
		"aspectRatio",
		"time",
	};
	static Map<String, BuiltInShaderVariable> MakeBuiltInShaderVariableMap()
	{
		Map<String, BuiltInShaderVariable> map;
		for(int32 i = 0; i < SV__BuiltInEnd; i++)
		{
			map.Add(builtInShaderVariableNames[i], (BuiltInShaderVariable)i);
		}
		return map;
	}
	Map<String, BuiltInShaderVariable> builtInShaderVariableMap = MakeBuiltInShaderVariableMap();

	Material MaterialRes::Create(OpenGL* gl)
	{
		Material_Impl* impl = new Material_Impl(gl);
		return GetResourceManager<ResourceType::Material>().Register(impl);

	}
	Material MaterialRes::Create(OpenGL* gl, const String& vsPath, const String& fsPath)
	{
		Material_Impl* impl = new Material_Impl(gl);
		impl->AssignShader(ShaderType::Vertex, ShaderRes::Create(gl, ShaderType::Vertex, vsPath));
		impl->AssignShader(ShaderType::Fragment, ShaderRes::Create(gl, ShaderType::Fragment, fsPath));
#if _DEBUG
		impl->m_debugNames[(size_t)ShaderType::Vertex] = vsPath;
		impl->m_debugNames[(size_t)ShaderType::Fragment] = fsPath;
#endif

		if(!impl->m_shaders[(size_t)ShaderType::Vertex])
		{
			Logf("Failed to load vertex shader for material from %s", Logger::Severity::Error, vsPath);
			delete impl;
			return Material();
		}
		if(!impl->m_shaders[(size_t)ShaderType::Fragment])
		{
			Logf("Failed to load fragment shader for material from %s", Logger::Severity::Error, fsPath);
			delete impl;
			return Material();
		}

		return GetResourceManager<ResourceType::Material>().Register(impl);
	}

	void MaterialParameterSet::SetParameter(const String& name, int sc)
	{
		Add(name, MaterialParameter::Create(sc, GL_INT));
	}
	void MaterialParameterSet::SetParameter(const String& name, float sc)
	{
		Add(name, MaterialParameter::Create(sc, GL_FLOAT));
	}
	void MaterialParameterSet::SetParameter(const String& name, const Vector4& vec)
	{
		Add(name, MaterialParameter::Create(vec, GL_FLOAT_VEC4));
	}
	void MaterialParameterSet::SetParameter(const String& name, const Colori& color)
	{
		Add(name, MaterialParameter::Create(Color(color), GL_FLOAT_VEC4));
	}
	void MaterialParameterSet::SetParameter(const String& name, const Vector2& vec2)
	{
		Add(name, MaterialParameter::Create(vec2, GL_FLOAT_VEC2));
	}
	void MaterialParameterSet::SetParameter(const String& name, const Vector3& vec3)
	{
		Add(name, MaterialParameter::Create(vec3, GL_FLOAT_VEC3));
	}
	void MaterialParameterSet::SetParameter(const String& name, const Transform& tf)
	{
		Add(name, MaterialParameter::Create(tf, GL_FLOAT_MAT4));
	}
	void MaterialParameterSet::SetParameter(const String& name, Ref<class TextureRes> tex)
	{
		Add(name, MaterialParameter::Create(tex, GL_SAMPLER_2D));
	}
	void MaterialParameterSet::SetParameter(const String& name, const Vector2i& vec2)
	{
		Add(name, MaterialParameter::Create(vec2, GL_INT_VEC2));
	}
}
