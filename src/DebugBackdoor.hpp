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

// Jump to the n-th spellcard of the current stage (1-based). Requires an
// active boss. Returns 0 on success, -1 no boss, -2 no ECL, -3 no such spell.
i32 ThDebugSpell(i32 n);

// Run the first / last boss-spawn timeline (midboss / final boss).
void ThDebugMidboss();
void ThDebugFinalBoss();

// Run the n-th enemy-wave timeline (1-based, opcode 0/1 spawns).
void ThDebugWave(i32 n);

// Raw: call ECL subroutine subId on the first active boss.
void ThDebugSub(i32 subId);

// Run-state adjustments used by the F9 practice panel.
void ThDebugSetScore(i32 score);
void ThDebugSetLives(i32 lives);
void ThDebugSetBombs(i32 bombs);
void ThDebugSetPower(i32 power);
i32 ThDebugGetScore();
i32 ThDebugGetLives();
i32 ThDebugGetBombs();
i32 ThDebugGetPower();

#ifdef __cplusplus
}
#endif
