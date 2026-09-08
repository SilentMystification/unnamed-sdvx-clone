#include "stdafx.h"
#include "Material_Impl.hpp"
#include "OpenGL.hpp"
#include "RenderQueue.hpp"

namespace Graphics
{
	Material_Impl::Material_Impl(OpenGL* gl) : m_gl(gl)
	{
		m_program = glCreateProgram();
	}
	Material_Impl::~Material_Impl()
	{
		if (glIsProgram(m_program))
			glDeleteProgram(m_program);
	}

	void Material_Impl::AssignShader(ShaderType t, Shader shader)
	{
		m_shaders[(size_t)t] = shader;

		if (shader.get() == nullptr)
			return;

		uint32 handle = shader->Handle();

#ifdef _DEBUG
		Logf("Listing shader uniforms for %s", Logger::Severity::Info, shader->GetOriginalName());
#endif // _DEBUG

		glAttachShader(m_program, handle);
		glLinkProgram(m_program);

		int32 numUniforms;
		glGetProgramiv(m_program, GL_ACTIVE_UNIFORMS, &numUniforms);

		for(int32 i = 0; i < numUniforms; i++)
		{
			char name[64];
			int32 nameLen, size;
			uint32 type;
			glGetActiveUniform(m_program, i, sizeof(name), &nameLen, &size, &type, name);
			uint32 loc = glGetUniformLocation(m_program, name);
			m_uniforms.Add(name);
			// Select type
			uint32 textureID = 0;
			String typeName = "Unknown";
			if(type == GL_SAMPLER_2D)
			{
				typeName = "Sampler2D";
				if(!m_textureIDs.Contains(name))
					m_textureIDs.Add(name, m_textureID++);
			}
			else if(type == GL_FLOAT_MAT4)
			{
				typeName = "Transform";
			}
			else if(type == GL_FLOAT_VEC4)
			{
				typeName = "Vector4";
			}
			else if(type == GL_FLOAT_VEC3)
			{
				typeName = "Vector3";
			}
			else if(type == GL_FLOAT_VEC2)
			{
				typeName = "Vector2";
			}
			else if(type == GL_FLOAT)
			{
				typeName = "Float";
			}

			// Built in variable?
			uint32 targetID = 0;
			if(builtInShaderVariableMap.Contains(name))
			{
				targetID = builtInShaderVariableMap[name];
			}
			else
			{
				if(m_mappedParameters.Contains(name))
					targetID = m_mappedParameters[name];
				else
					targetID = m_mappedParameters.Add(name, m_userID++);
			}

			BoundParameterInfo& param = m_boundParameters.FindOrAdd(targetID).Add(BoundParameterInfo(t, type, loc));

#ifdef _DEBUG
			Logf("Uniform [%d, loc=%d, %s] = %s", Logger::Severity::Info,
				i, loc, Utility::Sprintf("Unknown [%d]", type), name);
#endif // _DEBUG
		}
	}

	void Material_Impl::Bind(const RenderState& rs, const MaterialParameterSet& params)
	{
#if _DEBUG
		bool reloadedShaders = false;
		for(uint32 i = 0; i < 3; i++)
		{
			if(m_shaders[i] && m_shaders[i]->UpdateHotReload())
			{
				reloadedShaders = true;
			}
		}

		// Regenerate parameter map
		if(reloadedShaders)
		{
			Log("Reloading material", Logger::Severity::Info);
			m_boundParameters.clear();
			m_textureIDs.clear();
			m_mappedParameters.clear();
			m_userID = SV_User;
			m_textureID = 0;
			for(uint32 i = 0; i < 3; i++)
			{
				if(m_shaders[i])
					AssignShader(ShaderType(i), m_shaders[i]);
				glLinkProgram(m_program);
			}
		}
#endif
		BindToContext();

		// Bind renderstate variables
		BindAll(SV_Proj, rs.projectionTransform);
		BindAll(SV_Camera, rs.cameraTransform);
		BindAll(SV_Viewport, rs.viewportSize);
		BindAll(SV_AspectRatio, rs.aspectRatio);
		Transform billboard = CameraMatrix::BillboardMatrix(rs.cameraTransform);
		BindAll(SV_BillboardMatrix, billboard);
		BindAll(SV_Time, rs.time);

		// Bind parameters
		BindParameters(params, rs.worldTransform);
	}

	void Material_Impl::BindParameters(const MaterialParameterSet& params, const Transform& worldTransform)
	{
		BindAll(SV_World, worldTransform);
		for(auto p : params)
		{
			switch(p.second.parameterType)
			{
			case GL_INT:
				BindAll(p.first, p.second.Get<int>());
				break;
			case GL_FLOAT:
				BindAll(p.first, p.second.Get<float>());
				break;
			case GL_INT_VEC2:
				BindAll(p.first, p.second.Get<Vector2i>());
				break;
			case GL_INT_VEC3:
				BindAll(p.first, p.second.Get<Vector3i>());
				break;
			case GL_INT_VEC4:
				BindAll(p.first, p.second.Get<Vector4i>());
				break;
			case GL_FLOAT_VEC2:
				BindAll(p.first, p.second.Get<Vector2>());
				break;
			case GL_FLOAT_VEC3:
				BindAll(p.first, p.second.Get<Vector3>());
				break;
			case GL_FLOAT_VEC4:
				BindAll(p.first, p.second.Get<Vector4>());
				break;
			case GL_FLOAT_MAT4:
				BindAll(p.first, p.second.Get<Transform>());
				break;
			case GL_SAMPLER_2D:
			{
				uint32* textureUnit = m_textureIDs.Find(p.first);
				if(!textureUnit)
				{
					break;
				}
				Ref<TextureRes> texture = p.second.Get<Ref<TextureRes>>();

				// Bind the texture
				texture->Bind(*textureUnit);

				// Bind sampler
				BindAll<int32>(p.first, *textureUnit);
				break;
			}
			default:
				assert(false);
			}
		}
	}

	void Material_Impl::BindToContext()
	{
		glUseProgram(m_program);
	}

	bool Material_Impl::HasUniform(String name)
	{
		return m_uniforms.Contains(name);
	}

	template<typename T> void Material_Impl::BindAll(const String& name, const T& obj)
	{
		uint32 num = 0;
		glUseProgram(m_program);
		BoundParameterInfo* bp = GetBoundParameters(name, num);
		for(uint32 i = 0; bp && i < num; i++)
		{
			BindShaderVar<T>(m_shaders[(size_t)bp[i].shaderType]->Handle(), bp[i].location, obj);
		}
	}
	template<typename T> void Material_Impl::BindAll(BuiltInShaderVariable bsv, const T& obj)
	{
		uint32 num = 0;
		glUseProgram(m_program);
		BoundParameterInfo* bp = GetBoundParameters(bsv, num);
		for(uint32 i = 0; bp && i < num; i++)
		{
			BindShaderVar<T>(m_shaders[(size_t)bp[i].shaderType]->Handle(), bp[i].location, obj);
		}
	}

	template<> void Material_Impl::BindShaderVar<Vector4>(uint32 shader, uint32 loc, const Vector4& obj)
	{
		glUniform4fv(loc, 1, &obj.x);
	}
	template<> void Material_Impl::BindShaderVar<Vector3>(uint32 shader, uint32 loc, const Vector3& obj)
	{
		glUniform3fv(loc, 1, &obj.x);
	}
	template<> void Material_Impl::BindShaderVar<Vector2>(uint32 shader, uint32 loc, const Vector2& obj)
	{
		glUniform2fv(loc, 1, &obj.x);
	}
	template<> void Material_Impl::BindShaderVar<float>(uint32 shader, uint32 loc, const float& obj)
	{
		glUniform1fv(loc, 1, &obj);
	}
	template<> void Material_Impl::BindShaderVar<Colori>(uint32 shader, uint32 loc, const Colori& obj)
	{
		Color c = obj;
		glUniform4fv(loc, 1, &c.x);
	}
	template<> void Material_Impl::BindShaderVar<Vector4i>(uint32 shader, uint32 loc, const Vector4i& obj)
	{
		glUniform4iv(loc, 1, &obj.x);
	}
	template<> void Material_Impl::BindShaderVar<Vector3i>(uint32 shader, uint32 loc, const Vector3i& obj)
	{
		glUniform3iv(loc, 1, &obj.x);
	}
	template<> void Material_Impl::BindShaderVar<Vector2i>(uint32 shader, uint32 loc, const Vector2i& obj)
	{
		glUniform2iv(loc, 1, &obj.x);
	}
	template<> void Material_Impl::BindShaderVar<int32>(uint32 shader, uint32 loc, const int32& obj)
	{
		glUniform1iv(loc, 1, &obj);
	}
	template<> void Material_Impl::BindShaderVar<Transform>(uint32 shader, uint32 loc, const Transform& obj)
	{
		glUniformMatrix4fv(loc, 1, GL_FALSE, obj.mat);
	}
}
