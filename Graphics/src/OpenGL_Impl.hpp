#pragma once
#include "OpenGL.hpp"
#include <thread>

namespace Graphics
{
	class OpenGL_Impl
	{
	public:
		// Opaque GL context handle - SDL_GLContext (SDL2 backend) or AGLContext (Carbon
		// backend) are themselves both just typedef'd opaque pointers, so this stays a
		// plain void* rather than pulling either backend's real type into shared code.
		// See WindowGLContext.hpp for the operations that act on it.
		void* context = nullptr;
		std::thread::id threadId;
	};
}
