#include "stdafx.h"
#include "Shader_Impl.hpp"
#include "OpenGL.hpp"

namespace Graphics
{
	const uint32 typeMap[] =
	{
		GL_VERTEX_SHADER,
		GL_FRAGMENT_SHADER,
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
		sourceStr = "#version 100\n#define EMBEDDED\n#define target gl_FragColor\n#define texture texture2D\nprecision mediump float;\n" + sourceStr;
		const GLint programsize = sourceStr.size();

		const char* pChars = *sourceStr;
		glShaderSource(programOut, 1, &pChars, &programsize);
		glCompileShader(programOut);

		int nStatus = 0;
		glGetShaderiv(programOut, GL_COMPILE_STATUS, &nStatus);
		if(nStatus == GL_FALSE)
		{
			static char infoLogBuffer[2048];
			int s = 0;
			glGetShaderInfoLog(programOut, sizeof(infoLogBuffer), &s, infoLogBuffer);

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
		m_prog = glCreateShader(typeMap[(size_t)type]);
		return LoadProgram(m_prog);
	}

	void ShaderRes::Unbind(class OpenGL* gl, ShaderType type)
	{
		// GLES shaders are unlinked objects with no program handle of their own -
		// binding happens at the Material's linked program instead, nothing to do here.
	}
}
