// dsound.dll proxy: iw4x.exe imports DSOUND.dll, so a copy in the game
// folder is loaded with the game. Every export jumps to the real system DLL.
#include <windows.h>
#include <string>

namespace
{
	enum Export
	{
		DirectSoundCreate,
		DirectSoundEnumerateA,
		DirectSoundEnumerateW,
		DllCanUnloadNow,
		DllGetClassObject,
		DirectSoundCaptureCreate,
		DirectSoundCaptureEnumerateA,
		DirectSoundCaptureEnumerateW,
		GetDeviceID,
		DirectSoundFullDuplexCreate,
		DirectSoundCreate8,
		DirectSoundCaptureCreate8,
		ExportCount
	};

	const char* const g_names[ExportCount] = {
		"DirectSoundCreate", "DirectSoundEnumerateA", "DirectSoundEnumerateW", "DllCanUnloadNow",
		"DllGetClassObject", "DirectSoundCaptureCreate", "DirectSoundCaptureEnumerateA",
		"DirectSoundCaptureEnumerateW", "GetDeviceID", "DirectSoundFullDuplexCreate",
		"DirectSoundCreate8", "DirectSoundCaptureCreate8",
	};
}

extern "C" FARPROC g_dsoundReal[ExportCount] = {};

namespace ss
{
	bool LoadRealDsound()
	{
		wchar_t sys[MAX_PATH];
		GetSystemDirectoryW(sys, MAX_PATH);
		HMODULE real = LoadLibraryW((std::wstring(sys) + L"\\dsound.dll").c_str());
		if (!real) return false;
		for (int i = 0; i < ExportCount; ++i) g_dsoundReal[i] = GetProcAddress(real, g_names[i]);
		return true;
	}
}

// Register/stack-preserving forwarders.
#define FORWARD(name, index) \
	extern "C" __declspec(naked) void Proxy_##name() { __asm { jmp dword ptr [g_dsoundReal + index * 4] } }

FORWARD(DirectSoundCreate, 0)
FORWARD(DirectSoundEnumerateA, 1)
FORWARD(DirectSoundEnumerateW, 2)
FORWARD(DllCanUnloadNow, 3)
FORWARD(DllGetClassObject, 4)
FORWARD(DirectSoundCaptureCreate, 5)
FORWARD(DirectSoundCaptureEnumerateA, 6)
FORWARD(DirectSoundCaptureEnumerateW, 7)
FORWARD(GetDeviceID, 8)
FORWARD(DirectSoundFullDuplexCreate, 9)
FORWARD(DirectSoundCreate8, 10)
FORWARD(DirectSoundCaptureCreate8, 11)

// Identifies our DLL in the game folder.
extern "C" const char* WINAPI Proxy_Version()
{
	return "MW2SS_PROXY 2.0.0";
}
