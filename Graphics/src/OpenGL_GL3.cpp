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

		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);

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

		//windows always needs glew
#ifdef _WIN32
		glewExperimental = true;
		glewInit();
#elif !defined(__APPLE__)
		// macOS doesn't need glew
		glewExperimental = true;
		glewInit();
#endif

		//#define LIST_OGL_EXTENSIONS
#ifdef LIST_OGL_EXTENSIONS
		Logf("Listing OpenGL Extensions:", Logger::Info);
		GLint n, i;
		glGetIntegerv(GL_NUM_EXTENSIONS, &n);
		for(i = 0; i < n; i++) {
			Logf("%s", Logger::Info, glGetStringi(GL_EXTENSIONS, i));
		}
#endif

#ifdef _DEBUG
		// Setup GL debug messages to go to the console
		if(glDebugMessageCallback && glDebugMessageControl)
		{
			Log("OpenGL Logging on.", Logger::Severity::Info);
			glDebugMessageCallback(GLDebugProc, 0);
			glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, 0, GL_TRUE);
			glDebugMessageControl(GL_DONT_CARE, GL_DEBUG_TYPE_OTHER, GL_DONT_CARE, 0, 0, GL_FALSE);
		}
#endif

		Logf("OpenGL Version: %s", Logger::Severity::Info, glGetString(GL_VERSION));
		Logf("OpenGL Shading Language Version: %s", Logger::Severity::Info, glGetString(GL_SHADING_LANGUAGE_VERSION));
		Logf("OpenGL Renderer: %s", Logger::Severity::Info, glGetString(GL_RENDERER));
		Logf("OpenGL Vendor: %s", Logger::Severity::Info, glGetString(GL_VENDOR));

		InitResourceManagers();

		// Create pipeline for the program
		glGenProgramPipelines(1, &m_mainProgramPipeline);
		glBindProgramPipeline(m_mainProgramPipeline);
		glEnable(GL_TEXTURE_2D);
		glEnable(GL_MULTISAMPLE);

		glDisable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);
		glEnable(GL_BLEND);
		glEnable(GL_STENCIL_TEST);
		return true;
	}
}
