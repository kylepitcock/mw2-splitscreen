// Split screen rendering.
//
// SCR_UpdateScreen calls CL_CGameRendering once, for local client 0. We call it
// once per local player instead: each player's state is swapped in, their
// screen placement is set to their part of the screen, and the cgame draws
// the view and HUD into it.
#include "splitscreen.hpp"
#include "game.hpp"
#include "util.hpp"

namespace
{
	using namespace game;

	// CL_CGameRendering: localClientNum in ESI, returns nonzero if a view was drawn.
	void* oCGameRendering;

	__declspec(naked) int CallCGameRendering()
	{
		__asm
		{
			push esi
			xor esi, esi
			call oCGameRendering
			pop esi
			ret
		}
	}

	// Sys_WaitWorkerCmds: blocks until worker threads are idle. The cgame queues
	// skinning/animation work that reads the live per-client state, so we must
	// wait before swapping another player's state in.
	__declspec(naked) void WaitWorkerCmds()
	{
		__asm
		{
			push esi
			mov esi, 0x52CC60
			push 1
			mov eax, 0x52CA10
			call eax
			add esp, 4
			pop esi
			ret
		}
	}

	using ScrPlace_SetupViewport_t = void(__cdecl*)(void* scrPlace, float x, float y, float w, float h, int fullW, int fullH);
	auto ScrPlace_SetupViewport = reinterpret_cast<ScrPlace_SetupViewport_t>(0x4AEB80);
	constexpr uintptr_t scrPlaceView = 0x1084378;
	constexpr uintptr_t vidWidth = 0xA7FFFC;  // cls.vidConfig.displayWidth
	constexpr uintptr_t vidHeight = 0xA80000; // cls.vidConfig.displayHeight

	struct Rect
	{
		float x, y, w, h;
	};

	// Console layouts: 2 = top/bottom, 3 = top + two bottom quarters, 4 = quadrants.
	Rect ViewRect(int index, int count, float W, float H)
	{
		float hw = W / 2, hh = H / 2;
		switch (count)
		{
		case 1: return { 0, 0, W, H };
		case 2: return index == 0 ? Rect{ 0, 0, W, hh } : Rect{ 0, hh, W, H - hh };
		case 3:
			if (index == 0) return { 0, 0, W, hh };
			return index == 1 ? Rect{ 0, hh, hw, H - hh } : Rect{ hw, hh, W - hw, H - hh };
		default:
			return Rect{ (index & 1) ? hw : 0, (index & 2) ? hh : 0, (index & 1) ? W - hw : hw, (index & 2) ? H - hh : hh };
		}
	}

	void SetViewport(int index, int count)
	{
		int W = *reinterpret_cast<int*>(vidWidth), H = *reinterpret_cast<int*>(vidHeight);
		Rect r = ViewRect(index, count, static_cast<float>(W), static_cast<float>(H));
		ScrPlace_SetupViewport(reinterpret_cast<void*>(scrPlaceView), r.x, r.y, r.w, r.h, W, H);
	}

	int __cdecl RenderAllPlayers()
	{
		int players[MAX_SPLITSCREEN];
		int count = 0;
		for (int p = 0; p < MAX_SPLITSCREEN; ++p)
			if (ss::PlayerInGame(p)) players[count++] = p;

		if (count <= 1)
		{
			static int lastLog1 = 0;
			if (Sys_Milliseconds() - lastLog1 > 5000)
			{
				lastLog1 = Sys_Milliseconds();
				ss::Log("render: single view (extra players in game: %d %d %d)", ss::PlayerInGame(1), ss::PlayerInGame(2), ss::PlayerInGame(3));
			}
			if (count == 1) SetViewport(0, 1);
			return CallCGameRendering();
		}

		int drawn = 0;
		int results[MAX_SPLITSCREEN] = {};
		for (int i = 0; i < count; ++i)
		{
			int p = players[i];
			if (p != ss::g_live)
			{
				WaitWorkerCmds();
				ss::SwitchTo(p);
			}
			SetViewport(i, count);
			int r = CallCGameRendering();
			results[i] = r;
			if (p == 0) drawn = r;
		}
		static int lastLog = 0;
		int now = Sys_Milliseconds();
		if (now - lastLog > 2000)
		{
			lastLog = now;
			ss::Log("render: %d views, results %d %d %d %d", count, results[0], results[1], results[2], results[3]);
		}
		WaitWorkerCmds();
		ss::SwitchTo(0);
		SetViewport(0, count);
		return drawn;
	}

	__declspec(naked) void hkCGameRendering()
	{
		__asm
		{
			test esi, esi
			jnz other_client
			jmp RenderAllPlayers
		other_client:
			jmp oCGameRendering
		}
	}
}

namespace ss
{
	void InstallRendering()
	{
		Hook(0x5AC8D0, reinterpret_cast<void*>(hkCGameRendering), &oCGameRendering);
	}
}
