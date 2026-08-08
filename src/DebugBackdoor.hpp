#pragma once

#include "inttypes.hpp"

#ifdef __cplusplus
extern "C"
{
#endif

// Injected into the raw input for exactly one logic tick (used to open/close
// the in-game pause menu as if the player pressed ESC).
extern u16 g_DebugInjectedInput;

// Debug backdoor: instantly finish the current run and jump to the result
// screen. score is clamped to [0, 999999999] by GameManager::CutChain.
void ThDebugFinish(i32 score);

// Open/close the in-game ESC pause menu while the debug console is open.
void ThDebugSetPaused(i32 paused);

#ifdef __cplusplus
}
#endif
