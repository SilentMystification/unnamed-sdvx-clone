/*
	Carbon/AGL window backend for macOS 10.4 Tiger / PowerPC, where SDL2 does not run
	(SDL2 requires 10.6+/Cocoa). Mirrors WindowImpl_SDL2.cpp's Window_Impl shape so
	Window.hpp's public interface (and the ~30 files under Main/ that consume it via
	SDL_Scancode/SDL_Event) needs no changes - see Graphics/include/backends/window/
	carbon-sdl-headers/README.md for why those types are still available here without
	linking real SDL2.

	UNVERIFIED: written against documented, stable Carbon Window Manager / AGL / Carbon
	Event Manager APIs, but there is no macOS/PPC hardware or emulator in the environment
	this was written in to compile- or run-test it against. Parts most likely to need
	correction on real hardware are tagged TODO(ppc-verify).

	Known reduced scope (see humming-sleeping-stonebraker.md):
	- Gamepad support: real, via IOKit HID - see HIDJoystick_CARBON.cpp/Gamepad_Impl_CARBON.cpp.
	- IME text composition (StartTextInput/StopTextInput/GetTextComposition): stubbed,
	  Carbon's Text Services Manager integration is substantially more involved than
	  SDL2's and needs real hardware to get right.
*/
#include "stdafx.h"
#include "Window.hpp"
#include "Image.hpp"
#include "Gamepad_Impl.hpp"
#include "SDL2/SDL_joystick.h"
#include <Shared/Profiling.hpp>

#include <Carbon/Carbon.h>
#include <AGL/agl.h>
#include <ApplicationServices/ApplicationServices.h>

namespace Graphics
{
	namespace
	{
		// Standard Mac ADB/USB virtual keycode table (index = kEventParamKeyCode) -> SDL
		// scancode. Unrelated numbering scheme from SDL's own (USB HID-based) scancodes -
		// this is the same physical-key table SDL2's own Cocoa backend and Qt/wxWidgets use,
		// since it reflects real, unchanged-since-ADB hardware scancodes rather than
		// anything OS-version-specific. Slots with no well-established mapping (rare/exotic
		// keys) are left SDL_SCANCODE_UNKNOWN rather than guessed.
		const SDL_Scancode g_carbonKeyCodeTable[128] = {
			/* 0x00 */ SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_D, SDL_SCANCODE_F,
			/* 0x04 */ SDL_SCANCODE_H, SDL_SCANCODE_G, SDL_SCANCODE_Z, SDL_SCANCODE_X,
			/* 0x08 */ SDL_SCANCODE_C, SDL_SCANCODE_V, SDL_SCANCODE_NONUSBACKSLASH, SDL_SCANCODE_B,
			/* 0x0C */ SDL_SCANCODE_Q, SDL_SCANCODE_W, SDL_SCANCODE_E, SDL_SCANCODE_R,
			/* 0x10 */ SDL_SCANCODE_Y, SDL_SCANCODE_T, SDL_SCANCODE_1, SDL_SCANCODE_2,
			/* 0x14 */ SDL_SCANCODE_3, SDL_SCANCODE_4, SDL_SCANCODE_6, SDL_SCANCODE_5,
			/* 0x18 */ SDL_SCANCODE_EQUALS, SDL_SCANCODE_9, SDL_SCANCODE_7, SDL_SCANCODE_MINUS,
			/* 0x1C */ SDL_SCANCODE_8, SDL_SCANCODE_0, SDL_SCANCODE_RIGHTBRACKET, SDL_SCANCODE_O,
			/* 0x20 */ SDL_SCANCODE_U, SDL_SCANCODE_LEFTBRACKET, SDL_SCANCODE_I, SDL_SCANCODE_P,
			/* 0x24 */ SDL_SCANCODE_RETURN, SDL_SCANCODE_L, SDL_SCANCODE_J, SDL_SCANCODE_APOSTROPHE,
			/* 0x28 */ SDL_SCANCODE_K, SDL_SCANCODE_SEMICOLON, SDL_SCANCODE_BACKSLASH, SDL_SCANCODE_COMMA,
			/* 0x2C */ SDL_SCANCODE_SLASH, SDL_SCANCODE_N, SDL_SCANCODE_M, SDL_SCANCODE_PERIOD,
			/* 0x30 */ SDL_SCANCODE_TAB, SDL_SCANCODE_SPACE, SDL_SCANCODE_GRAVE, SDL_SCANCODE_BACKSPACE,
			/* 0x34 */ SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_ESCAPE, SDL_SCANCODE_RGUI, SDL_SCANCODE_LGUI,
			/* 0x38 */ SDL_SCANCODE_LSHIFT, SDL_SCANCODE_CAPSLOCK, SDL_SCANCODE_LALT, SDL_SCANCODE_LCTRL,
			/* 0x3C */ SDL_SCANCODE_RSHIFT, SDL_SCANCODE_RALT, SDL_SCANCODE_RCTRL, SDL_SCANCODE_UNKNOWN,
			/* 0x40 */ SDL_SCANCODE_F17, SDL_SCANCODE_KP_PERIOD, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_KP_MULTIPLY,
			/* 0x44 */ SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_KP_PLUS, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_NUMLOCKCLEAR,
			/* 0x48 */ SDL_SCANCODE_VOLUMEUP, SDL_SCANCODE_VOLUMEDOWN, SDL_SCANCODE_MUTE, SDL_SCANCODE_KP_DIVIDE,
			/* 0x4C */ SDL_SCANCODE_KP_ENTER, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_KP_MINUS, SDL_SCANCODE_F18,
			/* 0x50 */ SDL_SCANCODE_F19, SDL_SCANCODE_KP_EQUALS, SDL_SCANCODE_KP_0, SDL_SCANCODE_KP_1,
			/* 0x54 */ SDL_SCANCODE_KP_2, SDL_SCANCODE_KP_3, SDL_SCANCODE_KP_4, SDL_SCANCODE_KP_5,
			/* 0x58 */ SDL_SCANCODE_KP_6, SDL_SCANCODE_KP_7, SDL_SCANCODE_F20, SDL_SCANCODE_KP_8,
			/* 0x5C */ SDL_SCANCODE_KP_9, SDL_SCANCODE_INTERNATIONAL3, SDL_SCANCODE_INTERNATIONAL1, SDL_SCANCODE_KP_COMMA,
			/* 0x60 */ SDL_SCANCODE_F5, SDL_SCANCODE_F6, SDL_SCANCODE_F7, SDL_SCANCODE_F3,
			/* 0x64 */ SDL_SCANCODE_F8, SDL_SCANCODE_F9, SDL_SCANCODE_LANG2, SDL_SCANCODE_F11,
			/* 0x68 */ SDL_SCANCODE_LANG1, SDL_SCANCODE_F13, SDL_SCANCODE_F16, SDL_SCANCODE_F14,
			/* 0x6C */ SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_F10, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_F12,
			/* 0x70 */ SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_F15, SDL_SCANCODE_INSERT, SDL_SCANCODE_HOME,
			/* 0x74 */ SDL_SCANCODE_PAGEUP, SDL_SCANCODE_DELETE, SDL_SCANCODE_F4, SDL_SCANCODE_END,
			/* 0x78 */ SDL_SCANCODE_F2, SDL_SCANCODE_PAGEDOWN, SDL_SCANCODE_F1, SDL_SCANCODE_LEFT,
			/* 0x7C */ SDL_SCANCODE_RIGHT, SDL_SCANCODE_DOWN, SDL_SCANCODE_UP, SDL_SCANCODE_UNKNOWN,
		};

		SDL_Scancode ScancodeFromCarbonKeyCode(UInt32 keyCode)
		{
			if(keyCode >= 128)
				return SDL_SCANCODE_UNKNOWN;
			return g_carbonKeyCodeTable[keyCode];
		}

		ModifierKeys ModifierKeysFromCarbonMods(UInt32 mods)
		{
			ModifierKeys result = ModifierKeys::None;
			if(mods & shiftKey)
				(uint8&)result |= (uint8)ModifierKeys::Shift;
			if(mods & optionKey)
				(uint8&)result |= (uint8)ModifierKeys::Alt;
			if(mods & controlKey)
				(uint8&)result |= (uint8)ModifierKeys::Ctrl;
			return result;
		}

		// Carbon apps get no menu bar at all unless one is set up explicitly (no nib here) -
		// without this, Cmd+Q has no "Quit" menu item to be the key equivalent of and does
		// nothing at all, leaving no way to close the app short of a force-quit even though
		// the app itself is still alive and pumping events fine. Single-window app, so a
		// plain global for the command handler to reach m_closed through is fine - there's
		// nothing per-window to dispatch to. HandleMenuCommand/SetupQuitMenu are defined
		// after Window_Impl below (need the complete type); only the pointer lives here.
		class Window_Impl* g_windowForQuit = nullptr;
	}

	class Window_Impl
	{
	public:
		Window &outer;

		WindowRef m_window = nullptr;

		Map<SDL_Scancode, uint8> m_keyStates;
		ModifierKeys m_modKeys = ModifierKeys::None;

		TextComposition m_textComposition;

		bool m_closed = false;
		bool m_fullscreen = false;
		Vector2i m_clntSize;
		WString m_caption;
		uint32 m_lastEventTick = 0;

		// Mouse - Carbon reports absolute (global screen) coordinates per event, but
		// OnMouseMotion (like SDL2's) expects relative deltas, so the last position is
		// tracked here to diff against.
		Vector2i m_lastMousePos;
		bool m_haveLastMousePos = false;
		bool m_relativeMouseMode = false;

		// Fullscreen/resolution state (see SetFullscreen/SetWindowed below). 10.4 predates
		// CGDisplayModeRef (10.6+), so mode switches use the classic CGDisplayBestMode/
		// SwitchToMode API and the pre-switch mode is kept here to restore on return to
		// windowed mode.
		bool m_displayCaptured = false;
		CFDictionaryRef m_originalDisplayMode = nullptr;
		Vector2i m_windowedPos;
		Vector2i m_windowedSize;

		// Gamepad input - keyed by device index (no OS event queue to key by instance
		// ID off of, unlike the SDL2 backend; see Gamepad_Impl_CARBON.cpp's Poll()).
		Map<int32, Ref<Gamepad_Impl>> m_gamepads;

		Window_Impl(Window &outer, Vector2i size, uint8 sampleCount) : outer(outer)
		{
			ProfilerScope $("Creating Window");

			m_clntSize = size;
#ifdef _DEBUG
			m_caption = L"USC-Game Debug";
#else
			m_caption = L"USC-Game";
#endif

			// TODO(ppc-verify): confirm Rect coordinate convention (global vs local) on 10.4.
			Rect bounds = { 100, 100, static_cast<short>(100 + size.y), static_cast<short>(100 + size.x) };
			WindowAttributes attrs = kWindowStandardDocumentAttributes | kWindowStandardHandlerAttribute;
			OSStatus err = CreateNewWindow(kDocumentWindowClass, attrs, &bounds, &m_window);
			assert(err == noErr && m_window);

			SetCaption(m_caption);

			// The GL context itself (AGL pixel format/context/sampleCount) is created by
			// OpenGL::Init() (see OpenGL_GL1_LEGACY.cpp), not here - this mirrors the SDL2
			// backend, where Window_Impl only creates the native window and OpenGL::Init()
			// separately calls SDL_GL_CreateContext on it.
			(void)sampleCount;
		}

		~Window_Impl()
		{
			// Never leave the display captured/resolution-switched if the app exits (or
			// crashes) while in exclusive fullscreen - the user would be stuck at the
			// wrong resolution with no window to fix it from.
			ReleaseDisplayIfCaptured();
			if(m_window)
				DisposeWindow(m_window);
		}

		void ReleaseDisplayIfCaptured()
		{
			if(!m_displayCaptured)
				return;
			CGDirectDisplayID display = CGMainDisplayID();
			if(m_originalDisplayMode)
				CGDisplaySwitchToMode(display, m_originalDisplayMode);
			CGDisplayRelease(display);
			m_displayCaptured = false;
			m_originalDisplayMode = nullptr;
		}

		void Show() const
		{
			ShowWindow(m_window);
		}
		void Hide() const
		{
			HideWindow(m_window);
		}
		void SetCaption(const WString &cap)
		{
			m_caption = cap;
			String titleUtf8 = Utility::ConvertToUTF8(m_caption);
			CFStringRef cfTitle = CFStringCreateWithCString(kCFAllocatorDefault, titleUtf8.c_str(), kCFStringEncodingUTF8);
			SetWindowTitleWithCFString(m_window, cfTitle);
			CFRelease(cfTitle);
		}
		void SetCursor(const Ref<class ImageRes>& image, Vector2i hotspot)
		{
			// TODO(ppc-verify): custom cursor images - not implemented (matches the SDL2
			// backend, which only supports this on Windows too).
		}
		void SetWindowStyle(WindowStyle style)
		{
		}

		void SetWindowPos(const Vector2i &pos)
		{
			MoveWindow(m_window, pos.x, pos.y, false);
		}
		Vector2i GetWindowPos() const
		{
			Rect bounds;
			GetWindowBounds(m_window, kWindowContentRgn, &bounds);
			return Vector2i(bounds.left, bounds.top);
		}
		void SetWindowSize(const Vector2i &size)
		{
			SizeWindow(m_window, size.x, size.y, true);
		}
		Vector2i GetWindowSize() const
		{
			Rect bounds;
			GetWindowBounds(m_window, kWindowContentRgn, &bounds);
			return Vector2i(bounds.right - bounds.left, bounds.bottom - bounds.top);
		}
		void SetVSync(int8 setting)
		{
			GLint vsync = setting;
			aglSetInteger(aglGetCurrentContext(), AGL_SWAP_INTERVAL, &vsync);
		}

		// TODO(ppc-verify): monitorId isn't translated to a real CGDirectDisplayID for
		// multi-monitor setups (matches GetDisplayIndex()'s "single display for now" -
		// always CGMainDisplayID()); fine for the iMac G4's single built-in display.
		void SetWindowed(const Vector2i& pos, const Vector2i& size)
		{
			ReleaseDisplayIfCaptured();
			// Restore standard window chrome in case coming from windowed-fullscreen.
			ChangeWindowAttributes(m_window, kWindowStandardDocumentAttributes, kWindowNoTitleBarAttribute);
			SetWindowSize(size);
			SetWindowPos(pos);
			m_fullscreen = false;
		}
		void SetWindowedFullscreen(int32 monitorId)
		{
			// Borderless-fullscreen: no display capture or resolution switch, just remove
			// the title bar and size/position the window to cover the whole display.
			ReleaseDisplayIfCaptured();
			CGDirectDisplayID display = CGMainDisplayID();
			CGRect bounds = CGDisplayBounds(display);
			ChangeWindowAttributes(m_window, kWindowNoTitleBarAttribute, kWindowStandardDocumentAttributes);
			SetWindowPos(Vector2i((int32)bounds.origin.x, (int32)bounds.origin.y));
			SetWindowSize(Vector2i((int32)bounds.size.width, (int32)bounds.size.height));
			m_fullscreen = true;
		}
		void SetFullscreen(int32 monitorId, const Vector2i& res)
		{
			// Exclusive fullscreen: capture the display and switch its actual resolution.
			// 10.4 predates CGDisplayModeRef (10.6+) - CGDisplayBestModeForParameters/
			// CGDisplaySwitchToMode is the API available here, working with the same
			// CFDictionaryRef "mode" objects CGDisplayCurrentMode returns (not owned by the
			// caller - never CFRelease'd, matches Apple's own Carbon-era sample code).
			CGDirectDisplayID display = CGMainDisplayID();
			if(!m_displayCaptured)
			{
				if(CGDisplayCapture(display) == kCGErrorSuccess)
				{
					m_originalDisplayMode = CGDisplayCurrentMode(display);
					m_displayCaptured = true;
				}
				else
				{
					Log("Failed to capture display for fullscreen", Logger::Severity::Warning);
				}
			}
			if(m_displayCaptured)
			{
				boolean_t exact = false;
				CFDictionaryRef mode = CGDisplayBestModeForParameters(display, 32, res.x, res.y, &exact);
				if(mode)
					CGDisplaySwitchToMode(display, mode);
				else
					Logf("No matching display mode for %dx%d", Logger::Severity::Warning, res.x, res.y);
			}
			ChangeWindowAttributes(m_window, kWindowNoTitleBarAttribute, kWindowStandardDocumentAttributes);
			SetWindowPos(Vector2i(0, 0));
			SetWindowSize(res);
			m_fullscreen = true;
		}
		void SetPosAndShape(const Window::PosAndShape& posAndShape, bool ensureInBound)
		{
			switch (posAndShape.mode)
			{
			case Window::PosAndShape::Mode::Windowed:
				SetWindowed(posAndShape.windowPos, posAndShape.windowSize);
				break;
			case Window::PosAndShape::Mode::WindowedFullscreen:
				SetWindowedFullscreen(posAndShape.monitorId);
				break;
			case Window::PosAndShape::Mode::Fullscreen:
				SetFullscreen(posAndShape.monitorId, posAndShape.fullscreenSize);
				break;
			}
		}
		inline bool IsFullscreen() const { return m_fullscreen; }

		void ShowMessageBox(const String& title, const String& message, int severity)
		{
			// TODO(ppc-verify): use StandardAlert (Carbon) - AlertStdCFStringAlertParamRec.
			Logf("[MessageBox] %s: %s", Logger::Severity::Info, title, message);
		}
		bool ShowYesNoMessage(const String& title, const String& message)
		{
			// TODO(ppc-verify): use StandardAlert with two buttons.
			Logf("[YesNo] %s: %s", Logger::Severity::Info, title, message);
			return false;
		}

		// kEventParamMouseLocation is global screen coordinates; nuklear (via synthesized
		// SDL_Event motion/button coords) needs window-local, same conversion Window::GetMousePos
		// below already does via GetGlobalMouse+GetWindowBounds.
		static Vector2i GlobalToLocal(WindowRef window, Vector2i globalPos)
		{
			Rect bounds;
			GetWindowBounds(window, kWindowContentRgn, &bounds);
			return Vector2i(globalPos.x - bounds.left, globalPos.y - bounds.top);
		}

		// Carbon Event Manager pump. Translates native events into the same
		// SDL_Scancode/SDL_Event-shaped calls WindowImpl_SDL2.cpp makes, so every
		// downstream consumer of Window's delegates is unaffected by the backend.
		bool Update()
		{
			EventRef event;
			while(ReceiveNextEvent(0, nullptr, kEventDurationNoWait, true, &event) == noErr)
			{
				UInt32 eventClass = GetEventClass(event);
				UInt32 eventKind = GetEventKind(event);

				if(eventClass == kEventClassKeyboard)
				{
					if(eventKind == kEventRawKeyDown || eventKind == kEventRawKeyUp || eventKind == kEventRawKeyRepeat)
					{
						UInt32 keyCode = 0;
						GetEventParameter(event, kEventParamKeyCode, typeUInt32, nullptr, sizeof(keyCode), nullptr, &keyCode);
						UInt32 mods = 0;
						GetEventParameter(event, kEventParamKeyModifiers, typeUInt32, nullptr, sizeof(mods), nullptr, &mods);
						m_modKeys = ModifierKeysFromCarbonMods(mods);

						SDL_Scancode code = ScancodeFromCarbonKeyCode(keyCode);
						if(code != SDL_SCANCODE_UNKNOWN)
						{
							int32 delta = static_cast<int32>(TickCount() * (1000.0 / 60.0)) - static_cast<int32>(m_lastEventTick);
							uint8 newState = (eventKind == kEventRawKeyUp) ? 0 : 1;
							uint8& currentState = m_keyStates[code];
							if(currentState != newState)
							{
								currentState = newState;
								if(newState == 1)
									outer.OnKeyPressed.Call(code, delta);
								else
									outer.OnKeyReleased.Call(code, delta);
							}
							if(currentState == 1)
								outer.OnKeyRepeat.Call(code);
						}
					}
				}
				else if(eventClass == kEventClassMouse)
				{
					if(eventKind == kEventMouseDown || eventKind == kEventMouseUp)
					{
						if(eventKind == kEventMouseDown)
						{
							// Confirmed on real hardware: clicking the window's close box never
							// generated a kEventWindowClose through this hand-rolled
							// ReceiveNextEvent/SendEventToEventTarget loop - only the Cmd+Q menu
							// command path (a totally separate mechanism, see HandleMenuCommand
							// below) actually worked. Rather than trust that forwarding this raw
							// mouse-down to the window's standard handler correctly synthesizes
							// the close event (it evidently doesn't, for reasons unclear without
							// deeper Carbon internals access this environment doesn't have),
							// hit-test and track the close box directly with the classic,
							// always-available Window Manager API and treat that as the close
							// request ourselves - bypasses the synthesis step entirely.
							Point qdPt;
							if(GetEventParameter(event, kEventParamMouseLocation, typeQDPoint, nullptr, sizeof(qdPt), nullptr, &qdPt) == noErr)
							{
								WindowRef whichWindow = nullptr;
								if(FindWindow(qdPt, &whichWindow) == inGoAway && whichWindow == m_window)
								{
									if(TrackGoAway(m_window, qdPt))
									{
										Log("WindowImpl_CARBON: close box tracked and released, setting m_closed", Logger::Severity::Warning);
										m_closed = true;
										HideWindow(m_window);
									}
									ReleaseEvent(event);
									continue;
								}
							}
						}

						Point buttonQdPt;
						bool haveButtonQdPt = GetEventParameter(event, kEventParamMouseLocation, typeQDPoint, nullptr, sizeof(buttonQdPt), nullptr, &buttonQdPt) == noErr;

						EventMouseButton button = 0;
						GetEventParameter(event, kEventParamMouseButton, typeMouseButton, nullptr, sizeof(button), nullptr, &button);

						MouseButton mb;
						Uint8 sdlButton = 0;
						bool known = true;
						switch(button)
						{
						case kEventMouseButtonPrimary: mb = MouseButton::Left; sdlButton = SDL_BUTTON_LEFT; break;
						case kEventMouseButtonSecondary: mb = MouseButton::Right; sdlButton = SDL_BUTTON_RIGHT; break;
						case kEventMouseButtonTertiary: mb = MouseButton::Middle; sdlButton = SDL_BUTTON_MIDDLE; break;
						default: known = false; break;
						}
						if(known)
						{
							if(eventKind == kEventMouseDown)
								outer.OnMousePressed.Call(mb);
							else
								outer.OnMouseReleased.Call(mb);

							// nuklear (settings/config/calibration screens) only receives input
							// through outer.OnAnyEvent - see GuiUtils.cpp's
							// BasicNuklearGui::UpdateNuklearInput. That was never fed on this
							// backend (only WindowImpl_SDL2.cpp called it), so nuklear never saw
							// clicks, hover, drag, or scroll - confirmed on real hardware as
							// "cannot scroll in the settings menus on any tab". Synthesize the
							// SDL_Event shape nk_sdl_handle_event expects (nuklear_gl1.h) so
							// mouse input reaches it same as the SDL2 backend.
							if(haveButtonQdPt)
							{
								Vector2i localPos = GlobalToLocal(m_window, Vector2i((int32)buttonQdPt.h, (int32)buttonQdPt.v));
								SDL_Event sdlEvt = {};
								sdlEvt.type = (eventKind == kEventMouseDown) ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
								sdlEvt.button.button = sdlButton;
								sdlEvt.button.x = localPos.x;
								sdlEvt.button.y = localPos.y;
								sdlEvt.button.clicks = 1;
								outer.OnAnyEvent.Call(sdlEvt);
							}
						}
					}
					else if(eventKind == kEventMouseMoved || eventKind == kEventMouseDragged)
					{
						Point qdPt;
						if(GetEventParameter(event, kEventParamMouseLocation, typeQDPoint, nullptr, sizeof(qdPt), nullptr, &qdPt) == noErr)
						{
							Vector2i pos((int32)qdPt.h, (int32)qdPt.v);
							if(m_haveLastMousePos)
							{
								Vector2i delta = pos - m_lastMousePos;
								if(delta.x != 0 || delta.y != 0)
									outer.OnMouseMotion.Call(delta.x, delta.y);
							}
							m_lastMousePos = pos;
							m_haveLastMousePos = true;

							// See kEventMouseDown/Up above - nuklear needs absolute, window-local
							// motion coordinates (nk_input_motion via SDL_MOUSEMOTION), not the
							// relative deltas OnMouseMotion delivers.
							Vector2i localPos = GlobalToLocal(m_window, pos);
							SDL_Event sdlEvt = {};
							sdlEvt.type = SDL_MOUSEMOTION;
							sdlEvt.motion.x = localPos.x;
							sdlEvt.motion.y = localPos.y;
							outer.OnAnyEvent.Call(sdlEvt);
						}
					}
					else if(eventKind == kEventMouseWheelMoved)
					{
						EventMouseWheelAxis axis = 0;
						GetEventParameter(event, kEventParamMouseWheelAxis, typeMouseWheelAxis, nullptr, sizeof(axis), nullptr, &axis);
						if(axis == kEventMouseWheelAxisY)
						{
							SInt32 delta = 0;
							GetEventParameter(event, kEventParamMouseWheelDelta, typeSInt32, nullptr, sizeof(delta), nullptr, &delta);
							// TODO(ppc-verify): sign convention not checked against real
							// hardware - matches SDL2 backend's non-"flipped" case.
							outer.OnMouseScroll.Call(-(int32)delta);

							// See kEventMouseDown/Up above - this is the actual fix for "cannot
							// scroll in settings": nuklear's nk_input_scroll is only ever called
							// from nk_sdl_handle_event's SDL_MOUSEWHEEL branch, which was never
							// reached on this backend.
							SDL_Event sdlEvt = {};
							sdlEvt.type = SDL_MOUSEWHEEL;
							sdlEvt.wheel.x = 0;
							sdlEvt.wheel.y = -(int32)delta;
							sdlEvt.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
							outer.OnAnyEvent.Call(sdlEvt);
						}
					}
				}
				bool skipDefaultHandler = false;
				if(eventClass == kEventClassWindow)
				{
					if(eventKind == kEventWindowClose)
					{
						Log("WindowImpl_CARBON: kEventWindowClose received, setting m_closed", Logger::Severity::Warning);
						// We own the window's lifetime ourselves (Window_Impl::~Window_Impl()
						// calls DisposeWindow() during Application shutdown) - forwarding this
						// to Carbon's own default handler as well would let IT dispose the
						// window too, independently, right now, while we're still using it
						// for several more frames until shutdown actually unwinds. That's a
						// double-dispose (undefined behavior) on the same WindowRef, and a
						// very plausible reason the process could hang or misbehave on close
						// instead of actually exiting - skip the forward for this one event.
						m_closed = true;
						skipDefaultHandler = true;

						// Actual process shutdown (Application::m_Cleanup deleting every
						// tickable, jacket image, cached font, the audio engine, then joining
						// threads) can take a real, possibly multi-second amount of time on
						// this hardware with a large chart library loaded - and none of that
						// gives any visual feedback. Left on screen, the window looks exactly
						// like a hung/unresponsive process even when shutdown is proceeding
						// correctly, which is indistinguishable from an actual hang from the
						// user's side. Hide it the instant the close is requested so there's
						// immediate visual confirmation the click was received, regardless of
						// how long the cleanup behind it takes.
						HideWindow(m_window);
					}
					else if(eventKind == kEventWindowBoundsChanged)
						outer.OnResized.Call(GetWindowSize());
					else if(eventKind == kEventWindowActivated)
						outer.OnFocusChanged.Call(true);
					else if(eventKind == kEventWindowDeactivated)
						outer.OnFocusChanged.Call(false);
				}

				if(!skipDefaultHandler)
					SendEventToEventTarget(event, GetEventDispatcherTarget());
				ReleaseEvent(event);
				m_lastEventTick = static_cast<uint32>(TickCount() * (1000.0 / 60.0));
			}

			// No OS event queue for gamepads (unlike SDL2) - poll current hardware state
			// and let each Gamepad_Impl diff it into button/axis events itself.
			for(auto it : m_gamepads)
				it.second->Poll();

			return !m_closed;
		}
	};

	namespace
	{
		pascal OSStatus HandleMenuCommand(EventHandlerCallRef next, EventRef event, void* userData)
		{
			HICommand cmd;
			if(GetEventParameter(event, kEventParamDirectObject, typeHICommand, nullptr, sizeof(cmd), nullptr, &cmd) != noErr)
				return eventNotHandledErr;
			if(cmd.commandID != kHICommandQuit)
				return eventNotHandledErr;
			if(g_windowForQuit)
			{
				Log("WindowImpl_CARBON: Cmd+Q received, setting m_closed", Logger::Severity::Warning);
				g_windowForQuit->m_closed = true;
				// Same reasoning as kEventWindowClose's handler above: give instant visual
				// feedback that the quit was received, since actual shutdown can take a while.
				HideWindow(g_windowForQuit->m_window);
			}
			return noErr;
		}

		void SetupQuitMenu(Window_Impl* window)
		{
			g_windowForQuit = window;

			MenuRef appMenu = nullptr;
			CreateNewMenu(1, 0, &appMenu);
			SetMenuTitleWithCFString(appMenu, CFSTR("USC"));
			InsertMenu(appMenu, 0);

			MenuItemIndex quitIndex = 0;
			AppendMenuItemTextWithCFString(appMenu, CFSTR("Quit USC"), 0, 0, &quitIndex);
			SetMenuItemCommandID(appMenu, quitIndex, kHICommandQuit);
			SetMenuItemModifiers(appMenu, quitIndex, kMenuNoModifiers);
			SetItemCmd(appMenu, quitIndex, 'Q');
			DrawMenuBar();

			EventTypeSpec spec = { kEventClassCommand, kEventCommandProcess };
			InstallApplicationEventHandler(NewEventHandlerUPP(HandleMenuCommand), 1, &spec, nullptr, nullptr);
		}
	}

	Window::Window(Vector2i size, uint8 samplecount)
	{
		m_impl = new Window_Impl(*this, size, samplecount);
	}
	Window::~Window()
	{
		delete m_impl;
	}
	void Window::Show()
	{
		m_impl->Show();

		// Deliberately not done in the Window constructor: that runs before the AGL
		// context exists (OpenGL::Init() happens later, as its own separate "GL Init" step)
		// and before any event loop has run even once - creating/drawing a menu bar this
		// early was the single change present in every build that showed a "renders
		// successfully every frame per a hang sample, but displays almost nothing" symptom
		// on real G4 hardware. Setting it up here, once the window is actually being shown,
		// is deliberately closer to how a normal Carbon app's main() orders window-visible
		// vs. menu-bar-visible work - only ever runs once (SetupQuitMenu itself has no
		// re-entrancy guard, but Window::Show() in practice is only called once per window).
		static bool menuInstalled = false;
		if(!menuInstalled)
		{
			SetupQuitMenu(m_impl);
			menuInstalled = true;
		}
	}
	void Window::Hide()
	{
		m_impl->Hide();
	}
	bool Window::Update()
	{
		return m_impl->Update();
	}
	void *Window::Handle()
	{
		return m_impl->m_window;
	}
	void Window::SetCaption(const WString &cap)
	{
		m_impl->SetCaption(cap);
	}
	void Window::Close()
	{
		m_impl->m_closed = true;
	}

	Vector2i Window::GetMousePos()
	{
		Point globalPt;
		GetGlobalMouse(&globalPt);
		Rect bounds;
		GetWindowBounds(m_impl->m_window, kWindowContentRgn, &bounds);
		return Vector2i(globalPt.h - bounds.left, globalPt.v - bounds.top);
	}
	void Window::SetCursor(const Ref<class ImageRes>& image, Vector2i hotspot)
	{
		m_impl->SetCursor(image, hotspot);
	}
	void Window::SetCursorVisible(bool visible)
	{
		if(visible)
			ShowCursor();
		else
			HideCursor();
	}

	void Window::SetWindowStyle(WindowStyle style)
	{
		m_impl->SetWindowStyle(style);
	}

	Vector2i Window::GetWindowPos() const
	{
		return m_impl->GetWindowPos();
	}
	Vector2i Window::GetWindowSize() const
	{
		return m_impl->GetWindowSize();
	}
	void Window::SetVSync(int8 setting)
	{
		m_impl->SetVSync(setting);
	}
	void Window::SetPosAndShape(const PosAndShape& posAndShape, bool ensureInBound)
	{
		m_impl->SetPosAndShape(posAndShape, ensureInBound);
	}
	bool Window::IsFullscreen() const
	{
		return m_impl->IsFullscreen();
	}
	int Window::GetDisplayIndex() const
	{
		// TODO(ppc-verify): multi-display support via CGDisplay*. Single display for now.
		return 0;
	}

	bool Window::IsKeyPressed(SDL_Scancode key) const
	{
		return m_impl->m_keyStates[key] > 0;
	}
	Graphics::ModifierKeys Window::GetModifierKeys() const
	{
		return m_impl->m_modKeys;
	}
	bool Window::IsActive() const
	{
		return m_impl->m_window == GetUserFocusWindow();
	}

	void Window::StartTextInput()
	{
		// TODO(ppc-verify): Text Services Manager integration.
	}
	void Window::StopTextInput()
	{
		// TODO(ppc-verify): Text Services Manager integration.
	}
	const Graphics::TextComposition &Window::GetTextComposition() const
	{
		return m_impl->m_textComposition;
	}

	void Window::ShowMessageBox(const String& title, const String& message, int severity)
	{
		m_impl->ShowMessageBox(title, message, severity);
	}
	bool Window::ShowYesNoMessage(const String& title, const String& message)
	{
		return m_impl->ShowYesNoMessage(title, message);
	}

	String Window::GetClipboard() const
	{
		// TODO(ppc-verify): Carbon Scrap Manager (10.4) or Pasteboard Manager.
		return String();
	}

	int32 Window::GetNumGamepads() const
	{
		return SDL_NumJoysticks();
	}
	Vector<String> Window::GetGamepadDeviceNames() const
	{
		Vector<String> ret;
		int32 numJoysticks = GetNumGamepads();
		for(int32 i = 0; i < numJoysticks; i++)
		{
			SDL_Joystick* joystick = SDL_JoystickOpen(i);
			if(!joystick)
				continue;
			ret.Add(SDL_JoystickName(joystick));
			SDL_JoystickClose(joystick);
		}
		return ret;
	}

	Ref<Gamepad> Window::OpenGamepad(int32 deviceIndex)
	{
		Ref<Gamepad_Impl>* openGamepad = m_impl->m_gamepads.Find(deviceIndex);
		if(openGamepad)
			return Utility::CastRef<Gamepad_Impl, Gamepad>(*openGamepad);

		Ref<Gamepad_Impl> newGamepad;
		auto* gamepadImpl = new Gamepad_Impl();
		if(gamepadImpl->Init(this, deviceIndex))
			newGamepad = Ref<Gamepad_Impl>(gamepadImpl);
		else
			delete gamepadImpl;

		if(newGamepad)
			m_impl->m_gamepads.Add(deviceIndex, newGamepad);
		return Utility::CastRef<Gamepad_Impl, Gamepad>(newGamepad);
	}

	void Window::SetMousePos(const Vector2i &pos)
	{
		Rect bounds;
		GetWindowBounds(m_impl->m_window, kWindowContentRgn, &bounds);
		CGPoint globalPos = { (float)(bounds.left + pos.x), (float)(bounds.top + pos.y) };
		CGWarpMouseCursorPosition(globalPos);
	}
	void Window::SetRelativeMouseMode(bool enabled)
	{
		// Disabling association lets the mouse move indefinitely in one direction without
		// hitting the screen edge (relative/"mouse look" mode); re-enabling snaps the
		// cursor back to tracking normally.
		CGAssociateMouseAndMouseCursorPosition(!enabled);
		m_impl->m_relativeMouseMode = enabled;
	}
	bool Window::GetRelativeMouseMode()
	{
		return m_impl->m_relativeMouseMode;
	}
	uint32 Window::GetIdleTimsMs()
	{
		return static_cast<uint32>(TickCount() * (1000.0 / 60.0)) - m_impl->m_lastEventTick;
	}
} // namespace Graphics

namespace Graphics
{
	ImplementBitflagEnum(ModifierKeys);
}
