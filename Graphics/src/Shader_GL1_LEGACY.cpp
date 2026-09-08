/*
	GL1_LEGACY has no GLSL at all (no G4-era GPU in the supported hardware range - GeForce
	2/4MX, Rage128, Radeon 9200 - implements it), so a "shader" here is just a lightweight
	tag: it records which skin asset path was requested and succeeds unconditionally,
	without touching the file's GLSL source. Material_GL1_LEGACY.cpp is what actually drives
	fixed-function state (glTexEnv/blend/vertex color) at bind time, generically from
	whatever MaterialParameterSet is passed in - not from shader content, so no per-shader
	translation table is needed here. See humming-sleeping-stonebraker.md.
*/
#include "stdafx.h"
#include "Shader_Impl.hpp"
#include "OpenGL.hpp"

namespace Graphics
{
	// Declared (to satisfy Shader_Impl.hpp's extern) but never actually read - GL1_LEGACY
	// never calls glCreateShader, so there's no real GL enum to map ShaderType to, and
	// GL_VERTEX_SHADER/GL_FRAGMENT_SHADER aren't reliably defined by the classic
	// OpenGL/gl.h + glext.h header pair this backend uses (they're GL2.0/ARB_shader_objects
	// tokens). Placeholder values only.
	const uint32 typeMap[] =
	{
		0,
		1,
	};

	bool Shader_Impl::LoadProgram(uint32& programOut)
	{
		// Nothing to compile - just confirm the asset exists so Material::Create's
		// "shader failed to load" error path still behaves the same as other backends.
		File in;
		if(!in.OpenRead(m_sourcePath))
			return false;
		programOut = 1; // non-zero sentinel; GL1_LEGACY never dereferences this as a real GL object
		return true;
	}

	bool Shader_Impl::Init(ShaderType type, const String& name)
	{
		m_sourcePath = Path::Normalize(name);
		m_type = type;
		return LoadProgram(m_prog);
	}
}
