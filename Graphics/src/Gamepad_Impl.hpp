#pragma once
#include "Gamepad.hpp"

namespace Graphics
{
	class Window;
	class Gamepad_Impl : public Gamepad
	{
	public:
		~Gamepad_Impl();
		bool Init(Graphics::Window* window, uint32 deviceIndex);

		// Handles input events straight from the event loop
		void HandleInputEvent(uint32 buttonIndex, uint8 newState, int32 delta);
		void HandleAxisEvent(uint32 axisIndex, int16 newValue);
		void HandleHatEvent(uint32 hadIndex, uint8 newValue);

		// Backend hook: on SDL2 a no-op (real button/axis state arrives from the OS
		// event queue via WindowImpl_SDL2.cpp calling HandleInputEvent/HandleAxisEvent
		// directly). On Carbon, there's no such queue - Window_Impl::Update() calls this
		// once per frame so the backend can read current hardware state itself and diff
		// it into the same HandleInputEvent/HandleAxisEvent calls.
		void Poll();

		class Window* m_window;
		uint32 m_deviceIndex;
		// Opaque backend device handle - SDL_Joystick* (SDL2) or a real IOKit-backed
		// SDL_Joystick* (Carbon, see HIDJoystick_CARBON.cpp) are both just pointers, so
		// this stays void* rather than pulling either backend's real type in here.
		void* m_joystickHandle = nullptr;

		Vector<float> m_axisState;
		Vector<uint8> m_buttonStates;

		virtual bool GetButton(uint8 button) const override;
		virtual float GetAxis(uint8 idx) const override;
		virtual uint32 NumButtons() const override;
		virtual uint32 NumAxes() const override;
	};
}