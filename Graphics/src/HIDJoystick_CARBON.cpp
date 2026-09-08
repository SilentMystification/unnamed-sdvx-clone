/*
	Real gamepad/joystick support for the Carbon/PPC backend, via the classic (pre-10.5)
	IOKit HID Device Interface (IOHIDDeviceInterface122) - the API IOHIDManager itself is
	built on, and the one actually available on 10.4 (IOHIDManager arrived in 10.5).
	Pattern follows Apple's own "HID Utilities" DTS sample code from that era.

	This provides real bodies for the handful of SDL_Joystick* functions Main/ calls
	directly (Input.cpp, SettingsScreen.cpp) - previously stubbed out in SDLCompat_CARBON.cpp
	as "no gamepad support yet". Also backs Gamepad_Impl_CARBON.cpp's polling.

	UNVERIFIED: written against documented, stable IOKit HID APIs, but there is no
	macOS/PPC hardware in the environment this was written in to test against real
	controllers. Axis ordering and hat-switch centering in particular are flagged
	TODO(ppc-verify) below since they can vary per device/driver.
*/
#include "stdafx.h"
#include "SDL2/SDL_joystick.h"

#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/hid/IOHIDLib.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <IOKit/hid/IOHIDUsageTables.h>
#include <CoreFoundation/CoreFoundation.h>

namespace
{
	struct HIDElementInfo
	{
		IOHIDElementCookie cookie = 0;
		int32 min = 0;
		int32 max = 0;
	};

	// One physically present joystick/gamepad/multi-axis-controller HID device, found at
	// enumeration time. The plugin/device interface itself is only opened on demand
	// (SDL_JoystickOpen), mirroring SDL's own open/close semantics.
	struct HIDDeviceInfo
	{
		io_object_t service = 0;
		String name;
		uint16 vendorId = 0;
		uint16 productId = 0;
		uint16 versionNumber = 0;
	};

	Vector<HIDDeviceInfo> g_devices;
	bool g_enumerated = false;

	int32 ReadIntProperty(io_object_t service, CFStringRef key)
	{
		int32 result = 0;
		CFTypeRef prop = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
		if(prop)
		{
			if(CFGetTypeID(prop) == CFNumberGetTypeID())
				CFNumberGetValue((CFNumberRef)prop, kCFNumberSInt32Type, &result);
			CFRelease(prop);
		}
		return result;
	}
	String ReadStringProperty(io_object_t service, CFStringRef key)
	{
		String result;
		CFTypeRef prop = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
		if(prop)
		{
			if(CFGetTypeID(prop) == CFStringGetTypeID())
			{
				char buf[256];
				if(CFStringGetCString((CFStringRef)prop, buf, sizeof(buf), kCFStringEncodingUTF8))
					result = buf;
			}
			CFRelease(prop);
		}
		return result;
	}

	void EnsureEnumerated()
	{
		if(g_enumerated)
			return;
		g_enumerated = true;

		CFMutableDictionaryRef matchDict = IOServiceMatching(kIOHIDDeviceKey);
		if(!matchDict)
			return;

		io_iterator_t iter = 0;
		if(IOServiceGetMatchingServices(kIOMasterPortDefault, matchDict, &iter) != KERN_SUCCESS)
			return;

		io_object_t service;
		while((service = IOIteratorNext(iter)) != 0)
		{
			int32 usagePage = ReadIntProperty(service, CFSTR(kIOHIDPrimaryUsagePageKey));
			int32 usage = ReadIntProperty(service, CFSTR(kIOHIDPrimaryUsageKey));

			bool isGameController = (usagePage == kHIDPage_GenericDesktop) &&
				(usage == kHIDUsage_GD_Joystick || usage == kHIDUsage_GD_GamePad || usage == kHIDUsage_GD_MultiAxisController);

			if(isGameController)
			{
				HIDDeviceInfo info;
				info.service = service; // kept open for the lifetime of the process
				info.name = ReadStringProperty(service, CFSTR(kIOHIDProductKey));
				if(info.name.empty())
					info.name = "Unknown Gamepad";
				info.vendorId = (uint16)ReadIntProperty(service, CFSTR(kIOHIDVendorIDKey));
				info.productId = (uint16)ReadIntProperty(service, CFSTR(kIOHIDProductIDKey));
				info.versionNumber = (uint16)ReadIntProperty(service, CFSTR(kIOHIDVersionNumberKey));
				g_devices.Add(info);

				Logf("Found gamepad HID device \"%s\" (vendor=0x%04x product=0x%04x)", Logger::Severity::Info,
					info.name, info.vendorId, info.productId);
			}
			else
			{
				IOObjectRelease(service);
			}
		}
		IOObjectRelease(iter);
	}

	// Synthesized GUID: not the same algorithm real SDL2 uses on other platforms, but
	// stable per-device (vendor/product/version are fixed in hardware) and unique enough
	// for Input.cpp/SettingsScreen.cpp's own memcmp-based "is this the configured pad"
	// matching, which is the only thing that reads it.
	SDL_JoystickGUID MakeGUID(const HIDDeviceInfo& dev)
	{
		SDL_JoystickGUID guid;
		memset(guid.data, 0, sizeof(guid.data));
		guid.data[0] = 0x03; // bus type: USB
		guid.data[4] = (Uint8)(dev.vendorId & 0xff);
		guid.data[5] = (Uint8)(dev.vendorId >> 8);
		guid.data[8] = (Uint8)(dev.productId & 0xff);
		guid.data[9] = (Uint8)(dev.productId >> 8);
		guid.data[12] = (Uint8)(dev.versionNumber & 0xff);
		guid.data[13] = (Uint8)(dev.versionNumber >> 8);
		return guid;
	}
}

// Real definition of the opaque SDL_Joystick handle - only Carbon-target translation
// units ever see inside this, everywhere else (including Gamepad_Impl_CARBON.cpp) treats
// it as opaque, exactly like real SDL.
struct _SDL_Joystick
{
	IOHIDDeviceInterface122** hidDevice = nullptr;
	String name;

	// TODO(ppc-verify): element ordering comes from IOKit's copyMatchingElements() device
	// enumeration order, which is generally but not guaranteed to be ascending by usage
	// (X, Y, Z, Rx, Ry, Rz, Slider for axes) - matches most HID gamepads' own descriptor
	// order but hasn't been checked against a real controller.
	Vector<HIDElementInfo> buttons;
	Vector<HIDElementInfo> axes;
	Vector<HIDElementInfo> hats;
};

extern "C" int SDL_NumJoysticks()
{
	EnsureEnumerated();
	return (int)g_devices.size();
}

extern "C" SDL_JoystickGUID SDL_JoystickGetDeviceGUID(int deviceIndex)
{
	EnsureEnumerated();
	if(deviceIndex < 0 || deviceIndex >= (int)g_devices.size())
	{
		SDL_JoystickGUID guid;
		memset(guid.data, 0, sizeof(guid.data));
		return guid;
	}
	return MakeGUID(g_devices[deviceIndex]);
}

extern "C" SDL_Joystick* SDL_JoystickOpen(int deviceIndex)
{
	EnsureEnumerated();
	if(deviceIndex < 0 || deviceIndex >= (int)g_devices.size())
		return nullptr;

	HIDDeviceInfo& dev = g_devices[deviceIndex];

	IOCFPlugInInterface** plugin = nullptr;
	SInt32 score = 0;
	if(IOCreatePlugInInterfaceForService(dev.service, kIOHIDDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &plugin, &score) != kIOReturnSuccess || !plugin)
		return nullptr;

	IOHIDDeviceInterface122** hidDevice = nullptr;
	HRESULT hr = (*plugin)->QueryInterface(plugin, CFUUIDGetUUIDBytes(kIOHIDDeviceInterfaceID122), (LPVOID*)&hidDevice);
	(*plugin)->Release(plugin);
	if(hr != S_OK || !hidDevice)
		return nullptr;

	if((*hidDevice)->open(hidDevice, 0) != kIOReturnSuccess)
	{
		(*hidDevice)->Release(hidDevice);
		return nullptr;
	}

	_SDL_Joystick* joystick = new _SDL_Joystick();
	joystick->hidDevice = hidDevice;
	joystick->name = dev.name;

	CFArrayRef elements = nullptr;
	if((*hidDevice)->copyMatchingElements(hidDevice, nullptr, &elements) == kIOReturnSuccess && elements)
	{
		CFIndex count = CFArrayGetCount(elements);
		for(CFIndex i = 0; i < count; i++)
		{
			CFDictionaryRef element = (CFDictionaryRef)CFArrayGetValueAtIndex(elements, i);

			auto GetIntField = [&](CFStringRef key) -> int32
			{
				CFNumberRef num = (CFNumberRef)CFDictionaryGetValue(element, key);
				int32 value = 0;
				if(num)
					CFNumberGetValue(num, kCFNumberSInt32Type, &value);
				return value;
			};

			HIDElementInfo info;
			info.cookie = (IOHIDElementCookie)GetIntField(CFSTR(kIOHIDElementCookieKey));
			info.min = GetIntField(CFSTR(kIOHIDElementMinKey));
			info.max = GetIntField(CFSTR(kIOHIDElementMaxKey));
			int32 type = GetIntField(CFSTR(kIOHIDElementTypeKey));
			int32 usagePage = GetIntField(CFSTR(kIOHIDElementUsagePageKey));
			int32 usage = GetIntField(CFSTR(kIOHIDElementUsageKey));

			if(usagePage == kHIDPage_Button && type == kIOHIDElementTypeInput_Button)
			{
				joystick->buttons.Add(info);
			}
			else if(usagePage == kHIDPage_GenericDesktop && usage == kHIDUsage_GD_Hatswitch)
			{
				joystick->hats.Add(info);
			}
			else if(usagePage == kHIDPage_GenericDesktop &&
				(usage == kHIDUsage_GD_X || usage == kHIDUsage_GD_Y || usage == kHIDUsage_GD_Z ||
				 usage == kHIDUsage_GD_Rx || usage == kHIDUsage_GD_Ry || usage == kHIDUsage_GD_Rz ||
				 usage == kHIDUsage_GD_Slider || usage == kHIDUsage_GD_Dial || usage == kHIDUsage_GD_Wheel))
			{
				joystick->axes.Add(info);
			}
		}
		CFRelease(elements);
	}

	return joystick;
}

extern "C" void SDL_JoystickClose(SDL_Joystick* joystick)
{
	if(!joystick)
		return;
	if(joystick->hidDevice)
	{
		(*joystick->hidDevice)->close(joystick->hidDevice);
		(*joystick->hidDevice)->Release(joystick->hidDevice);
	}
	delete joystick;
}

extern "C" const char* SDL_JoystickName(SDL_Joystick* joystick)
{
	static String empty;
	return joystick ? joystick->name.c_str() : empty.c_str();
}

extern "C" int SDL_JoystickNumAxes(SDL_Joystick* joystick)
{
	return joystick ? (int)joystick->axes.size() : 0;
}
extern "C" int SDL_JoystickNumButtons(SDL_Joystick* joystick)
{
	return joystick ? (int)joystick->buttons.size() : 0;
}
extern "C" int SDL_JoystickNumHats(SDL_Joystick* joystick)
{
	return joystick ? (int)joystick->hats.size() : 0;
}

extern "C" Sint16 SDL_JoystickGetAxis(SDL_Joystick* joystick, int axis)
{
	if(!joystick || axis < 0 || axis >= (int)joystick->axes.size())
		return 0;
	const HIDElementInfo& el = joystick->axes[axis];
	IOHIDEventStruct value;
	if((*joystick->hidDevice)->getElementValue(joystick->hidDevice, el.cookie, &value) != kIOReturnSuccess)
		return 0;
	int32 range = el.max - el.min;
	if(range <= 0)
		return 0;
	double normalized = ((double)(value.value - el.min) / (double)range) * 2.0 - 1.0;
	normalized = Math::Clamp(normalized, -1.0, 1.0);
	return (Sint16)(normalized * 32767.0);
}
extern "C" Uint8 SDL_JoystickGetButton(SDL_Joystick* joystick, int button)
{
	if(!joystick || button < 0 || button >= (int)joystick->buttons.size())
		return 0;
	const HIDElementInfo& el = joystick->buttons[button];
	IOHIDEventStruct value;
	if((*joystick->hidDevice)->getElementValue(joystick->hidDevice, el.cookie, &value) != kIOReturnSuccess)
		return 0;
	return value.value != 0 ? 1 : 0;
}
extern "C" Uint8 SDL_JoystickGetHat(SDL_Joystick* joystick, int hat)
{
	if(!joystick || hat < 0 || hat >= (int)joystick->hats.size())
		return SDL_HAT_CENTERED;
	const HIDElementInfo& el = joystick->hats[hat];
	IOHIDEventStruct value;
	if((*joystick->hidDevice)->getElementValue(joystick->hidDevice, el.cookie, &value) != kIOReturnSuccess)
		return SDL_HAT_CENTERED;

	// TODO(ppc-verify): standard 8-way HID hat encoding (0=up .. 7=up-left, clockwise),
	// with "one past max" as centered/null - matches most devices but not checked against
	// real hardware.
	static const Uint8 hatTable[8] = {
		SDL_HAT_UP, SDL_HAT_RIGHTUP, SDL_HAT_RIGHT, SDL_HAT_RIGHTDOWN,
		SDL_HAT_DOWN, SDL_HAT_LEFTDOWN, SDL_HAT_LEFT, SDL_HAT_LEFTUP
	};
	int32 raw = value.value - el.min;
	if(raw >= 0 && raw < 8)
		return hatTable[raw];
	return SDL_HAT_CENTERED;
}
