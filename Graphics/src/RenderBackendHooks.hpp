#pragma once
#include <Graphics/GL.hpp>

namespace Graphics
{
	// Small per-backend hooks for behavior that differs by a single call/value between
	// render backends, so shared code never needs an #ifdef to pick between them.
	// Each backend (GL3/GLES3/GL1_LEGACY) provides exactly one implementation of these,
	// selected by CMake - see Graphics/CMakeLists.txt.
	namespace RenderBackendHooks
	{
		// GL type to use for a floating point vertex attribute of the given component
		// byte size (e.g. GLES has no GL_DOUBLE vertex attribute support).
		[[nodiscard]]
		uint32 FloatAttribType(size_t componentSize);

		// Tears down the program pipeline object created by OpenGL::Init() (a no-op on
		// backends that don't use separate shader objects/program pipelines).
		void DestroyMainProgramPipeline(uint32 pipeline);

		// Internal format to use for a depth (TextureFormat::D32) texture.
		[[nodiscard]]
		uint32 DepthTextureInternalFormat();

		// Applies anisotropic filtering to the currently bound 2D texture, if the
		// backend supports it (a no-op on backends without the extension).
		void ApplyAnisotropicFiltering(float anisotropic);

		// Called after RenderQueue draws a mesh, for backends that need to explicitly
		// unbind the active program afterward.
		void UnbindProgramAfterDraw();

		// Sets the rasterized point size for point primitives (desktop-only fixed
		// function state; GLES sets this from the vertex shader instead).
		void SetPointSize(float size);

		// Reads the currently bound 2D texture's pixels back into pData (RGBA8,
		// (size.x * size.y) pixels) - the read path differs (glReadPixels vs
		// glGetTexImage) by backend.
		void ReadTexture2DPixels(const Vector2i& size, void* pData);

		// Whether glGenerateMipmap is safe to call (needs ARB/core framebuffer_object,
		// not guaranteed on G4-era GPUs - definitely absent on Rage128).
		[[nodiscard]]
		bool SupportsMipmapGeneration();

		// glGenerateMipmap itself - only called when SupportsMipmapGeneration() is true,
		// but still needs a body on every backend since GL1_LEGACY's classic gl.h doesn't
		// declare the symbol at all (a compile-time concern, not just the runtime check
		// above).
		void GenerateMipmap();

		// Binds the default (0) read framebuffer, if the backend has an FBO concept at
		// all (GL1_LEGACY doesn't - there's never any other framebuffer bound to reset
		// from, so it's a no-op there). Used by ImageRes::Screenshot() before reading
		// back the current backbuffer.
		void BindDefaultReadFramebuffer();
	}
}
