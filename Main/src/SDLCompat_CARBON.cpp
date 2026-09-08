/*
	Main/ calls a number of real SDL2 functions directly, bypassing Graphics::Window
	entirely - not just the SDL_Scancode/SDL_Event *types* Window.hpp's public interface
	uses (see Graphics/include/backends/window/carbon-sdl-headers/README.md for those).
	These are general platform services (timing, semaphores, dynamic library loading,
	keyboard layout, clipboard) rather than windowing per se, so they need their own real
	implementations here for the Carbon/10.4 build, where actual SDL2 doesn't exist.

	See /home/andrew/.claude/plans/humming-sleeping-stonebraker.md for the port this is
	part of. UNVERIFIED - no macOS/PPC hardware in the environment this was written in.

	Main/include/nuklear/nuklear_gl1.h (the fixed-function nuklear backend selected in
	Main/stdafx.cpp for USC_GL1_LEGACY, replacing nuklear_sdl_gl3.h which needs GL3/GLSL
	this backend doesn't have) also calls SDL_GetWindowSize/SDL_GetKeyboardState directly -
	those two are provided below alongside the rest.
*/
#include "stdafx.h"

#include <mach/mach_time.h>
#include <semaphore.h>
#include <dlfcn.h>
#include <Carbon/Carbon.h>

/* ---- Error string (mirrors SDL_GetError/SDL_ClearError) ---- */

static thread_local String s_lastError;

extern "C" void SDL_ClearError()
{
	s_lastError.clear();
}
extern "C" const char* SDL_GetError()
{
	return s_lastError.c_str();
}
static void SetLastErrorFromDl()
{
	const char* err = dlerror();
	s_lastError = err ? err : "";
}

/* ---- Timing ---- */

extern "C" Uint32 SDL_GetTicks()
{
	static mach_timebase_info_data_t timebase = { 0, 0 };
	if(timebase.denom == 0)
		mach_timebase_info(&timebase);
	uint64_t nanos = mach_absolute_time() * timebase.numer / timebase.denom;
	return (Uint32)(nanos / 1000000ull);
}

/* ---- Semaphores (Application.cpp's render/input thread handoff) ---- */

struct SDL_semaphore { sem_t sem; };

extern "C" SDL_semaphore* SDL_CreateSemaphore(Uint32 initialValue)
{
	SDL_semaphore* s = new SDL_semaphore();
	if(sem_init(&s->sem, 0, initialValue) != 0)
	{
		delete s;
		return nullptr;
	}
	return s;
}
extern "C" void SDL_DestroySemaphore(SDL_semaphore* sem)
{
	if(!sem)
		return;
	sem_destroy(&sem->sem);
	delete sem;
}
extern "C" int SDL_SemWait(SDL_semaphore* sem)
{
	return sem ? sem_wait(&sem->sem) : -1;
}
extern "C" int SDL_SemPost(SDL_semaphore* sem)
{
	return sem ? sem_post(&sem->sem) : -1;
}

/* ---- Dynamic library loading (LightPlugins - Application.cpp) ---- */

extern "C" void* SDL_LoadObject(const char* sofile)
{
	void* handle = dlopen(sofile, RTLD_NOW);
	if(!handle)
		SetLastErrorFromDl();
	return handle;
}
extern "C" void* SDL_LoadFunction(void* handle, const char* name)
{
	void* fn = dlsym(handle, name);
	if(!fn)
		SetLastErrorFromDl();
	return fn;
}
extern "C" void SDL_UnloadObject(void* handle)
{
	if(handle)
		dlclose(handle);
}

/* ---- Subsystem/event-pump no-ops ----
   Carbon's Event Manager (see WindowImpl_CARBON.cpp's Window_Impl::Update, which uses
   ReceiveNextEvent) has no "pump without dispatching" concept the way SDL's internal
   event queue does, and no subsystem-init gate to check - both are safe no-ops/always-true
   here. */

extern "C" Uint32 SDL_WasInit(Uint32)
{
	return 1;
}
extern "C" void SDL_PumpEvents()
{
}

/* ---- Keyboard: scancode <-> keycode <-> name ----
   SDL's real keycode encoding (SDL_keycode.h, vendored verbatim - see the README next to
   it): printable keys' keycodes are their literal lowercase-ASCII/UTF-8 codepoint; every
   other key is (scancode | SDLK_SCANCODE_MASK). This table covers the printable range
   (US QWERTY, the only layout assumption reasonable without real hardware to test IME/
   layout switching on); everything else uses that formula directly, both documented,
   stable SDL behavior - not guessed. Used for game_config.cfg's legacy keycode->scancode
   migration (GameConfig.cpp) and the settings screen's bound-key display name
   (SettingsScreen.cpp), not for live gameplay input (which reads scancodes directly). */

struct ScancodeKeyName { SDL_Scancode scancode; char printable; const char* name; };
static const ScancodeKeyName s_printableKeys[] = {
	{ SDL_SCANCODE_A, 'a', "A" }, { SDL_SCANCODE_B, 'b', "B" }, { SDL_SCANCODE_C, 'c', "C" },
	{ SDL_SCANCODE_D, 'd', "D" }, { SDL_SCANCODE_E, 'e', "E" }, { SDL_SCANCODE_F, 'f', "F" },
	{ SDL_SCANCODE_G, 'g', "G" }, { SDL_SCANCODE_H, 'h', "H" }, { SDL_SCANCODE_I, 'i', "I" },
	{ SDL_SCANCODE_J, 'j', "J" }, { SDL_SCANCODE_K, 'k', "K" }, { SDL_SCANCODE_L, 'l', "L" },
	{ SDL_SCANCODE_M, 'm', "M" }, { SDL_SCANCODE_N, 'n', "N" }, { SDL_SCANCODE_O, 'o', "O" },
	{ SDL_SCANCODE_P, 'p', "P" }, { SDL_SCANCODE_Q, 'q', "Q" }, { SDL_SCANCODE_R, 'r', "R" },
	{ SDL_SCANCODE_S, 's', "S" }, { SDL_SCANCODE_T, 't', "T" }, { SDL_SCANCODE_U, 'u', "U" },
	{ SDL_SCANCODE_V, 'v', "V" }, { SDL_SCANCODE_W, 'w', "W" }, { SDL_SCANCODE_X, 'x', "X" },
	{ SDL_SCANCODE_Y, 'y', "Y" }, { SDL_SCANCODE_Z, 'z', "Z" },
	{ SDL_SCANCODE_1, '1', "1" }, { SDL_SCANCODE_2, '2', "2" }, { SDL_SCANCODE_3, '3', "3" },
	{ SDL_SCANCODE_4, '4', "4" }, { SDL_SCANCODE_5, '5', "5" }, { SDL_SCANCODE_6, '6', "6" },
	{ SDL_SCANCODE_7, '7', "7" }, { SDL_SCANCODE_8, '8', "8" }, { SDL_SCANCODE_9, '9', "9" },
	{ SDL_SCANCODE_0, '0', "0" },
	{ SDL_SCANCODE_SPACE, ' ', "Space" },
	{ SDL_SCANCODE_MINUS, '-', "-" }, { SDL_SCANCODE_EQUALS, '=', "=" },
	{ SDL_SCANCODE_LEFTBRACKET, '[', "[" }, { SDL_SCANCODE_RIGHTBRACKET, ']', "]" },
	{ SDL_SCANCODE_BACKSLASH, '\\', "\\" }, { SDL_SCANCODE_SEMICOLON, ';', ";" },
	{ SDL_SCANCODE_APOSTROPHE, '\'', "'" }, { SDL_SCANCODE_GRAVE, '`', "`" },
	{ SDL_SCANCODE_COMMA, ',', "," }, { SDL_SCANCODE_PERIOD, '.', "." },
	{ SDL_SCANCODE_SLASH, '/', "/" },
};
struct NamedNonPrintable { SDL_Scancode scancode; const char* name; };
static const NamedNonPrintable s_namedNonPrintable[] = {
	{ SDL_SCANCODE_RETURN, "Return" }, { SDL_SCANCODE_ESCAPE, "Escape" },
	{ SDL_SCANCODE_BACKSPACE, "Backspace" }, { SDL_SCANCODE_TAB, "Tab" },
	{ SDL_SCANCODE_CAPSLOCK, "CapsLock" },
	{ SDL_SCANCODE_F1, "F1" }, { SDL_SCANCODE_F2, "F2" }, { SDL_SCANCODE_F3, "F3" },
	{ SDL_SCANCODE_F4, "F4" }, { SDL_SCANCODE_F5, "F5" }, { SDL_SCANCODE_F6, "F6" },
	{ SDL_SCANCODE_F7, "F7" }, { SDL_SCANCODE_F8, "F8" }, { SDL_SCANCODE_F9, "F9" },
	{ SDL_SCANCODE_F10, "F10" }, { SDL_SCANCODE_F11, "F11" }, { SDL_SCANCODE_F12, "F12" },
	{ SDL_SCANCODE_PRINTSCREEN, "PrintScreen" }, { SDL_SCANCODE_SCROLLLOCK, "ScrollLock" },
	{ SDL_SCANCODE_PAUSE, "Pause" }, { SDL_SCANCODE_INSERT, "Insert" },
	{ SDL_SCANCODE_HOME, "Home" }, { SDL_SCANCODE_PAGEUP, "PageUp" },
	{ SDL_SCANCODE_DELETE, "Delete" }, { SDL_SCANCODE_END, "End" },
	{ SDL_SCANCODE_PAGEDOWN, "PageDown" },
	{ SDL_SCANCODE_RIGHT, "Right" }, { SDL_SCANCODE_LEFT, "Left" },
	{ SDL_SCANCODE_DOWN, "Down" }, { SDL_SCANCODE_UP, "Up" },
	{ SDL_SCANCODE_LSHIFT, "Left Shift" }, { SDL_SCANCODE_RSHIFT, "Right Shift" },
	{ SDL_SCANCODE_LCTRL, "Left Ctrl" }, { SDL_SCANCODE_RCTRL, "Right Ctrl" },
	{ SDL_SCANCODE_LALT, "Left Alt" }, { SDL_SCANCODE_RALT, "Right Alt" },
};

extern "C" SDL_Keycode SDL_GetKeyFromScancode(SDL_Scancode scancode)
{
	for(auto& k : s_printableKeys)
	{
		if(k.scancode == scancode)
			return (SDL_Keycode)(unsigned char)k.printable;
	}
	if(scancode == SDL_SCANCODE_UNKNOWN)
		return SDLK_UNKNOWN;
	return SDL_SCANCODE_TO_KEYCODE(scancode);
}
extern "C" SDL_Scancode SDL_GetScancodeFromKey(SDL_Keycode key)
{
	if(key & SDLK_SCANCODE_MASK)
		return (SDL_Scancode)(key & ~SDLK_SCANCODE_MASK);
	for(auto& k : s_printableKeys)
	{
		if((SDL_Keycode)(unsigned char)k.printable == key)
			return k.scancode;
	}
	return SDL_SCANCODE_UNKNOWN;
}
extern "C" const char* SDL_GetKeyName(SDL_Keycode key)
{
	SDL_Scancode scancode = SDL_GetScancodeFromKey(key);
	for(auto& k : s_printableKeys)
	{
		if(k.scancode == scancode)
			return k.name;
	}
	for(auto& k : s_namedNonPrintable)
	{
		if(k.scancode == scancode)
			return k.name;
	}
	return "";
}

extern "C" const Uint8* SDL_GetKeyboardState(int* numkeys)
{
	// TODO(ppc-verify): tied to the same pending real Carbon virtual-keycode -> SDL_Scancode
	// wiring flagged in WindowImpl_CARBON.cpp's Update() - key events aren't dispatched
	// there yet, so this is honestly an always-empty state rather than a regression.
	// Revisit together: once real key events populate Window_Impl's m_keyStates, this
	// should read from the same source instead of its own separate always-zero buffer.
	static Uint8 s_state[512] = { 0 };
	if(numkeys)
		*numkeys = 512;
	return s_state;
}

extern "C" void SDL_GetWindowSize(SDL_Window* window, int* w, int* h)
{
	Rect bounds;
	GetWindowBounds((WindowRef)window, kWindowContentRgn, &bounds);
	if(w) *w = bounds.right - bounds.left;
	if(h) *h = bounds.bottom - bounds.top;
}

/* ---- Display mode (Game.cpp: refresh rate for camera shake timing) ---- */

extern "C" int SDL_GetCurrentDisplayMode(int, SDL_DisplayMode* mode)
{
	// TODO(ppc-verify): real refresh rate via CGDisplayCurrentMode/CGDisplayModeRef.
	// 60Hz is a safe, common default for the shake-timing calculation this feeds.
	memset(mode, 0, sizeof(*mode));
	mode->refresh_rate = 60;
	mode->w = 640;
	mode->h = 480;
	return 0;
}

/* ---- Text input / clipboard ----
   Matches Window's own Carbon stance (WindowImpl_CARBON.cpp): Text Services Manager and
   Scrap Manager integration are real, separate follow-up work, not stubbed here beyond
   safe no-ops - see that file's TODO(ppc-verify) comments. */

extern "C" void SDL_StartTextInput() {}
extern "C" void SDL_StopTextInput() {}
extern "C" void SDL_SetTextInputRect(const SDL_Rect*) {}
extern "C" char* SDL_GetClipboardText()
{
	char* empty = (char*)malloc(1);
	empty[0] = '\0';
	return empty;
}
extern "C" int SDL_SetClipboardText(const char*)
{
	return 0;
}

/* Real joystick support (SDL_JoystickOpen/Close/NumAxes/NumButtons/NumHats/GetDeviceGUID/
   NumJoysticks/GetAxis/GetButton/GetHat/Name) lives in Graphics/src/HIDJoystick_CARBON.cpp,
   backed by IOKit's HID Device Interface - not stubbed here. */
