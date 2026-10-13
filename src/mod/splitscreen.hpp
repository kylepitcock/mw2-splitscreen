#pragma once

namespace ss
{
	// The PC engine only has storage for one local client. Every extra player's
	// client/cgame/UI state lives in a backup copy and is swapped into the
	// engine's globals while that player is being processed ("context swap").
	// While swapped in, the engine always runs as localClientNum 0.

	extern int g_live; // which local player's state currently occupies the engine globals

	bool PlayerActive(int player);
	void SwitchTo(int player);

	void InstallSplitscreen();
	void InstallLoopback();

	// Loopback queues for players 1..3 (player 0 uses the engine's own).
	void LoopbackQueue(int player, const void* data, int length);
	bool LoopbackDequeue(int player, void* fromAdr, void* msg);
	void LoopbackReset(int player);
}
