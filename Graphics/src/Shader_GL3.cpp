#include "stdafx.h"
#include "Shader_Impl.hpp"
#include "OpenGL.hpp"

namespace Graphics
{
	const uint32 typeMap[] =
	{
		GL_VERTEX_SHADER,
		GL_FRAGMENT_SHADER,
		GL_GEOMETRY_SHADER,
	};
	const uint32 shaderStageMap[] =
	{
		GL_VERTEX_SHADER_BIT,
		GL_FRAGMENT_SHADER_BIT,
		GL_GEOMETRY_SHADER_BIT,
	};

	bool Shader_Impl::LoadProgram(uint32& programOut)
	{
		File in;
		if(!in.OpenRead(m_sourcePath))
			return false;

		String sourceStr;
		sourceStr.resize(in.GetSize());
		if(sourceStr.size() == 0)
			return false;

		in.Read(&sourceStr.front(), sourceStr.size());
		String firstLine;
		sourceStr.Split("\n", &firstLine, nullptr);
		firstLine.Trim('\r');
		firstLine.ToLower();
		if (firstLine.compare("#version 330") != 0)
		{
			sourceStr = "#version 330\n" + sourceStr;
		}
		const char* pChars = *sourceStr;
		programOut = glCreateShaderProgramv(typeMap[(size_t)m_type], 1, &pChars);
		if(programOut == 0)
			return false;

		int nStatus = 0;
		glGetProgramiv(programOut, GL_LINK_STATUS, &nStatus);
		if(nStatus == 0)
		{
			static char infoLogBuffer[2048];
			int s = 0;
			glGetProgramInfoLog(programOut, sizeof(infoLogBuffer), &s, infoLogBuffer);

			Logf("Shader program compile log for %s: %s", Logger::Severity::Error, m_sourcePath, infoLogBuffer);
			return false;
		}

		// Shader hot-reload in debug mode
#if defined(_DEBUG) && defined(_WIN32)
		// Store last write time
		m_lwt = in.GetLastWriteTime();
		SetupChangeHandler();
#endif
		return true;
	}

	bool Shader_Impl::Init(ShaderType type, const String& name)
	{
		m_sourcePath = Path::Normalize(name);
		m_type = type;
		return LoadProgram(m_prog);
	}

	void Shader_Impl::Bind()
	{
		if(m_gl->m_activeShaders[(size_t)m_type] != this)
		{
			glUseProgramStages(m_gl->m_mainProgramPipeline, shaderStageMap[(size_t)m_type], m_prog);
			m_gl->m_activeShaders[(size_t)m_type] = this;
		}
	}
	bool Shader_Impl::IsBound() const
	{
		return m_gl->m_activeShaders[(size_t)m_type] == this;
	}
	uint32 Shader_Impl::GetLocation(const String& name) const
	{
		return glGetUniformLocation(m_prog, name.c_str());
	}
	void Shader_Impl::BindUniform(uint32 loc, const Transform& mat)
	{
		glProgramUniformMatrix4fv(m_prog, loc, 1, false, mat.mat);
	}
	void Shader_Impl::BindUniformVec2(uint32 loc, const Vector2& v)
	{
		glProgramUniform2fv(m_prog, loc, 1, &v.x);
	}
	void Shader_Impl::BindUniformVec3(uint32 loc, const Vector3& v)
	{
		glProgramUniform3fv(m_prog, loc, 1, &v.x);
	}
	void Shader_Impl::BindUniformVec4(uint32 loc, const Vector4& v)
	{
		glProgramUniform4fv(m_prog, loc, 1, &v.x);
	}
	void Shader_Impl::BindUniform(uint32 loc, int i)
	{
		glProgramUniform1i(m_prog, loc, i);
	}
	void Shader_Impl::BindUniform(uint32 loc, float i)
	{
		glProgramUniform1f(m_prog, loc, i);
	}
	void Shader_Impl::BindUniformArray(uint32 loc, const Transform* mat, size_t count)
	{
		glProgramUniformMatrix4fv(m_prog, loc, (int)count, false, (float*)mat);
	}
	void Shader_Impl::BindUniformArray(uint32 loc, const Vector2* v2, size_t count)
	{
		glProgramUniform2fv(m_prog, loc, (int)count, (float*)v2);
	}
	void Shader_Impl::BindUniformArray(uint32 loc, const Vector3* v3, size_t count)
	{
		glProgramUniform3fv(m_prog, loc, (int)count, (float*)v3);
	}
	void Shader_Impl::BindUniformArray(uint32 loc, const Vector4* v4, size_t count)
	{
		glProgramUniform4fv(m_prog, loc, (int)count, (float*)v4);
	}
	void Shader_Impl::BindUniformArray(uint32 loc, const float* i, size_t count)
	{
		glProgramUniform1fv(m_prog, loc, (int)count, i);
	}
	void Shader_Impl::BindUniformArray(uint32 loc, const int* i, size_t count)
	{
		glProgramUniform1iv(m_prog, loc, (int)count, i);
	}

	void ShaderRes::Unbind(class OpenGL* gl, ShaderType type)
	{
		if(gl->m_activeShaders[(size_t)type] != 0)
		{
			glUseProgramStages(gl->m_mainProgramPipeline, shaderStageMap[(size_t)type], 0);
			gl->m_activeShaders[(size_t)type] = 0;
		}
	}
}
