#include "stdafx.h"
#include "RenderBackendHooks.hpp"

namespace Graphics::RenderBackendHooks
{
	uint32 FloatAttribType(size_t componentSize)
	{
		if(componentSize == 4)
			return GL_FLOAT;
		else if(componentSize == 8)
			return GL_DOUBLE;
		assert(false);
		return GL_FLOAT;
	}

	void DestroyMainProgramPipeline(uint32 pipeline)
	{
		if(pipeline)
		{
			glDeleteProgramPipelines(1, &pipeline);
		}
	}

	uint32 DepthTextureInternalFormat()
	{
		return GL_DEPTH_COMPONENT32;
	}

	void ApplyAnisotropicFiltering(float anisotropic)
	{
		if(GL_TEXTURE_MAX_ANISOTROPY_EXT)
		{
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, anisotropic);
		}
	}

	void UnbindProgramAfterDraw()
	{
		// Desktop shaders are bound per-object via the program pipeline; nothing to do.
	}

	void SetPointSize(float size)
	{
		glPointSize(size);
	}

	void ReadTexture2DPixels(const Vector2i& size, void* pData)
	{
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pData);
	}

	bool SupportsMipmapGeneration()
	{
		return true;
	}
	void BindDefaultReadFramebuffer()
	{
		glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	}
	void GenerateMipmap()
	{
		glGenerateMipmap(GL_TEXTURE_2D);
	}
}
