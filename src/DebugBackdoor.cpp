#include "DebugBackdoor.hpp"

#include "Controller.hpp"
#include "EclManager.hpp"
#include "EnemyManager.hpp"
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

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void ThDebugStage(i32 stage)
{
    if (stage < 1)
    {
        stage = 1;
    }
    if (stage > 8)
    {
        stage = 8;
    }

    // curState 3 makes GameManager::AddedCallback keep the run state instead
    // of resetting it, and it increments currentStage before setting up the
    // stage, so set it one below the requested stage.
    g_Supervisor.curState = 3;
    g_GameManager.currentStage = stage - 1;
    GameManager::CutChain();
    GameManager::RegisterChain();
    g_Supervisor.wantedState = 2;
    g_Supervisor.curState = 2;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void ThDebugTimeline(i32 n)
{
    if (!g_EclManager.eclFile)
    {
        return;
    }
    if (n < 0 || n >= g_EclManager.eclFile->timelineCount)
    {
        return;
    }

    EclTimeline *tl = &g_EnemyManager.timelines[n];
    if (!tl->timelineInstr)
    {
        tl->timelineInstr = g_EclManager.GetTimeline(n);
    }
    if (!tl->timelineInstr)
    {
        return;
    }

    tl->timelineTime = tl->timelineInstr->time;
    EnemyManager::RunEclTimeline(tl);
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void ThDebugBoss(i32 subId)
{
    for (i32 i = 0; i < 8; i++)
    {
        Enemy *boss = g_EnemyManager.bosses[i];
        if (boss && boss->active)
        {
            g_EclManager.CallEclSub(&boss->currentContext, (i16)subId);
            return;
        }
    }
}
