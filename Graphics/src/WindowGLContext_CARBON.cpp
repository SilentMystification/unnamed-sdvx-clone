#include "stdafx.h"
#include "WindowGLContext.hpp"
#include "Window.hpp"

#include <AGL/agl.h>

namespace Graphics::WindowGLContext
{
	namespace
	{
		// aglSetCurrentContext/aglSwapBuffers run every single frame and, until now, had
		// their result completely unchecked - if AGL were silently failing on either of
		// them, every GL call downstream would still "succeed" from OpenGL's own
		// perspective while never actually reaching a properly-bound, visible drawable.
		// Logging only on state CHANGE (not every frame) keeps this from spamming while
		// still catching a persistent failure immediately, once.
		void CheckAGLError(const char* what)
		{
			static GLenum lastErr = AGL_NO_ERROR;
			GLenum err = aglGetError();
			if(err != AGL_NO_ERROR && err != lastErr)
				Logf("AGL error after %s: %d (%s)", Logger::Severity::Error, what, (int)err, aglErrorString(err));
			lastErr = err;
		}
	}

	void Destroy(void* context)
	{
		AGLContext ctx = (AGLContext)context;
		if(ctx)
		{
			aglSetCurrentContext(nullptr);
			aglSetDrawable(ctx, nullptr);
			aglDestroyContext(ctx);
		}
	}
	void MakeCurrent(Window& window, void* context)
	{
		if(!aglSetCurrentContext((AGLContext)context))
			CheckAGLError("aglSetCurrentContext (MakeCurrent)");
	}
	void ReleaseCurrent()
	{
		aglSetCurrentContext(nullptr);
	}
	void SwapBuffers(Window& window, void* context)
	{
		// aglSwapBuffers returns void (unlike the other AGL calls here) - only
		// aglGetError() can tell us anything about it.
		aglSwapBuffers((AGLContext)context);
		CheckAGLError("aglSwapBuffers");
	}
	void UpdateDrawable(Window& window, void* context)
	{
		// Apple's own AGL docs: call this whenever the drawable's position, size, or
		// visibility changes, or the context keeps rendering into the old region/size -
		// this is exactly the "renders successfully every frame, shows nothing useful"
		// symptom, since none of the GL calls themselves fail or error.
		if(context)
		{
			if(!aglUpdateContext((AGLContext)context))
				CheckAGLError("aglUpdateContext");
		}
	}
}
