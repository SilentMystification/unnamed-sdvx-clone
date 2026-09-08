#include "stdafx.h"
#include "Shader_Impl.hpp"
#include <Graphics/ResourceManagers.hpp>
#include "OpenGL.hpp"

namespace Graphics
{
	Shader_Impl::~Shader_Impl()
	{
		// Cleanup OpenGL resource
		if(glIsProgram(m_prog))
		{
			glDeleteProgram(m_prog);
		}

#ifdef _WIN32
		// Close change notification handle
		if(m_changeNotification != INVALID_HANDLE_VALUE)
		{
			CloseHandle(m_changeNotification);
		}
#endif
	}

	void Shader_Impl::SetupChangeHandler()
	{
#ifdef _WIN32
		if(m_changeNotification != INVALID_HANDLE_VALUE)
		{
			CloseHandle(m_changeNotification);
			m_changeNotification = INVALID_HANDLE_VALUE;
		}

		WString rootFolder = Utility::ConvertToWString(Path::RemoveLast(m_sourcePath));
		m_changeNotification = FindFirstChangeNotificationW(*rootFolder, false, FILE_NOTIFY_CHANGE_LAST_WRITE);
#endif
	}

	bool Shader_Impl::UpdateHotReload()
	{
#ifdef _WIN32
		if(m_changeNotification != INVALID_HANDLE_VALUE)
		{
			if(WaitForSingleObject(m_changeNotification, 0) == WAIT_OBJECT_0)
			{
				uint64 newLwt = File::GetLastWriteTime(m_sourcePath);
				if(newLwt != -1 && newLwt > m_lwt)
				{
					uint32 newProgram = 0;
					if(LoadProgram(newProgram))
					{
						// Successfully reloaded
						m_prog = newProgram;
						return true;
					}
				}

				// Watch for new change
				SetupChangeHandler();
			}
		}
#endif
		return false;
	}

	Shader ShaderRes::Create(class OpenGL* gl, ShaderType type, const String& assetPath)
	{
		Shader_Impl* pImpl = new Shader_Impl(gl);
		if(!pImpl->Init(type, assetPath))
		{
			delete pImpl;
			return Shader();
		}
		else
		{
			return GetResourceManager<ResourceType::Shader>().Register(pImpl);
		}
	}
}
