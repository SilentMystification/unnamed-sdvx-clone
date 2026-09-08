#include "stdafx.h"
#include "WindowGLContext.hpp"
#include "Window.hpp"

namespace Graphics::WindowGLContext
{
	void Destroy(void* context)
	{
		SDL_GL_DeleteContext(context);
	}
	void MakeCurrent(Window& window, void* context)
	{
		SDL_GL_MakeCurrent((SDL_Window*)window.Handle(), context);
	}
	void ReleaseCurrent()
	{
		SDL_GL_MakeCurrent(NULL, NULL);
	}
	void SwapBuffers(Window& window, void* context)
	{
		SDL_Window* sdlWnd = (SDL_Window*)window.Handle();
		SDL_GL_SwapWindow(sdlWnd);
	}
	void UpdateDrawable(Window& window, void* context)
	{
		// SDL2's GL context re-syncs itself on resize/move - nothing to do here.
	}
}
