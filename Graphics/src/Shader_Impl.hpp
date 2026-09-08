#pragma once
#include "Shader.hpp"

namespace Graphics
{
#ifdef USC_SEPARATE_SHADER_OBJECTS
	extern const uint32 typeMap[];
	extern const uint32 shaderStageMap[];
#else
	extern const uint32 typeMap[];
#endif

	class Shader_Impl : public ShaderRes
	{
	public:
		ShaderType m_type;
		uint32 m_prog = 0;
		OpenGL* m_gl;

		String m_sourcePath;

#ifdef _WIN32
		HANDLE m_changeNotification = INVALID_HANDLE_VALUE;
		uint64 m_lwt = -1;
#endif

		Shader_Impl(OpenGL* gl) : m_gl(gl) {}
		~Shader_Impl();

		void SetupChangeHandler();

		// Backend-specific: see Shader_GL3.cpp / Shader_GLES3.cpp / Shader_GL1_LEGACY.cpp
		bool LoadProgram(uint32& programOut);

		bool UpdateHotReload() override;
		bool Init(ShaderType type, const String& name);

#ifdef USC_SEPARATE_SHADER_OBJECTS
		void Bind() override;
		bool IsBound() const override;
		uint32 GetLocation(const String& name) const override;
		virtual void BindUniform(uint32 loc, const Transform& mat);
		virtual void BindUniformVec2(uint32 loc, const Vector2& v);
		virtual void BindUniformVec3(uint32 loc, const Vector3& v);
		virtual void BindUniformVec4(uint32 loc, const Vector4& v);
		virtual void BindUniform(uint32 loc, int i);
		virtual void BindUniform(uint32 loc, float i);
		virtual void BindUniformArray(uint32 loc, const Transform* mat, size_t count);
		virtual void BindUniformArray(uint32 loc, const Vector2* v2, size_t count);
		virtual void BindUniformArray(uint32 loc, const Vector3* v3, size_t count);
		virtual void BindUniformArray(uint32 loc, const Vector4* v4, size_t count);
		virtual void BindUniformArray(uint32 loc, const float* i, size_t count);
		virtual void BindUniformArray(uint32 loc, const int* i, size_t count);
#endif
		uint32 Handle() override
		{
			return m_prog;
		}
		String GetOriginalName() const override
		{
			return m_sourcePath;
		}
	};
}
