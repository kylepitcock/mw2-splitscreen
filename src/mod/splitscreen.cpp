// Local player management and the per-player "context swap".
#include "splitscreen.hpp"
#include "game.hpp"
#include "util.hpp"
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>

namespace ss
{
	int g_live = 0;
}

namespace
{
	using namespace game;
	using ss::Log;

	constexpr int BLOCK_COUNT = sizeof(clientStateBlocks) / sizeof(clientStateBlocks[0]);

	struct PlayerState
	{
		bool active = false;
		unsigned char* blocks[BLOCK_COUNT] = {};
	};

	PlayerState g_players[MAX_SPLITSCREEN];
	unsigned char* g_template[BLOCK_COUNT] = {};
	bool g_haveTemplate = false;

	unsigned char* AllocBlocks(unsigned char** out)
	{
		for (int b = 0; b < BLOCK_COUNT; ++b)
		{
			out[b] = static_cast<unsigned char*>(
				VirtualAlloc(nullptr, clientStateBlocks[b].size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
			if (!out[b]) return nullptr;
		}
		return out[0];
	}

	void Save(unsigned char** dst)
	{
		for (int b = 0; b < BLOCK_COUNT; ++b)
			std::memcpy(dst[b], reinterpret_cast<void*>(clientStateBlocks[b].address), clientStateBlocks[b].size);
	}

	void Load(unsigned char* const* src)
	{
		for (int b = 0; b < BLOCK_COUNT; ++b)
			std::memcpy(reinterpret_cast<void*>(clientStateBlocks[b].address), src[b], clientStateBlocks[b].size);
	}

	template <typename T>
	T& At(uintptr_t address)
	{
		return *reinterpret_cast<T*>(address);
	}

	// ------------------------------------------------------------ error safety

	// A fatal error raised while an extra player is swapped in would otherwise
	// longjmp back to Com_Frame with that player's state still live, and the
	// engine would disconnect the wrong client. We trap it and drop the player.
	jmp_buf g_extraFrameJmp;
	bool g_inExtraFrame = false;
	char g_lastError[512];

	using Com_Error_t = void(__cdecl*)(int code, const char* fmt, ...);
	Com_Error_t oCom_Error;

	void __cdecl hkCom_Error(int code, const char* fmt, ...)
	{
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(g_lastError, sizeof(g_lastError), fmt, ap);
		va_end(ap);

		void* frames[24];
		USHORT n = CaptureStackBackTrace(1, 24, frames, nullptr);
		char trace[24 * 10 + 1] = {};
		for (USHORT i = 0; i < n; ++i) sprintf_s(trace + i * 9, sizeof(trace) - i * 9, "%08X ", reinterpret_cast<unsigned>(frames[i]));
		Log("Com_Error(%d): %s | stack: %s", code, g_lastError, trace);

		if (g_inExtraFrame)
		{
			Log("Com_Error(%d) while player %d was live: %s", code, ss::g_live + 1, g_lastError);
			longjmp(g_extraFrameJmp, 1);
		}
		oCom_Error(code, "%s", g_lastError);
	}

	// ------------------------------------------------------------ frame driver

	void PumpLoopback()
	{
		static unsigned char buffer[0x20000];
		msg_t msg;
		netadr_t from;
		MSG_Init(&msg, buffer, sizeof(buffer));
		while (ss::LoopbackDequeue(ss::g_live, &from, &msg))
			CL_PacketEvent(0, from, &msg, Sys_Milliseconds());
	}

	CL_Frame_t oCL_Frame;

	// Runs one engine client frame for an extra player. Returns false if it errored.
	bool RunExtraFrame(int player)
	{
		ss::SwitchTo(player);
		g_inExtraFrame = true;
		if (setjmp(g_extraFrameJmp) == 0)
		{
			PumpLoopback();
			oCL_Frame(0);
			g_inExtraFrame = false;
			return true;
		}
		g_inExtraFrame = false;
		return false;
	}

	void CaptureTemplate()
	{
		if (g_haveTemplate || ss::g_live != 0) return;
		// Running, but not connected anywhere: a clean "main menu" client.
		if (!At<bool>(clientUIActive_isRunning) || At<int>(clientUIActive_connectionState) >= 3) return;
		if (!AllocBlocks(g_template)) return;
		Save(g_template);
		g_haveTemplate = true;
		Log("captured clean client template");
	}

	void RegisterCommands();
	int AddPlayer();

	// Test hook: "-ss_autotest N" on the command line adds players until there
	// are N, once player 1 has been in a match for a few seconds.
	int g_autotestPlayers = 0;
	int g_autotestActiveSince = 0;

	void ParseCommandLine()
	{
		const char* cmd = GetCommandLineA();
		if (const char* p = std::strstr(cmd, "-ss_autotest "))
			g_autotestPlayers = atoi(p + 13);
		if (g_autotestPlayers) Log("autotest: %d players", g_autotestPlayers);
	}

	// Exactly what the main menu's "Private Match" button runs (ui_mp/main_text.menu),
	// then the lobby's "Start Game" (ui_mp/createserver.menu).
	constexpr const char* kPrivateMatchCommands =
		"xrequirelivesignin; set systemlink 0; set splitscreen 0; set onlinegame 1; exec default_xboxlive.cfg; "
		"set party_maxplayers 18; set xblive_privateserver 0; set xblive_rankedmatch 0; xstartprivateparty; "
		"set ui_mptype 0; xcheckezpatch; exec default_xboxlive.cfg; set xblive_rankedmatch 0; ui_enumeratesaved; "
		"set xblive_privateserver 1; xstartprivatematch; openmenu menu_xboxlive_privatelobby\n";

	int g_autotestStage = 0;
	int g_autotestStageTime = 0;

	void AutotestHost(int now)
	{
		if (g_autotestStage == 0)
		{
			if (!g_haveTemplate) return;
			g_autotestStage = 1;
			g_autotestStageTime = now;
		}
		if (g_autotestStage == 1 && now - g_autotestStageTime > 4000)
		{
			Log("autotest: opening private match");
			Cbuf_AddText(0, kPrivateMatchCommands);
			g_autotestStage = 2;
			g_autotestStageTime = now;
		}
		if (g_autotestStage == 2 && now - g_autotestStageTime > 6000)
		{
			Log("autotest: starting match");
			Cbuf_AddText(0, "xpartygo\n");
			g_autotestStage = 3;
		}
	}

	void Autotest()
	{
		if (!g_autotestPlayers) return;
		int now = Sys_Milliseconds();
		AutotestHost(now);
		if (At<int>(clientUIActive_connectionState) != 9) // CA_ACTIVE
		{
			g_autotestActiveSince = 0;
			return;
		}
		if (!g_autotestActiveSince) g_autotestActiveSince = now;
		if (now - g_autotestActiveSince < 4000) return;

		int count = 1;
		for (int p = 1; p < MAX_SPLITSCREEN; ++p) count += g_players[p].active;
		if (count < g_autotestPlayers)
		{
			AddPlayer();
			g_autotestActiveSince = now - 1000; // next one in ~3 s
		}
	}

	void __cdecl hkCL_Frame(int localClientNum)
	{
		static bool once = false;
		if (!once)
		{
			once = true;
			RegisterCommands();
			ParseCommandLine();
		}

		oCL_Frame(localClientNum);
		if (localClientNum != 0 || ss::g_live != 0) return;

		CaptureTemplate();
		Autotest();
		for (int p = 1; p < MAX_SPLITSCREEN; ++p)
		{
			if (!g_players[p].active) continue;
			if (!RunExtraFrame(p))
			{
				Log("player %d dropped after an error", p + 1);
				g_players[p].active = false;
				ss::LoopbackReset(p);
			}
		}
		ss::SwitchTo(0);
	}

	// ------------------------------------------------------------ players

	int AddPlayer()
	{
		if (!g_haveTemplate)
		{
			Com_Printf(0, "^1splitscreen: not ready yet (no client template)\n");
			return -1;
		}
		int p = 1;
		while (p < MAX_SPLITSCREEN && g_players[p].active) ++p;
		if (p == MAX_SPLITSCREEN)
		{
			Com_Printf(0, "^1splitscreen: already %d players\n", MAX_SPLITSCREEN);
			return -1;
		}
		if (!g_players[p].blocks[0] && !AllocBlocks(g_players[p].blocks))
		{
			Com_Printf(0, "^1splitscreen: out of memory\n");
			return -1;
		}
		for (int b = 0; b < BLOCK_COUNT; ++b)
			std::memcpy(g_players[p].blocks[b], g_template[b], clientStateBlocks[b].size);
		ss::LoopbackReset(p);
		g_players[p].active = true;

		Log("adding player %d", p + 1);
		ss::SwitchTo(p);
		g_inExtraFrame = true;
		if (setjmp(g_extraFrameJmp) == 0)
			CL_ConnectToLocalServer();
		else
			g_players[p].active = false;
		g_inExtraFrame = false;
		Log("player %d connect issued, connectionState=%d", p + 1, At<int>(clientUIActive_connectionState));
		ss::SwitchTo(0);
		return g_players[p].active ? p : -1;
	}

	void RemovePlayer(int p)
	{
		if (p <= 0 || p >= MAX_SPLITSCREEN || !g_players[p].active) return;
		ss::SwitchTo(p);
		g_inExtraFrame = true;
		if (setjmp(g_extraFrameJmp) == 0) CL_Disconnect(0);
		g_inExtraFrame = false;
		ss::SwitchTo(0);
		g_players[p].active = false;
		ss::LoopbackReset(p);
		Log("removed player %d", p + 1);
	}

	game::cmd_function_s g_cmdAdd, g_cmdRemove, g_cmdStatus;

	void RegisterCommands()
	{
		Cmd_AddCommand("ss_addplayer", [] {
			int p = AddPlayer();
			if (p > 0) Com_Printf(0, "splitscreen: player %d joining\n", p + 1);
		}, &g_cmdAdd, 0);
		Cmd_AddCommand("ss_removeplayers", [] {
			for (int p = MAX_SPLITSCREEN - 1; p > 0; --p) RemovePlayer(p);
		}, &g_cmdRemove, 0);
		Cmd_AddCommand("ss_status", [] {
			for (int p = 0; p < MAX_SPLITSCREEN; ++p)
			{
				if (p && !g_players[p].active) continue;
				ss::SwitchTo(p);
				Com_Printf(0, "player %d: connectionState %d\n", p + 1, At<int>(clientUIActive_connectionState));
			}
			ss::SwitchTo(0);
		}, &g_cmdStatus, 0);
		Log("commands registered");
	}
}

namespace ss
{
	bool PlayerActive(int player)
	{
		return player == 0 || (player > 0 && player < MAX_SPLITSCREEN && g_players[player].active);
	}

	void SwitchTo(int player)
	{
		if (player == g_live) return;
		if (!g_players[g_live].blocks[0] && !AllocBlocks(g_players[g_live].blocks)) return;
		Save(g_players[g_live].blocks);
		Load(g_players[player].blocks);
		g_live = player;
	}

	void InstallSplitscreen()
	{
		Hook(CL_Frame, hkCL_Frame, &oCL_Frame);
		Hook(0x4B22D0, hkCom_Error, &oCom_Error);
		InstallLoopback();
	}
}
