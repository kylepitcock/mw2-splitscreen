#include "common.h"
#include <MinHook.h>

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
	if (reason != DLL_PROCESS_ATTACH) return TRUE;
	DisableThreadLibraryCalls(module);

	// iw4x.dll imports us, so this runs before the game or IW4x initialise.
	ss::LoadConfig();
	if (!ss::g_cfg.active) return TRUE; // normal launch: plain XInput pass-through

	if (MH_Initialize() != MH_OK)
	{
		ss::Log("MH_Initialize failed; splitscreen hooks disabled");
		return TRUE;
	}
	ss::InstallWindowHooks();
	ss::InstallFileRedirect();
	ss::InstallD3DHooks();
	ss::Log("hooks installed");
	return TRUE;
}
