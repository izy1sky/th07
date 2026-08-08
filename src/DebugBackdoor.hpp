#pragma once

#include "inttypes.hpp"

#ifdef __cplusplus
extern "C"
{
#endif

// Debug backdoor: instantly finish the current run and jump to the result
// screen. score is clamped to [0, 999999999] by GameManager::CutChain.
void ThDebugFinish(i32 score);

// Freeze/unfreeze the game while the debug console is open so typed keys do
// not reach gameplay.
void ThDebugSetPaused(i32 paused);

#ifdef __cplusplus
}
#endif
