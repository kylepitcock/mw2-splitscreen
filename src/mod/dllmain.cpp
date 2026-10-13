// MW2 split screen: loaded as dsound.dll, which iw4x.exe imports, so we are
// in the process before the game starts. DirectSound calls go to the system DLL.
//
// Hooks are installed once the client has initialised, i.e. after IW4x has
// applied its own patches, so MinHook chains onto them instead of being
// overwritten by them.
#include "util.hpp"
#include "splitscreen.hpp"
#include "game.hpp"
#include <MinHook.h>

namespace
{
	DWORD WINAPI InstallWhenReady(LPVOID)
	{
		for (int waited = 0; !*reinterpret_cast<volatile bool*>(game::clientUIActive_isRunning); waited += 50)
		{
			if (waited > 10 * 60 * 1000) return 0;
			Sleep(50);
		}
		Sleep(250);
		ss::InstallSplitscreen();
		ss::Log("hooks installed");
		return 0;
	}
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
	if (reason != DLL_PROCESS_ATTACH) return TRUE;
	DisableThreadLibraryCalls(module);
	if (!ss::LoadRealDsound()) return FALSE;

	if (!ss::IsSupportedGame()) return TRUE; // not iw4mp 1.2.159: plain dsound proxy

	ss::LogInit();
	ss::Log("mw2 splitscreen loading");
	if (MH_Initialize() != MH_OK)
	{
		ss::Log("MH_Initialize failed");
		return TRUE;
	}
	if (HANDLE t = CreateThread(nullptr, 0, InstallWhenReady, nullptr, 0, nullptr)) CloseHandle(t);
	return TRUE;
}
