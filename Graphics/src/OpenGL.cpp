#include "stdafx.h"
#include "OpenGL_Impl.hpp"
#include "RenderBackendHooks.hpp"
#include "WindowGLContext.hpp"
#include <Graphics/ResourceManagers.hpp>
#ifdef _MSC_VER
#pragma comment(lib, "opengl32.lib")
#endif

#include "Mesh.hpp"
#include "Texture.hpp"
#include "Shader.hpp"
#include "Font.hpp"
#include "Material.hpp"
#include "ParticleSystem.hpp"
#include "Window.hpp"
#include <Shared/Thread.hpp>

namespace Graphics
{
	OpenGL::OpenGL()
	{
		m_impl = new OpenGL_Impl();
	}
	OpenGL::~OpenGL()
	{
		if(m_impl->context)
		{
			// Cleanup resource managers
			ResourceManagers::DestroyResourceManager<ResourceType::Mesh>();
			ResourceManagers::DestroyResourceManager<ResourceType::Texture>();
			ResourceManagers::DestroyResourceManager<ResourceType::Shader>();
			ResourceManagers::DestroyResourceManager<ResourceType::Font>();
			ResourceManagers::DestroyResourceManager<ResourceType::Material>();
			ResourceManagers::DestroyResourceManager<ResourceType::ParticleSystem>();

			RenderBackendHooks::DestroyMainProgramPipeline(m_mainProgramPipeline);

			WindowGLContext::Destroy(m_impl->context);
			m_impl->context = nullptr;
		}
		delete m_impl;
	}
	void OpenGL::InitResourceManagers()
	{
		ResourceManagers::CreateResourceManager<ResourceType::Mesh>();
		ResourceManagers::CreateResourceManager<ResourceType::Texture>();
		ResourceManagers::CreateResourceManager<ResourceType::Shader>();
		ResourceManagers::CreateResourceManager<ResourceType::Font>();
		ResourceManagers::CreateResourceManager<ResourceType::Material>();
		ResourceManagers::CreateResourceManager<ResourceType::ParticleSystem>();
	}

	Recti OpenGL::GetViewport() const
	{
		// GLint is `long` on this target's GL headers (not `int` as on most others,
		// where int32*/GLint* just happen to already match) - read through a
		// same-typed buffer instead of reinterpreting Recti's own int32 fields.
		GLint viewport[4];
		glGetIntegerv(GL_VIEWPORT, viewport);
		Recti vp;
		vp.pos.x = (int32)viewport[0];
		vp.pos.y = (int32)viewport[1];
		vp.size.x = (int32)viewport[2];
		vp.size.y = (int32)viewport[3];
		return vp;
	}
	uint32 OpenGL::GetFramebufferHandle()
	{
		return GL_BACK;
	}

	void OpenGL::SetViewport(Recti vp)
	{
		glViewport(vp.pos.x, vp.pos.y, vp.size.x, vp.size.y);
	}

	//https://www.khronos.org/opengl/wiki/OpenGL_and_multithreading
	void OpenGL::MakeCurrent()
	{
		assert(m_impl->threadId != std::this_thread::get_id());
		WindowGLContext::MakeCurrent(*m_window, m_impl->context);
		m_impl->threadId = std::this_thread::get_id();

	}
	void OpenGL::ReleaseCurrent()
	{
		assert(m_impl->threadId == std::this_thread::get_id());
		WindowGLContext::ReleaseCurrent();
	}

	void OpenGL::SetViewport(Vector2i size)
	{
		glViewport(0, 0, size.x, size.y);
	}
	bool OpenGL::IsOpenGLThread() const
	{
		return m_impl->threadId == std::this_thread::get_id();
	}

	void OpenGL::SwapBuffers()
	{
		glFlush();
		WindowGLContext::SwapBuffers(*m_window, m_impl->context);
	}

	void OpenGL::OnWindowMoved()
	{
		WindowGLContext::UpdateDrawable(*m_window, m_impl->context);
	}

	#ifdef _WIN32
	void APIENTRY GLDebugProc(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam)
	#else
	void GLDebugProc(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam)
	#endif
	{
#define DEBUG_SEVERITY_HIGH                              0x9146
#define DEBUG_SEVERITY_MEDIUM                            0x9147
#define DEBUG_SEVERITY_LOW                               0x9148
#define DEBUG_SEVERITY_NOTIFICATION                      0x826B

		Logger::Severity mySeverity;
		switch(severity)
		{
		case DEBUG_SEVERITY_MEDIUM:
		case DEBUG_SEVERITY_HIGH:
			mySeverity = Logger::Severity::Warning;
			break;
		default:
			mySeverity = Logger::Severity::Info;
			break;
		}
		String msgString = String(message, message + length);
		Logf("GLDebug: %s", mySeverity, msgString);
	}
}
