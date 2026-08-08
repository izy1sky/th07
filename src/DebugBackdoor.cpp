#include "DebugBackdoor.hpp"

#include "Controller.hpp"
#include "GameManager.hpp"
#include "GameWindow.hpp"
#include "ResultScreen.hpp"
#include "Supervisor.hpp"

u16 g_DebugInjectedInput;

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

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void ThDebugSetPaused(i32 paused)
{
    if (paused)
    {
        if (g_GameManager.notInMenu && !g_GameManager.isInPauseMenu)
        {
            g_DebugInjectedInput = TH_BUTTON_MENU;
        }
    }
    else
    {
        if (g_GameManager.isInPauseMenu)
        {
            g_DebugInjectedInput = TH_BUTTON_MENU;
        }
    }
}
