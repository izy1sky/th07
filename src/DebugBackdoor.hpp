#pragma once

#include "inttypes.hpp"

#ifdef __cplusplus
extern "C"
{
#endif

// Debug backdoor: instantly finish the current run and jump to the result
// screen. score is clamped to [0, 999999999] by GameManager::CutChain.
void ThDebugFinish(i32 score);

#ifdef __cplusplus
}
#endif
