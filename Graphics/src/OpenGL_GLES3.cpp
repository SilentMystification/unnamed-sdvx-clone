#include "stdafx.h"
#include "OpenGL_Impl.hpp"
#include "Window.hpp"

namespace Graphics
{
	bool OpenGL::Init(Window& window, uint32 antialiasing)
	{
		if(m_impl->context)
			return true; // Already initialized

		// Store the thread ID that the OpenGL context runs on
		m_impl->threadId = std::this_thread::get_id();

		m_window = &window;
		SDL_Window* sdlWnd = (SDL_Window*)m_window->Handle();

		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

		// Create a context
		m_impl->context = SDL_GL_CreateContext(sdlWnd);
		if(!m_impl->context)
		{
            Logf("Failed to create OpenGL context: %s", Logger::Severity::Error, SDL_GetError());
            return false;
		}

		if (SDL_GL_MakeCurrent(sdlWnd, m_impl->context) < 0)
		{
			Logf("Failed to set OpenGL context to current: %s", Logger::Severity::Error, SDL_GetError());
			return false;
		}

		// embedded doesn't need glew

		Logf("OpenGL Version: %s", Logger::Severity::Info, glGetString(GL_VERSION));
		Logf("OpenGL Shading Language Version: %s", Logger::Severity::Info, glGetString(GL_SHADING_LANGUAGE_VERSION));
		Logf("OpenGL Renderer: %s", Logger::Severity::Info, glGetString(GL_RENDERER));
		Logf("OpenGL Vendor: %s", Logger::Severity::Info, glGetString(GL_VENDOR));

		InitResourceManagers();

		glDisable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);
		glEnable(GL_BLEND);
		glEnable(GL_STENCIL_TEST);
		return true;
	}
}
