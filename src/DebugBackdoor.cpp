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

static i32 CollectTimelinesByOpcode(i32 opcodeA, i32 opcodeB, i32 *out,
                                    i32 maxOut)
{
    i32 count = 0;
    if (!g_EclManager.eclFile)
    {
        return 0;
    }

    for (i32 t = 0; t < g_EclManager.eclFile->timelineCount && count < maxOut; t++)
    {
        EclTimelineInstr *instr = g_EclManager.timelinePtr[t];
        i32 guard = 0;
        while (instr && instr->time >= 0 && instr->size > 0 && guard++ < 100000)
        {
            if (instr->opcode == opcodeA || instr->opcode == opcodeB)
            {
                out[count++] = t;
                break;
            }
            instr = (EclTimelineInstr *)((u8 *)instr + instr->size);
        }
    }

    return count;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
i32 ThDebugSpell(i32 n)
{
    Enemy *boss = NULL;

    if (!g_EclManager.eclFile)
    {
        return -2;
    }

    for (i32 i = 0; i < 8; i++)
    {
        if (g_EnemyManager.bosses[i] && g_EnemyManager.bosses[i]->active)
        {
            boss = g_EnemyManager.bosses[i];
            break;
        }
    }
    if (!boss)
    {
        return -1;
    }

    i32 found = 0;
    for (i32 s = 0; s < g_EclManager.eclFile->subCount; s++)
    {
        EclRawInstr *instr = g_EclManager.subTable[s];
        i32 guard = 0;
        while (instr && instr->id != 1 && instr->size > 0 && guard++ < 100000)
        {
            if (instr->id == 90) // begin spellcard
            {
                found++;
                if (found == n)
                {
                    g_EclManager.CallEclSub(&boss->currentContext, (i16)s);
                    return 0;
                }
                break;
            }
            instr = (EclRawInstr *)((u8 *)instr + instr->size);
        }
    }

    return -3;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void ThDebugMidboss()
{
    i32 timelines[8];
    i32 count = CollectTimelinesByOpcode(2, 3, timelines, 8);
    if (count > 0)
    {
        ThDebugTimeline(timelines[0]);
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void ThDebugFinalBoss()
{
    i32 timelines[8];
    i32 count = CollectTimelinesByOpcode(2, 3, timelines, 8);
    if (count > 0)
    {
        ThDebugTimeline(timelines[count - 1]);
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void ThDebugWave(i32 n)
{
    i32 timelines[64];
    i32 count = CollectTimelinesByOpcode(0, 1, timelines, 64);
    if (n >= 1 && n <= count)
    {
        ThDebugTimeline(timelines[n - 1]);
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void ThDebugSub(i32 subId)
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
