#pragma once
#include <Graphics/GL.hpp>

namespace Graphics
{
	class Window;

	// The GL context lifecycle (make-current/swap/teardown) is a WINDOW-backend concern
	// (SDL_GL_* vs AGL*), independent of which render backend is active - context
	// *creation* stays in each render backend's Init() instead (see OpenGL_GL3.cpp/
	// OpenGL_GLES3.cpp), since the attributes it's created with are render-backend
	// specific and, in this codebase, each render backend only ever pairs with one
	// window backend in practice. Selected by USC_WINDOW_BACKEND - see
	// Graphics/CMakeLists.txt.
	namespace WindowGLContext
	{
		void Destroy(void* context);
		void MakeCurrent(Window& window, void* context);
		void ReleaseCurrent();
		void SwapBuffers(Window& window, void* context);
		// Must be called whenever the window's position, size, or visibility changes -
		// AGL (Carbon) requires this to re-sync the context's drawable to match, or
		// rendering keeps going to the old (now stale) region/size while still succeeding
		// at the API level. No-op on backends that don't need it (SDL2's GL context
		// already re-syncs itself on resize).
		void UpdateDrawable(Window& window, void* context);
	}
}
