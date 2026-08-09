#include "DebugBackdoor.hpp"

#include "Controller.hpp"
#include "EclManager.hpp"
#include "EnemyManager.hpp"
#include "GameManager.hpp"
#include "GameWindow.hpp"
#include "ResultScreen.hpp"
#include "Supervisor.hpp"

#include <vector>

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
void ThDebugSetScore(i32 score)
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
    g_GameManager.RegenerateGameIntegrityCsum();
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void ThDebugSetLives(i32 lives)
{
    if (!g_GameManager.globals)
    {
        return;
    }
    if (lives < 0)
    {
        lives = 0;
    }
    if (lives > 8)
    {
        lives = 8;
    }
    g_GameManager.SetLivesRemaining(lives);
    g_GameManager.RegenerateGameIntegrityCsum();
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void ThDebugSetBombs(i32 bombs)
{
    if (!g_GameManager.globals)
    {
        return;
    }
    if (bombs < 0)
    {
        bombs = 0;
    }
    if (bombs > 8)
    {
        bombs = 8;
    }
    g_GameManager.SetBombsRemainingAndComputeCsum(bombs);
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void ThDebugSetPower(i32 power)
{
    if (!g_GameManager.globals)
    {
        return;
    }
    if (power < 0)
    {
        power = 0;
    }
    if (power > 128)
    {
        power = 128;
    }
    g_GameManager.SetCurrentPower(power);
    g_GameManager.RegenerateGameIntegrityCsum();
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
i32 ThDebugGetScore()
{
    return g_GameManager.globals ? (i32)g_GameManager.globals->score : 0;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
i32 ThDebugGetLives()
{
    return g_GameManager.globals ? (i32)g_GameManager.globals->livesRemaining : 0;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
i32 ThDebugGetBombs()
{
    return g_GameManager.globals ? (i32)g_GameManager.globals->bombsRemaining : 0;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
i32 ThDebugGetPower()
{
    return g_GameManager.globals ? (i32)g_GameManager.globals->currentPower : 0;
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

struct DebugBossTarget
{
    i32 timeline;
    i32 time;
};

static bool DebugSubIsBoss(EclRawInstr *start)
{
    EclRawInstr *instr = start;
    i32 guard = 0;
    while (instr && instr->id != 1 && instr->size > 0 && guard++ < 100000)
    {
        if (instr->id == 99)
        {
            i32 slot = instr->args[0].i;
            if (slot >= 0)
            {
                return true;
            }
        }
        instr = (EclRawInstr *)((u8 *)instr + instr->size);
    }
    return false;
}

static bool DebugFindBossTargets(DebugBossTarget *mid, DebugBossTarget *final)
{
    if (!g_EclManager.eclFile)
    {
        return false;
    }

    i32 subCount = g_EclManager.eclFile->subCount;
    std::vector<i32> firstTimes(subCount, -1);
    std::vector<i32> firstTimelines(subCount, -1);

    for (i32 t = 0; t < g_EclManager.eclFile->timelineCount; t++)
    {
        EclTimelineInstr *instr = g_EclManager.timelinePtr[t];
        i32 guard = 0;
        while (instr && instr->time >= 0 && instr->size > 0 && guard++ < 100000)
        {
            if (instr->opcode >= 0 && instr->opcode <= 7)
            {
                i32 sub = instr->arg0;
                if (sub >= 0 && sub < subCount && DebugSubIsBoss(g_EclManager.subTable[sub]))
                {
                    if (firstTimes[sub] < 0 || instr->time < firstTimes[sub])
                    {
                        firstTimes[sub] = instr->time;
                        firstTimelines[sub] = t;
                    }
                }
            }
            instr = (EclTimelineInstr *)((u8 *)instr + instr->size);
        }
    }

    i32 midTime = 0x7fffffff;
    i32 finalTime = -1;
    i32 midTimeline = -1;
    i32 finalTimeline = -1;
    for (i32 s = 0; s < subCount; s++)
    {
        if (firstTimes[s] < 0)
        {
            continue;
        }
        if (firstTimes[s] < midTime)
        {
            midTime = firstTimes[s];
            midTimeline = firstTimelines[s];
        }
        if (firstTimes[s] > finalTime)
        {
            finalTime = firstTimes[s];
            finalTimeline = firstTimelines[s];
        }
    }

    if (midTimeline < 0 || finalTimeline < 0)
    {
        return false;
    }

    mid->timeline = midTimeline;
    mid->time = midTime;
    final->timeline = finalTimeline;
    final->time = finalTime;
    return true;
}

static void DebugRestartStageAndSeek(i32 timeline, i32 targetTime)
{
    i32 stage = g_GameManager.currentStage;
    if (stage < 1 || stage > 8 || !g_GameManager.globals)
    {
        return;
    }

    // Rebuild the current stage exactly like entering it, then fast-forward
    // every timeline to the target frame so the scene is clean instead of
    // being force-injected mid-frame.
    ThDebugStage(stage);

    if (!g_EclManager.eclFile || timeline < 0 || timeline >= g_EclManager.eclFile->timelineCount)
    {
        return;
    }

    for (i32 i = 0; i < g_EclManager.eclFile->timelineCount; i++)
    {
        EclTimeline *tl = &g_EnemyManager.timelines[i];
        tl->timelineInstr = g_EclManager.GetTimeline(i);
        tl->timelineTime = targetTime;
    }

    EclTimeline *target = &g_EnemyManager.timelines[timeline];
    if (target->timelineInstr && target->timelineInstr->time >= 0)
    {
        EnemyManager::RunEclTimeline(target);
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void ThDebugTimeline(i32 n)
{
    if (!g_EclManager.eclFile || !g_GameManager.globals)
    {
        return;
    }
    if (n < 0 || n >= g_EclManager.eclFile->timelineCount)
    {
        return;
    }

    EclTimelineInstr *first = g_EclManager.GetTimeline(n);
    if (!first || first->time < 0)
    {
        return;
    }

    Supervisor::DebugPrint("Debug: timeline %d @ %d\n", n, first->time);
    DebugRestartStageAndSeek(n, first->time);
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
        ThDebugFinalBoss();
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
    DebugBossTarget mid;
    DebugBossTarget final;
    if (DebugFindBossTargets(&mid, &final))
    {
        Supervisor::DebugPrint("Debug: midboss tl %d @ %d\n", mid.timeline, mid.time);
        DebugRestartStageAndSeek(mid.timeline, mid.time);
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void ThDebugFinalBoss()
{
    DebugBossTarget mid;
    DebugBossTarget final;
    if (DebugFindBossTargets(&mid, &final))
    {
        Supervisor::DebugPrint("Debug: boss tl %d @ %d\n", final.timeline, final.time);
        DebugRestartStageAndSeek(final.timeline, final.time);
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
        i32 tl = timelines[n - 1];
        EclTimelineInstr *first = g_EclManager.GetTimeline(tl);
        if (first && first->time >= 0)
        {
            Supervisor::DebugPrint("Debug: wave %d -> tl %d @ %d\n", n, tl, first->time);
            DebugRestartStageAndSeek(tl, first->time);
        }
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
