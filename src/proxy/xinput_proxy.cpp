// Exports of xinput1_3.dll, forwarded to the real system DLL.
//
// In splitscreen mode each game instance owns exactly one physical pad. The
// game only ever sees that pad, presented as user index 0; every other index
// reports "not connected". IW4x's gamepad code therefore needs no changes.
#include "common.h"
#include <Xinput.h>

namespace
{
	using GetState_t = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
	using SetState_t = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);
	using GetCaps_t = DWORD(WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES*);
	using Enable_t = void(WINAPI*)(BOOL);
	using GetGuids_t = DWORD(WINAPI*)(DWORD, GUID*, GUID*);
	using GetBattery_t = DWORD(WINAPI*)(DWORD, BYTE, XINPUT_BATTERY_INFORMATION*);
	using GetKeystroke_t = DWORD(WINAPI*)(DWORD, DWORD, PXINPUT_KEYSTROKE);
	using WaitGuide_t = DWORD(WINAPI*)(DWORD, DWORD, LPVOID);
	using CancelGuide_t = DWORD(WINAPI*)(DWORD);
	using PowerOff_t = DWORD(WINAPI*)(DWORD);

	struct Real
	{
		GetState_t getState;
		SetState_t setState;
		GetCaps_t getCaps;
		Enable_t enable;
		GetGuids_t getGuids;
		GetBattery_t getBattery;
		GetKeystroke_t getKeystroke;
		GetState_t getStateEx;
		WaitGuide_t waitGuide;
		CancelGuide_t cancelGuide;
		PowerOff_t powerOff;
	} g_real{};

	INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;

	BOOL CALLBACK LoadReal(PINIT_ONCE, PVOID, PVOID*)
	{
		wchar_t sys[MAX_PATH];
		GetSystemDirectoryW(sys, MAX_PATH);

		HMODULE mod = nullptr;
		for (const wchar_t* name : { L"\\xinput1_3.dll", L"\\xinput1_4.dll", L"\\xinput9_1_0.dll" })
		{
			mod = LoadLibraryW((std::wstring(sys) + name).c_str());
			if (mod) break;
		}
		if (!mod)
		{
			ss::Log("could not load any system XInput DLL");
			return TRUE;
		}

		auto get = [mod](const char* n) { return GetProcAddress(mod, n); };
		g_real.getState = reinterpret_cast<GetState_t>(get("XInputGetState"));
		g_real.setState = reinterpret_cast<SetState_t>(get("XInputSetState"));
		g_real.getCaps = reinterpret_cast<GetCaps_t>(get("XInputGetCapabilities"));
		g_real.enable = reinterpret_cast<Enable_t>(get("XInputEnable"));
		g_real.getGuids = reinterpret_cast<GetGuids_t>(get("XInputGetDSoundAudioDeviceGuids"));
		g_real.getBattery = reinterpret_cast<GetBattery_t>(get("XInputGetBatteryInformation"));
		g_real.getKeystroke = reinterpret_cast<GetKeystroke_t>(get("XInputGetKeystroke"));
		g_real.getStateEx = reinterpret_cast<GetState_t>(get(MAKEINTRESOURCEA(100)));
		g_real.waitGuide = reinterpret_cast<WaitGuide_t>(get(MAKEINTRESOURCEA(101)));
		g_real.cancelGuide = reinterpret_cast<CancelGuide_t>(get(MAKEINTRESOURCEA(102)));
		g_real.powerOff = reinterpret_cast<PowerOff_t>(get(MAKEINTRESOURCEA(103)));
		if (!g_real.getStateEx) g_real.getStateEx = g_real.getState;
		return TRUE;
	}

	const Real& R()
	{
		InitOnceExecuteOnce(&g_once, LoadReal, nullptr, nullptr);
		return g_real;
	}

	// Translates the game's user index to a physical one. False = hide it.
	bool Map(DWORD user, DWORD& physical)
	{
		if (!ss::g_cfg.active)
		{
			physical = user;
			return true;
		}
		if (user != 0 || ss::g_cfg.pad < 0) return false;
		physical = static_cast<DWORD>(ss::g_cfg.pad);
		return true;
	}
}

extern "C"
{
	DWORD WINAPI Proxy_XInputGetState(DWORD user, XINPUT_STATE* state)
	{
		DWORD p;
		if (!R().getState) return ERROR_DEVICE_NOT_CONNECTED;
		return Map(user, p) ? g_real.getState(p, state) : ERROR_DEVICE_NOT_CONNECTED;
	}

	DWORD WINAPI Proxy_XInputGetStateEx(DWORD user, XINPUT_STATE* state)
	{
		DWORD p;
		if (!R().getStateEx) return ERROR_DEVICE_NOT_CONNECTED;
		return Map(user, p) ? g_real.getStateEx(p, state) : ERROR_DEVICE_NOT_CONNECTED;
	}

	DWORD WINAPI Proxy_XInputSetState(DWORD user, XINPUT_VIBRATION* vibration)
	{
		DWORD p;
		if (!R().setState) return ERROR_DEVICE_NOT_CONNECTED;
		return Map(user, p) ? g_real.setState(p, vibration) : ERROR_DEVICE_NOT_CONNECTED;
	}

	DWORD WINAPI Proxy_XInputGetCapabilities(DWORD user, DWORD flags, XINPUT_CAPABILITIES* caps)
	{
		DWORD p;
		if (!R().getCaps) return ERROR_DEVICE_NOT_CONNECTED;
		return Map(user, p) ? g_real.getCaps(p, flags, caps) : ERROR_DEVICE_NOT_CONNECTED;
	}

	void WINAPI Proxy_XInputEnable(BOOL enable)
	{
		if (R().enable) g_real.enable(enable);
	}

	DWORD WINAPI Proxy_XInputGetDSoundAudioDeviceGuids(DWORD user, GUID* render, GUID* capture)
	{
		DWORD p;
		if (!R().getGuids) return ERROR_DEVICE_NOT_CONNECTED;
		return Map(user, p) ? g_real.getGuids(p, render, capture) : ERROR_DEVICE_NOT_CONNECTED;
	}

	DWORD WINAPI Proxy_XInputGetBatteryInformation(DWORD user, BYTE type, XINPUT_BATTERY_INFORMATION* info)
	{
		DWORD p;
		if (!R().getBattery) return ERROR_DEVICE_NOT_CONNECTED;
		return Map(user, p) ? g_real.getBattery(p, type, info) : ERROR_DEVICE_NOT_CONNECTED;
	}

	DWORD WINAPI Proxy_XInputGetKeystroke(DWORD user, DWORD reserved, PXINPUT_KEYSTROKE keystroke)
	{
		if (!R().getKeystroke) return ERROR_DEVICE_NOT_CONNECTED;
		if (!ss::g_cfg.active) return g_real.getKeystroke(user, reserved, keystroke);

		DWORD p;
		if (user == XUSER_INDEX_ANY) user = 0;
		if (!Map(user, p)) return ERROR_DEVICE_NOT_CONNECTED;
		const DWORD r = g_real.getKeystroke(p, reserved, keystroke);
		if (r == ERROR_SUCCESS && keystroke) keystroke->UserIndex = 0;
		return r;
	}

	DWORD WINAPI Proxy_XInputWaitForGuideButton(DWORD user, DWORD flags, LPVOID overlapped)
	{
		DWORD p;
		if (!R().waitGuide) return ERROR_DEVICE_NOT_CONNECTED;
		return Map(user, p) ? g_real.waitGuide(p, flags, overlapped) : ERROR_DEVICE_NOT_CONNECTED;
	}

	DWORD WINAPI Proxy_XInputCancelGuideButtonWait(DWORD user)
	{
		DWORD p;
		if (!R().cancelGuide) return ERROR_DEVICE_NOT_CONNECTED;
		return Map(user, p) ? g_real.cancelGuide(p) : ERROR_DEVICE_NOT_CONNECTED;
	}

	DWORD WINAPI Proxy_XInputPowerOffController(DWORD user)
	{
		DWORD p;
		if (!R().powerOff) return ERROR_DEVICE_NOT_CONNECTED;
		return Map(user, p) ? g_real.powerOff(p) : ERROR_DEVICE_NOT_CONNECTED;
	}

	// Lets the launcher identify (and upgrade) its own proxy in the game folder.
	const char* WINAPI Proxy_Version()
	{
		return "MW2SS_PROXY " MW2SS_VERSION;
	}
}
