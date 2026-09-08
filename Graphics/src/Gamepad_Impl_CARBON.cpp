#include "stdafx.h"
#include "Gamepad_Impl.hpp"
#include "Window.hpp"

/*
	Carbon/PPC gamepad backend. SDL_Joystick* is real here (see HIDJoystick_CARBON.cpp -
	backed by the classic IOKit HID Device Interface, the API IOHIDManager is itself built
	on and the one available on 10.4).

	Unlike the SDL2 backend, there's no OS event queue pushing button/axis transitions to
	us, so Poll() (called once per frame from WindowImpl_CARBON.cpp's Window_Impl::Update())
	reads current hardware state and diffs it against the last known state itself, calling
	the same HandleInputEvent/HandleAxisEvent hooks a real event queue would call.
*/
namespace Graphics
{
	Gamepad_Impl::~Gamepad_Impl()
	{
		SDL_JoystickClose((SDL_Joystick*)m_joystickHandle);
	}
	bool Gamepad_Impl::Init(Window* window, uint32 deviceIndex)
	{
		Logf("Trying to open joystick %d", Logger::Severity::Info, deviceIndex);

		m_window = window;
		m_deviceIndex = deviceIndex;
		SDL_Joystick* joystick = SDL_JoystickOpen(deviceIndex);
		m_joystickHandle = joystick;
		if(!joystick)
		{
			Logf("Failed to open joystick %d", Logger::Severity::Error, deviceIndex);
			return false;
		}

		for(int32 i = 0; i < SDL_JoystickNumButtons(joystick); i++)
			m_buttonStates.Add(0);
		for(int32 i = 0; i < SDL_JoystickNumAxes(joystick); i++)
			m_axisState.Add(0.0f);

		String deviceName = SDL_JoystickName(joystick);
		Logf("Joystick device \"%s\" opened with %d buttons and %d axes", Logger::Severity::Info,
			deviceName, m_buttonStates.size(), m_axisState.size());

		return true;
	}

	void Gamepad_Impl::Poll()
	{
		SDL_Joystick* joystick = (SDL_Joystick*)m_joystickHandle;
		if(!joystick)
			return;

		for(uint32 i = 0; i < m_buttonStates.size(); i++)
		{
			uint8 newState = SDL_JoystickGetButton(joystick, i) ? 1 : 0;
			if(newState != m_buttonStates[i])
				HandleInputEvent(i, newState, 0);
		}
		for(uint32 i = 0; i < m_axisState.size(); i++)
		{
			int16 newValue = SDL_JoystickGetAxis(joystick, i);
			float normalized = (float)newValue / (float)0x7fff;
			if(normalized != m_axisState[i])
				HandleAxisEvent(i, newValue);
		}
	}

	void Gamepad_Impl::HandleInputEvent(uint32 buttonIndex, uint8 newState, int32 delta)
	{
		m_buttonStates[buttonIndex] = newState;
		if(newState != 0)
			OnButtonPressed.Call(buttonIndex, delta);
		else
			OnButtonReleased.Call(buttonIndex, delta);
	}
	void Gamepad_Impl::HandleAxisEvent(uint32 axisIndex, int16 newValue)
	{
		m_axisState[axisIndex] = (float)newValue / (float)0x7fff;
	}
	void Gamepad_Impl::HandleHatEvent(uint32 hadIndex, uint8 newValue)
	{
		// Not surfaced by the base Gamepad interface - matches the SDL2 backend.
	}

	bool Gamepad_Impl::GetButton(uint8 button) const
	{
		if(button >= m_buttonStates.size())
			return false;
		return m_buttonStates[button] != 0;
	}
	float Gamepad_Impl::GetAxis(uint8 idx) const
	{
		if(idx >= m_axisState.size())
			return 0.0f;
		return m_axisState[idx];
	}
	uint32 Gamepad_Impl::NumButtons() const
	{
		return (uint32)m_buttonStates.size();
	}
	uint32 Gamepad_Impl::NumAxes() const
	{
		return (uint32)m_axisState.size();
	}
}
