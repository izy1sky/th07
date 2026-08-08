#include "DebugBackdoor.hpp"

#include "GameManager.hpp"
#include "ResultScreen.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void ThDebugFinish(i32 score)
{
    if (!g_GameManager.globals)
    {
        return;
    }

    if (score < 0)
    {
        score = 0;
    }

    g_GameManager.globals->score = (u32)score;
    g_GameManager.globals->guiScore = (u32)score;
    g_GameManager.currentStage = 6;

    // Same transition the game uses after clearing a stage.
    GameManager::CutChain();
    ResultScreen::RegisterChain(1);
}
