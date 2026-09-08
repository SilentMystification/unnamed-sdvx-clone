#include "stdafx.h"
#include "RenderBackendHooks.hpp"

namespace Graphics::RenderBackendHooks
{
	uint32 FloatAttribType(size_t componentSize)
	{
		// Used by Mesh_GL1_LEGACY.cpp's SetData() to pick the glVertexPointer/
		// glTexCoordPointer component type - GL1 has no GL_DOUBLE vertex attribute
		// support any more than GLES does.
		if(componentSize == 4)
			return GL_FLOAT;
		assert(false);
		return GL_FLOAT;
	}

	void DestroyMainProgramPipeline(uint32)
	{
		// No program pipeline object exists on this backend - nothing to do.
	}

	uint32 DepthTextureInternalFormat()
	{
		// ARB_depth_texture (GL1.4-era, ~2002) - present on GeForce4MX/Radeon 9200, not
		// guaranteed on the oldest Rage128 iMac G4s. D32 textures degrade ungracefully
		// there; flagged as a known gap rather than solved here (rarely-used feature -
		// see humming-sleeping-stonebraker.md for scope).
		return GL_DEPTH_COMPONENT16;
	}

	void ApplyAnisotropicFiltering(float)
	{
		// Not supported on fixed-function G4-era hardware.
	}

	void UnbindProgramAfterDraw()
	{
		// No program to unbind.
	}

	void SetPointSize(float size)
	{
		glPointSize(size);
	}

	void ReadTexture2DPixels(const Vector2i& size, void* pData)
	{
		glReadPixels(0, 0, size.x, size.y, GL_RGBA, GL_UNSIGNED_BYTE, pData);
	}

	bool SupportsMipmapGeneration()
	{
		return false;
	}

	void BindDefaultReadFramebuffer()
	{
		// No FBO concept at all on this backend - nothing to reset from.
	}
	void GenerateMipmap()
	{
		// Never actually reached (SupportsMipmapGeneration() is false), but still needs a
		// body since glGenerateMipmap isn't declared by this backend's classic gl.h at all.
	}
}
