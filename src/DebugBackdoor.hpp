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

// Jump to stage (1-8) using the game's own stage-transition path. The run
// state (score, lives, bombs) is preserved.
void ThDebugStage(i32 stage);

// Force-run ECL timeline n (mid-stage waves / boss spawns).
void ThDebugTimeline(i32 n);

// Call ECL subroutine subId on the first active boss (boss patterns and
// spellcards are ECL subroutines; sub IDs come from the stage .ecl data).
void ThDebugBoss(i32 subId);

#ifdef __cplusplus
}
#endif
