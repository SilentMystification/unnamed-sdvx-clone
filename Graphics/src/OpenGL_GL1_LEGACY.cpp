#include "stdafx.h"
#include "OpenGL_Impl.hpp"
#include "Window.hpp"

#include <Carbon/Carbon.h>
#include <AGL/agl.h>

namespace Graphics
{
	bool OpenGL::Init(Window& window, uint32 antialiasing)
	{
		if(m_impl->context)
			return true; // Already initialized

		m_impl->threadId = std::this_thread::get_id();
		m_window = &window;
		WindowRef windowRef = (WindowRef)m_window->Handle();

		// No core-profile concept exists pre-10.7 - this is always the classic
		// fixed-function GL context (matches the SDL2 backend's stencil=2/alpha=8
		// SDL_GL_SetAttribute calls, made here via AGL instead).
		//
		// Deliberately ignoring the antialiasing parameter (AntiAliasing defaults to 1 in
		// GameConfig.cpp, i.e. a 2x multisample request, on every backend): on every other
		// platform this codebase targets, the window system transparently resolves MSAA to
		// the displayed buffer on swap - so ubiquitous a guarantee that there was no reason
		// to suspect it here. ARB_multisample support in Apple's classic AGL layer (not the
		// modern CGL/NSOpenGLContext path) on a ~2003 mobile GPU driver (GeForce FX Go5200)
		// is exactly the kind of thing that can be incomplete - requesting a multisample
		// pixel format that context creation happily grants, but whose buffer never
		// actually gets resolved into what's shown, would look exactly like this port's
		// real symptom on real G4 hardware: every draw call succeeds, nothing ever
		// visibly renders. Never requesting multisampling here removes that whole class of
		// risk outright rather than trying to verify AGL's resolve behavior blind.
		GLint attribs[] = {
			AGL_RGBA, AGL_DOUBLEBUFFER,
			AGL_ALPHA_SIZE, 8,
			AGL_STENCIL_SIZE, 2,
			AGL_NONE
		};
		(void)antialiasing;
		AGLPixelFormat pixelFormat = aglChoosePixelFormat(nullptr, 0, attribs);
		if(!pixelFormat)
		{
			Log("Failed to choose an AGL pixel format", Logger::Severity::Error);
			return false;
		}

		// aglChoosePixelFormat's attribs above are requests, not guarantees - log what was
		// actually granted instead of assuming it matches, since a silently-downgraded
		// format (0 stencil bits, single-buffered, etc.) would explain a lot and has never
		// actually been confirmed either way on real hardware.
		{
			GLint gotDoubleBuffer = 0, gotStencil = 0, gotAlpha = 0, gotColor = 0, gotAccelerated = 0;
			GLint gotSampleBuffers = 0, gotSamples = 0, gotAux = 0, gotOffscreen = 0;
			aglDescribePixelFormat(pixelFormat, AGL_DOUBLEBUFFER, &gotDoubleBuffer);
			aglDescribePixelFormat(pixelFormat, AGL_STENCIL_SIZE, &gotStencil);
			aglDescribePixelFormat(pixelFormat, AGL_ALPHA_SIZE, &gotAlpha);
			aglDescribePixelFormat(pixelFormat, AGL_BUFFER_SIZE, &gotColor);
			aglDescribePixelFormat(pixelFormat, AGL_ACCELERATED, &gotAccelerated);
			// Never requested - querying what the driver granted anyway, since a request
			// omission doesn't guarantee the driver doesn't default to a multisampled/
			// offscreen surface that needs an explicit resolve-to-window step we're not
			// doing. A nonzero gotSampleBuffers here, despite never asking for it, would be
			// a real lead for "renders once, then black with a couple of surviving edge/AA
			// pixels" if that symptom ever recurs.
			aglDescribePixelFormat(pixelFormat, AGL_SAMPLE_BUFFERS_ARB, &gotSampleBuffers);
			aglDescribePixelFormat(pixelFormat, AGL_SAMPLES_ARB, &gotSamples);
			aglDescribePixelFormat(pixelFormat, AGL_AUX_BUFFERS, &gotAux);
			aglDescribePixelFormat(pixelFormat, AGL_OFFSCREEN, &gotOffscreen);
			Logf("AGL pixel format granted: color=%d alpha=%d stencil=%d doublebuffer=%d accelerated=%d sampleBuffers=%d samples=%d auxBuffers=%d offscreen=%d",
				Logger::Severity::Info, gotColor, gotAlpha, gotStencil, gotDoubleBuffer, gotAccelerated, gotSampleBuffers, gotSamples, gotAux, gotOffscreen);
		}

		AGLContext context = aglCreateContext(pixelFormat, nullptr);
		aglDestroyPixelFormat(pixelFormat);
		if(!context)
		{
			Log("Failed to create AGL context", Logger::Severity::Error);
			return false;
		}
		m_impl->context = context;

		// aglSetWindowRef doesn't exist on this SDK - AGLDrawable is a CGrafPtr
		// (QuickDraw graphics port), not a WindowRef; GetWindowPort() bridges the two.
		if(!aglSetDrawable(context, GetWindowPort(windowRef)))
		{
			Log("Failed to bind AGL context to window", Logger::Severity::Error);
			return false;
		}
		if(!aglSetCurrentContext(context))
		{
			Log("Failed to set AGL context to current", Logger::Severity::Error);
			return false;
		}

		// Do NOT rely on a later window move/resize event to call aglUpdateContext for the
		// first time - on a plain launch with no user interaction, the window's size/
		// position can already match what Carbon expects, so no kEventWindowBoundsChanged
		// ever fires, and WindowGLContext::UpdateDrawable() (wired to that event) never
		// runs at all. That exactly matched the real symptom on real hardware: nothing
		// visibly rendered on a fresh launch, but resizing/moving the window "fixed" it
		// from then on, because THAT interaction was the first time aglUpdateContext ever
		// actually got called. Call it here, unconditionally, right after the drawable is
		// bound, so the very first frame doesn't depend on the user doing anything first.
		aglUpdateContext(context);

		Logf("OpenGL Version: %s", Logger::Severity::Info, glGetString(GL_VERSION));
		Logf("OpenGL Renderer: %s", Logger::Severity::Info, glGetString(GL_RENDERER));
		Logf("OpenGL Vendor: %s", Logger::Severity::Info, glGetString(GL_VENDOR));

		InitResourceManagers();

		glDisable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);
		glEnable(GL_BLEND);

		// Deliberately NOT enabled globally: the only code that needs stencil testing at
		// all is nanovg_gl1.h's gl1nvg__fill() (complex/self-intersecting path fills),
		// which already enables and disables it itself around its own use - nothing else
		// in the entire pipeline touches stencil. AGL_STENCIL_SIZE in the pixel format
		// request above is a preference, not a guarantee; if this specific GPU/driver
		// granted a format with zero actual stencil bits, leaving GL_STENCIL_TEST enabled
		// for the whole application lifetime - as it was before this - means every single
		// draw call, everywhere, all the time, runs with stencil testing against a stencil
		// plane that may not exist, which is undefined behavior on old/exotic drivers and
		// can plausibly mean every fragment silently fails to rasterize while every GL call
		// still reports success (no error, no crash) - exactly this port's real "renders
		// successfully every frame, displays nothing" symptom on real G4 hardware.
		glDisable(GL_STENCIL_TEST);

		// Every vertex format in this engine has a position as its first attribute, so
		// GL_VERTEX_ARRAY stays enabled for the life of the context; Mesh_GL1_LEGACY.cpp just
		// re-points it per mesh. GL_TEXTURE_COORD_ARRAY is toggled per-mesh instead (not
		// every format has texcoords - see Mesh_GL1_LEGACY.cpp). There is no GL_COLOR_ARRAY:
		// vertex color is a Material_GL1 glColor4f constant, not a per-vertex attribute -
		// see Material_GL1_LEGACY.cpp.
		glEnableClientState(GL_VERTEX_ARRAY);

		// GL_MODULATE is the only texture-env mode ever used anywhere on this backend
		// (Material_GL1_LEGACY.cpp, nanovg_gl1.h, nuklear_gl1.h) - set once here instead of
		// redundantly on every single textured draw call, which on fixed-function G4-era
		// hardware issuing hundreds of small immediate-mode-style draws per frame (sprites,
		// UI, glyphs) is otherwise a lot of repeated, always-identical driver calls.
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

		return true;
	}
}
