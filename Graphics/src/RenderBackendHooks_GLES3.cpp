#include "stdafx.h"
#include "RenderBackendHooks.hpp"

namespace Graphics::RenderBackendHooks
{
	uint32 FloatAttribType(size_t)
	{
		// GLES has no GL_DOUBLE vertex attribute support.
		return GL_FLOAT;
	}

	void DestroyMainProgramPipeline(uint32)
	{
		// GLES doesn't use separate shader objects/program pipelines - nothing to do.
	}

	uint32 DepthTextureInternalFormat()
	{
		return GL_DEPTH_COMPONENT16;
	}

	void ApplyAnisotropicFiltering(float)
	{
		// Not supported.
	}

	void UnbindProgramAfterDraw()
	{
		glUseProgram(0);
	}

	void SetPointSize(float)
	{
		// GLES has no fixed-function point size state; set gl_PointSize in the shader instead.
	}

	void ReadTexture2DPixels(const Vector2i& size, void* pData)
	{
		glReadPixels(0, 0, size.x, size.y, GL_RGBA, GL_UNSIGNED_BYTE, pData);
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
