#pragma once
#include "Material.hpp"

namespace Graphics
{
	// Defines build in shader variables
	enum BuiltInShaderVariable
	{
		SV_World = 0,
		SV_Proj,
		SV_Camera,
		SV_BillboardMatrix,
		SV_Viewport,
		SV_AspectRatio,
		SV_Time,
		SV__BuiltInEnd,
		SV_User = 0x100, // Start defining user variables here
	};

	struct BoundParameterInfo
	{
		BoundParameterInfo(ShaderType shaderType, uint32 paramType, uint32 location)
			:shaderType(shaderType), paramType(paramType), location(location)
		{
		}

		ShaderType shaderType;
		uint32 paramType;
		uint32 location;
	};
	struct BoundParameterList : public Vector<BoundParameterInfo>
	{
	};

	// Defined in Material.cpp; used by AssignShader() in the per-backend Material files.
	extern Map<String, BuiltInShaderVariable> builtInShaderVariableMap;

	class Material_Impl : public MaterialRes
	{
	public:
		OpenGL* m_gl;
		Shader m_shaders[3];
#if _DEBUG
		String m_debugNames[3];
#endif
#ifdef EMBEDDED
		uint32 m_program;
#elif defined(USC_SEPARATE_SHADER_OBJECTS)
		uint32 m_pipeline;
#endif
		// GL1_LEGACY has no program object at all - no GLSL, no glCreateProgram; Material
		// drives fixed-function state (glTexEnv/blend/vertex color) directly instead. See
		// Material_GL1_LEGACY.cpp.
#ifdef USC_GL1_LEGACY
		// GL_MODELVIEW = cameraTransform * worldTransform, but BindParameters() (called
		// per draw item, possibly without an intervening Bind()) only receives
		// worldTransform - cache the per-frame camera transform from Bind() to combine
		// with it there.
		Transform m_cachedCameraTransform;
#endif
		Map<uint32, BoundParameterList> m_boundParameters;
		Map<String, uint32> m_mappedParameters;
		Map<String, uint32> m_textureIDs;
		uint32 m_userID = SV_User;
		uint32 m_textureID = 0;
		Set<String> m_uniforms;

		Material_Impl(OpenGL* gl);
		~Material_Impl();

		void AssignShader(ShaderType t, Shader shader) override;
		void Bind(const RenderState& rs, const MaterialParameterSet& params) override;
		void BindParameters(const MaterialParameterSet& params, const Transform& worldTransform) override;
		void BindToContext() override;
		bool HasUniform(String name) override;

		BoundParameterInfo* GetBoundParameters(const String& name, uint32& count)
		{
			uint32* mappedID = m_mappedParameters.Find(name);
			if(!mappedID)
				return nullptr;
			return GetBoundParameters((BuiltInShaderVariable)*mappedID, count);
		}
		BoundParameterInfo* GetBoundParameters(BuiltInShaderVariable bsv, uint32& count)
		{
			BoundParameterList* l = m_boundParameters.Find(bsv);
			if(!l)
				return nullptr;
			else
			{
				count = (uint32)l->size();
				return l->data();
			}
		}

		// Backend-specific bodies (need the backend's BindShaderVar specializations
		// visible at the point of use) - see Material_GL3.cpp / Material_GLES3.cpp / Material_GL1_LEGACY.cpp
		template<typename T> void BindAll(const String& name, const T& obj);
		template<typename T> void BindAll(BuiltInShaderVariable bsv, const T& obj);

		template<typename T> void BindShaderVar(uint32 shader, uint32 loc, const T& obj)
		{
			static_assert(sizeof(T) != 0, "Incompatible shader uniform type");
		}
	};
}
