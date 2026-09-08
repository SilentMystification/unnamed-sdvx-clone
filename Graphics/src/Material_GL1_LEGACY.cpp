/*
	GL1_LEGACY has no GLSL, so "binding a material" here means driving the fixed-function
	pipeline directly, generically from whatever MaterialParameterSet is actually passed at
	draw time (not from which shader asset paths were requested - see Shader_GL1_LEGACY.cpp):
	- a GL_SAMPLER_2D param binds a texture to unit 0 and enables GL_TEXTURE_2D/modulate
	- a GL_FLOAT_VEC4 param is treated as a vertex tint, applied via glColor4fv
	- everything else (per-shader uniforms with no fixed-function equivalent) is ignored

	This intentionally degrades fidelity on effects that lean on real per-pixel shader math
	(laser glow, particle blending, scanline overlay) - an accepted tradeoff, see
	humming-sleeping-stonebraker.md. World/camera/projection transforms are replicated
	exactly via the legacy matrix stacks (glLoadMatrixf) instead of shader uniforms; see the
	Transform layout/multiplication-order confirmation in that plan file - Transform::mat is
	column-major and directly glLoadMatrixf-compatible, and GL_MODELVIEW = cameraTransform *
	worldTransform matches the shaders' `proj * camera * world * pos` exactly.
*/
#include "stdafx.h"
#include "Material_Impl.hpp"
#include "OpenGL.hpp"
#include "RenderQueue.hpp"

namespace Graphics
{
	Material_Impl::Material_Impl(OpenGL* gl) : m_gl(gl)
	{
	}
	Material_Impl::~Material_Impl()
	{
	}

	void Material_Impl::AssignShader(ShaderType t, Shader shader)
	{
		// No reflection is possible without a real linked program - just record it so
		// m_shaders[] stays populated for debug names / hot-reload bookkeeping.
		m_shaders[(size_t)t] = shader;
	}

	void Material_Impl::Bind(const RenderState& rs, const MaterialParameterSet& params)
	{
		m_cachedCameraTransform = rs.cameraTransform;

		glMatrixMode(GL_PROJECTION);
		glLoadMatrixf(rs.projectionTransform.mat);
		glMatrixMode(GL_MODELVIEW);

		BindParameters(params, rs.worldTransform);
		BindToContext();
	}

	void Material_Impl::BindParameters(const MaterialParameterSet& params, const Transform& worldTransform)
	{
		Transform modelView = m_cachedCameraTransform * worldTransform;
		glMatrixMode(GL_MODELVIEW);
		glLoadMatrixf(modelView.mat);

		bool hasTexture = false;
		bool hasColor = false;
		Vector4 color(1.0f, 1.0f, 1.0f, 1.0f);

		for(auto p : params)
		{
			switch(p.second.parameterType)
			{
			case GL_SAMPLER_2D:
			{
				// No fixed-function equivalent for compositing multiple textures (masks,
				// overlays) in one draw - a material with 2+ GL_SAMPLER_2D params (the skin
				// "gauge" shader's mainTex+maskTex, some track materials' base+overlay
				// layers) previously just showed whichever one this loop iterated last,
				// arbitrarily, since nothing preferred one name over another - real hardware
				// showed this as a masked-gauge-texture-instead-of-fill-texture, and very
				// likely the "red striped" track appearance too. "mainTex" is the
				// convention for the primary/base texture across the skin shaders (see
				// gameplay.lua's LoadGauge, Track.cpp's material setup) - same
				// name-preference pattern the color param below already uses for "color".
				if(hasTexture && p.first != "mainTex")
					break;
				Ref<TextureRes> texture = p.second.Get<Ref<TextureRes>>();
				if(texture)
				{
					texture->Bind(0);
					hasTexture = true;
				}
				break;
			}
			case GL_FLOAT_VEC4:
				// "color" is the convention used throughout Main/ for the vertex tint
				// (see Track.cpp/GuiUtils.cpp/etc.) - prefer it if present, otherwise
				// fall back to the last vec4 param found (best-effort).
				if(!hasColor || p.first == "color")
					color = p.second.Get<Vector4>();
				hasColor = true;
				break;
			default:
				// No fixed-function equivalent for this parameter type - ignore it.
				break;
			}
		}

		// GL_TEXTURE_ENV_MODE is set once at context Init() (always GL_MODULATE, never
		// changes) - only the enable bit and bound texture vary per draw here.
		if(hasTexture)
			glEnable(GL_TEXTURE_2D);
		else
			glDisable(GL_TEXTURE_2D);

		if(hasColor)
			glColor4fv(&color.x);
		else
			glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	}

	void Material_Impl::BindToContext()
	{
		// No program/pipeline object to bind - all state is set directly in
		// BindParameters(), which RenderQueue.cpp always calls alongside this.
	}

	bool Material_Impl::HasUniform(String name)
	{
		// No reflection is possible without a real linked program; accept any name so
		// callers that gate SetParameter() on this don't skip essential setup - unknown
		// parameter types are silently ignored in BindParameters() instead.
		return true;
	}

	// BindAll<T>/BindShaderVar<T> (declared in Material_Impl.hpp for the other backends'
	// shader-reflection-based uniform binding) are never called here - parameters are
	// consumed directly and generically in BindParameters() above instead - so they need
	// no definition in this translation unit.
}
