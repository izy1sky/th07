#include "PointDevice.hpp"

#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "AsciiManager.hpp"
#include "BulletManager.hpp"
#include "Controller.hpp"
#include "EclManager.hpp"
#include "EffectManager.hpp"
#include "EnemyManager.hpp"
#include "FileSystem.hpp"
#include "GameManager.hpp"
#include "Gui.hpp"
#include "ItemManager.hpp"
#include "Player.hpp"
#include "ReplayManager.hpp"
#include "Rng.hpp"
#include "SoundPlayer.hpp"
#include "Stage.hpp"
#include "Supervisor.hpp"
#include "utils.hpp"

namespace PointDevice
{

// A periodic checkpoint is wanted after this many gameplay frames (15 seconds).
#define PD_PERIODIC_INTERVAL 900
// A boss attack checkpoint that cannot be taken safely within this many frames is dropped.
#define PD_FORCED_DEADLINE 120
// Invulnerability given after a rewind, so a checkpoint can never become a death loop.
#define PD_RESTORE_INVULNERABILITY 120
#define PD_MESSAGE_FRAMES 90

// Menus ignore input for this many frames after opening, so a player mashing Z
// does not pick an option by accident.
#define PD_MENU_INPUT_DELAY 20

#define PD_SAVE_FILE "pointdevice.sav"
#define PD_SAVE_MAGIC 0x37534450 // "PDS7"
#define PD_SAVE_VERSION 1

// The web branch still uses raw Supervisor state numbers.
#define PD_SUPERVISOR_STATE_MAINMENU 1
#define PD_SUPERVISOR_STATE_GAMEMANAGER 2
#define PD_SUPERVISOR_STATE_RESULTSCREEN_FROM_GAME 6
#define PD_STAGE6 6

#define PD_SAFE_BULLET_RADIUS 64.0f
#define PD_SAFE_BULLET_RADIUS_RELAXED 32.0f
#define PD_SAFE_ENEMY_RADIUS 48.0f

#define PD_COLOR_WHITE 0xffffffff
#define PD_COLOR_LIGHT_RED 0xffff8080
#define PD_COLOR_LIGHT_CYAN 0xffe0ffff
#define PD_COLOR_GREY 0xff808080

// Raw byte copy of a manager. The gameplay managers are plain data: fixed size
// arrays (their linked lists only link nodes inside those arrays) plus pointers
// into stage data that stays loaded for the whole stage, so a byte copy
// restores them exactly.
template <typename T> struct Blob
{
    alignas(T) u8 bytes[sizeof(T)];

    void Save(const T *src)
    {
        std::memcpy(this->bytes, (const void *)src, sizeof(T));
    }
    void Load(T *dst) const
    {
        std::memcpy((void *)dst, this->bytes, sizeof(T));
    }
};

struct Snapshot
{
    bool valid;
    Blob<GameManager> gameManager;
    Blob<ZunGlobals> globals;
    Blob<Player> player;
    Blob<BulletManager> bulletManager;
    Blob<EnemyManager> enemyManager;
    Blob<ItemManager> itemManager;
    Blob<EffectManager> effectManager;
    Blob<Stage> stage;
    Blob<Gui> gui;
    Blob<GuiImpl> guiImpl;
    Blob<EclManager> eclManager;
    Blob<EclGlobalVars> eclGlobalVars;
    Blob<Rng> rng;
    // The replay recorder appends one input per frame into a fixed size stage
    // buffer, so its cursor is rewound too. Pointdevice replays are never saved.
    bool hasReplayManager;
    Blob<ReplayManager> replayManager;
    // Stage background VMs live in a heap array, and the stage script toggles
    // flags inside the loaded std file, so both are saved separately.
    std::vector<AnmVm> stageQuadVms;
    std::vector<i8> stageObjectFlags;
    i32 songIdx;
};

// What a saved run keeps: the selection, the stage, and the stats that stage
// started with. A mid-stage checkpoint cannot be written to disk, because it
// points into stage data that is loaded at a different address next time.
// Cherry values are stored relative to the per-run random cherryStart.
struct SaveData
{
    u32 magic;
    u32 version;
    u8 character;
    u8 shotType;
    u8 difficulty;
    u8 stage;
    u32 score;
    u32 highScore;
    f32 currentPower;
    f32 bombsRemaining;
    f32 deaths;
    f32 bombsUsed;
    i32 grazeInTotal;
    i32 spellCardsCaptured;
    i32 pointItemsCollectedForExtend;
    i32 extendsFromPointItems;
    i32 nextNeededPointItemsForExtend;
    i32 cherry;
    i32 cherryMax;
    i32 cherryPlus;
    i32 rank;
    i32 playTimeAll;
    i8 powerItemCountForScore;
    u8 padding[3];
    i32 retries;
};

enum MenuKind
{
    MENU_NONE,
    MENU_DEATH,
    MENU_PAUSE,
};

enum DeathMenuItem
{
    DEATH_MENU_CONTINUE,
    DEATH_MENU_GIVE_UP,
    DEATH_MENU_COUNT,
};

enum PauseMenuItem
{
    PAUSE_MENU_RESUME,
    PAUSE_MENU_SAVE_AND_QUIT,
    PAUSE_MENU_QUIT,
    PAUSE_MENU_COUNT,
};

enum AutotestMode
{
    AUTOTEST_OFF,
    AUTOTEST_CONTINUE,
    AUTOTEST_GIVE_UP,
    AUTOTEST_SAVE_QUIT,
    AUTOTEST_HOLD_MENU,
};

struct State
{
    bool enabled;
    bool log;
    AutotestMode autotest;
    bool autotestBossSeen;
    bool autotestSaved;
    bool autotestResumed;
    i32 autotestFrames;
    MenuKind menu;
    i32 menuCursor;
    i32 menuFrames;
    bool audioPaused;
    bool hasSave;
    SaveData save;
    bool resuming;
    // The stats the current stage started with, written out by "Save and quit".
    SaveData stageStart;
    bool resumeKeyWasDown;
    bool runActive;
    bool pendingRestore;
    bool checkpointWanted;
    bool checkpointForced;
    i32 forcedFrames;
    i32 framesSinceCheckpoint;
    i32 songIdx;
    i32 retries;
    i32 checkpointMsgFrames;
    i32 retryMsgFrames;
    bool menuKeyWasDown;
};

static Snapshot g_Snapshot;
static State g_State;

#define PD_LOG(...)                                                                                \
    if (g_State.log)                                                                               \
    {                                                                                              \
        std::fprintf(stderr, "[pointdevice] stage %d: ", g_GameManager.currentStage);              \
        std::fprintf(stderr, __VA_ARGS__);                                                         \
        std::fputc('\n', stderr);                                                                  \
    }

static std::string SavePath()
{
    // On the web build the pref path is an IndexedDB mount, so the save
    // survives a page reload.
    return FileSystem::GetPrefPath(PD_SAVE_FILE);
}

static void LoadSave()
{
    FILE *file;
    SaveData save;

    g_State.hasSave = false;
    file = std::fopen(SavePath().c_str(), "rb");
    if (file == NULL)
    {
        return;
    }
    if (std::fread(&save, sizeof(save), 1, file) == 1 && save.magic == PD_SAVE_MAGIC &&
        save.version == PD_SAVE_VERSION && save.character < 3 && save.shotType < 2 && save.difficulty <= 5 &&
        save.stage >= 1 && save.stage <= 8)
    {
        g_State.save = save;
        g_State.hasSave = true;
    }
    std::fclose(file);
}

static bool WriteSave()
{
    FILE *file;
    bool ok;

    file = std::fopen(SavePath().c_str(), "wb");
    if (file == NULL)
    {
        return false;
    }
    ok = std::fwrite(&g_State.stageStart, sizeof(g_State.stageStart), 1, file) == 1;
    ok = std::fclose(file) == 0 && ok;
    if (ok)
    {
        g_State.save = g_State.stageStart;
        g_State.hasSave = true;
    }
    return ok;
}

// A saved run is used up when it is resumed.
static void DeleteSave()
{
    std::remove(SavePath().c_str());
    g_State.hasSave = false;
}

static const char *DifficultyName(i32 difficulty)
{
    static const char *names[] = {"Easy", "Normal", "Hard", "Lunatic", "Extra", "Phantasm"};
    return difficulty >= 0 && difficulty < (i32)ARRAY_SIZE(names) ? names[difficulty] : "?";
}

static const char *CharacterName(i32 character)
{
    static const char *names[] = {"Reimu", "Marisa", "Sakuya"};
    return character >= 0 && character < (i32)ARRAY_SIZE(names) ? names[character] : "?";
}

void Init(int argc, char *argv[])
{
    const char *env;
    i32 i;

    g_State.enabled = true;
    env = std::getenv("TH07_POINTDEVICE");
    if (env != NULL)
    {
        g_State.enabled = env[0] != '0';
    }
    env = std::getenv("TH07_POINTDEVICE_LOG");
    g_State.log = env != NULL && env[0] != '0';
    env = std::getenv("TH07_POINTDEVICE_AUTOTEST");
    g_State.autotest = AUTOTEST_OFF;
    if (env != NULL && env[0] != '0')
    {
        g_State.autotest = std::strcmp(env, "giveup") == 0     ? AUTOTEST_GIVE_UP
                           : std::strcmp(env, "savequit") == 0 ? AUTOTEST_SAVE_QUIT
                           : std::strcmp(env, "hold") == 0     ? AUTOTEST_HOLD_MENU
                                                               : AUTOTEST_CONTINUE;
    }
    for (i = 1; i < argc; i++)
    {
        if (std::strcmp(argv[i], "--pointdevice") == 0)
        {
            g_State.enabled = true;
        }
        else if (std::strcmp(argv[i], "--legacy") == 0)
        {
            g_State.enabled = false;
        }
    }
    LoadSave();
}

bool IsEnabled()
{
    return g_State.enabled;
}

bool IsRunActive()
{
    return g_State.runActive;
}

// Starts the saved run the same way the main menu starts a new game.
static void ResumeSave()
{
    const SaveData *save = &g_State.save;

    g_GameManager.character = save->character;
    g_GameManager.shotType = save->shotType;
    g_GameManager.shotTypeAndCharacter = save->character * 2 + save->shotType;
    g_GameManager.difficulty = save->difficulty;
    // GameManager::AddedCallback moves on to the next stage.
    g_GameManager.currentStage = save->stage - 1;
    g_GameManager.practice = 0;
    g_GameManager.demo = 0;
    g_GameManager.SetReplay(0);
    g_SoundPlayer.PlaySoundByIdx(SOUND_SELECT, 0);
    g_Supervisor.curState = PD_SUPERVISOR_STATE_GAMEMANAGER;
    g_Supervisor.StopAudio();
    while (g_SoundPlayer.ProcessQueues())
        ;

    g_State.enabled = true;
    g_State.resuming = true;
    DeleteSave();
}

bool MenuUpdate()
{
    const bool *keys;
    bool keyDown;
    bool resumeKeyDown;
    ZunVec3 pos;

    keys = SDL_GetKeyboardState(NULL);
    keyDown = keys != NULL && keys[SDL_SCANCODE_P];
    if (keyDown && !g_State.menuKeyWasDown)
    {
        g_State.enabled = !g_State.enabled;
        g_SoundPlayer.PlaySoundByIdx(SOUND_SELECT, 0);
    }
    g_State.menuKeyWasDown = keyDown;

    resumeKeyDown = (keys != NULL && keys[SDL_SCANCODE_R]) ||
                    (g_State.autotest == AUTOTEST_SAVE_QUIT && g_State.autotestSaved);
    if (g_State.hasSave && resumeKeyDown && !g_State.resumeKeyWasDown)
    {
        g_State.autotestSaved = false;
        g_State.autotestResumed = g_State.autotest == AUTOTEST_SAVE_QUIT;
        ResumeSave();
        return true;
    }
    g_State.resumeKeyWasDown = resumeKeyDown;

    pos = ZunVec3(16.0f, 456.0f, 0.0f);
    g_AsciiManager.SetColor(g_State.enabled ? PD_COLOR_LIGHT_RED : PD_COLOR_WHITE);
    AsciiManager::AddFormatText(&g_AsciiManager, &pos, "Pointdevice Mode %s  (P)",
                                g_State.enabled ? "ON " : "OFF");
    if (g_State.hasSave)
    {
        pos = ZunVec3(16.0f, 412.0f, 0.0f);
        g_AsciiManager.SetColor(PD_COLOR_LIGHT_CYAN);
        AsciiManager::AddFormatText(&g_AsciiManager, &pos, "Saved run: Stage %d %s %c %s  (R)", g_State.save.stage,
                                    CharacterName(g_State.save.character), g_State.save.shotType == 0 ? 'A' : 'B',
                                    DifficultyName(g_State.save.difficulty));
    }
    g_AsciiManager.SetColor(PD_COLOR_WHITE);
    return false;
}

void OnReturnToMenu()
{
    g_State.runActive = false;
    g_State.pendingRestore = false;
    g_State.menu = MENU_NONE;
    g_Snapshot.valid = false;
}

// GameManager::AddedCallback has just reset the stats for a new game, so the
// saved ones are put back on top.
static void ApplyResume()
{
    const SaveData *save = &g_State.save;
    ZunGlobals *globals = g_GameManager.globals;

    globals->guiScore = save->score;
    globals->score = save->score;
    if (globals->highScore < save->highScore)
    {
        globals->highScore = save->highScore;
    }
    globals->currentPower = save->currentPower;
    globals->grazeInTotal = save->grazeInTotal;
    globals->spellCardsCaptured = save->spellCardsCaptured;
    globals->pointItemsCollectedForExtend = save->pointItemsCollectedForExtend;
    globals->extendsFromPointItems = save->extendsFromPointItems;
    globals->nextNeededPointItemsForExtend = save->nextNeededPointItemsForExtend;
    globals->deaths = save->deaths;
    globals->bombsUsed = save->bombsUsed;
    g_GameManager.SetBombsRemainingAndComputeCsum((i32)save->bombsRemaining);
    g_GameManager.cherry = globals->cherryStart + save->cherry;
    g_GameManager.cherryMax = globals->cherryStart + save->cherryMax;
    g_GameManager.cherryPlus = globals->cherryStart + save->cherryPlus;
    g_GameManager.rank.rank = save->rank;
    g_GameManager.playTimeAll = save->playTimeAll;
    g_GameManager.powerItemCountForScore = save->powerItemCountForScore;
    g_State.retries = save->retries;
    g_Gui.showLives = 2;
    g_Gui.showBombs = 2;
    g_Gui.showPower = 2;
    g_Gui.showGraze = 2;
    g_Gui.showPoint = 2;
    PD_LOG("resumed saved run (score %u, power %d, bombs %d, retries %d)", save->score, (i32)save->currentPower,
           (i32)save->bombsRemaining, save->retries);
}

void OnGameStart(bool isNewGame)
{
    if (isNewGame)
    {
        g_State.runActive = g_State.enabled && !g_GameManager.replay && !g_GameManager.demo;
        g_State.retries = 0;
        PD_LOG("new game, pointdevice %s", g_State.runActive ? "active" : "inactive");
        if (g_State.resuming && g_State.runActive)
        {
            ApplyResume();
        }
        g_State.resuming = false;
    }
    g_State.menu = MENU_NONE;
    // Snapshots point into stage data, so they never survive a stage change.
    g_Snapshot.valid = false;
    g_State.pendingRestore = false;
    g_State.checkpointWanted = true;
    g_State.checkpointForced = true;
    g_State.forcedFrames = 0;
    g_State.framesSinceCheckpoint = 0;
    // Stage 6 starts silent; every other stage starts on its first track.
    g_State.songIdx = g_GameManager.currentStage != PD_STAGE6 ? 0 : -1;
    g_State.checkpointMsgFrames = 0;
    g_State.retryMsgFrames = 0;
}

static f32 DistanceSquared(const ZunVec3 *a, const ZunVec3 *b)
{
    f32 dx = a->x - b->x;
    f32 dy = a->y - b->y;
    return dx * dx + dy * dy;
}

// A checkpoint is only worth taking when the player can survive the next
// moment after being sent back to it.
static bool IsSafeToCheckpoint(bool relaxed)
{
    const ZunVec3 *playerPos = &g_Player.positionCenter;
    f32 bulletRadius = relaxed ? PD_SAFE_BULLET_RADIUS_RELAXED : PD_SAFE_BULLET_RADIUS;
    i32 i;

    if (g_Player.playerState != PLAYER_STATE_ALIVE && g_Player.playerState != PLAYER_STATE_INVULNERABLE)
    {
        return false;
    }
    if (g_Player.bombInfo.isInUse || g_Gui.HasCurrentMsgIdx())
    {
        return false;
    }
    for (i = 0; i < (i32)ARRAY_SIZE(g_BulletManager.bullets); i++)
    {
        if (g_BulletManager.bullets[i].state != 0 &&
            DistanceSquared(&g_BulletManager.bullets[i].pos, playerPos) < bulletRadius * bulletRadius)
        {
            return false;
        }
    }
    if (!relaxed)
    {
        for (i = 0; i < (i32)ARRAY_SIZE(g_BulletManager.lasers); i++)
        {
            if (g_BulletManager.lasers[i].inUse)
            {
                return false;
            }
        }
    }
    for (i = 0; i < (i32)ARRAY_SIZE(g_EnemyManager.enemies); i++)
    {
        if (g_EnemyManager.enemies[i].active &&
            DistanceSquared(&g_EnemyManager.enemies[i].position, playerPos) <
                PD_SAFE_ENEMY_RADIUS * PD_SAFE_ENEMY_RADIUS)
        {
            return false;
        }
    }
    return true;
}

static void RecordStageStart()
{
    SaveData *save = &g_State.stageStart;
    const ZunGlobals *globals = g_GameManager.globals;

    std::memset(save, 0, sizeof(*save));
    save->magic = PD_SAVE_MAGIC;
    save->version = PD_SAVE_VERSION;
    save->character = g_GameManager.character;
    save->shotType = g_GameManager.shotType;
    save->difficulty = (u8)g_GameManager.difficulty;
    save->stage = (u8)g_GameManager.currentStage;
    // The real score lives in guiScore until the first frame of a stage.
    save->score = globals->score > globals->guiScore ? globals->score : globals->guiScore;
    save->highScore = globals->highScore;
    save->currentPower = globals->currentPower;
    save->bombsRemaining = globals->bombsRemaining;
    save->deaths = globals->deaths;
    save->bombsUsed = globals->bombsUsed;
    save->grazeInTotal = globals->grazeInTotal;
    save->spellCardsCaptured = globals->spellCardsCaptured;
    save->pointItemsCollectedForExtend = globals->pointItemsCollectedForExtend;
    save->extendsFromPointItems = globals->extendsFromPointItems;
    save->nextNeededPointItemsForExtend = globals->nextNeededPointItemsForExtend;
    save->cherry = g_GameManager.cherry - globals->cherryStart;
    save->cherryMax = g_GameManager.cherryMax - globals->cherryStart;
    save->cherryPlus = g_GameManager.cherryPlus - globals->cherryStart;
    save->rank = g_GameManager.rank.rank;
    save->playTimeAll = g_GameManager.playTimeAll;
    save->powerItemCountForScore = g_GameManager.powerItemCountForScore;
    save->retries = g_State.retries;
}

static void TakeCheckpoint()
{
    i32 i;

    if (!g_Snapshot.valid)
    {
        RecordStageStart();
    }
    g_Snapshot.gameManager.Save(&g_GameManager);
    g_Snapshot.globals.Save(g_GameManager.globals);
    g_Snapshot.player.Save(&g_Player);
    g_Snapshot.bulletManager.Save(&g_BulletManager);
    g_Snapshot.enemyManager.Save(&g_EnemyManager);
    g_Snapshot.itemManager.Save(&g_ItemManager);
    g_Snapshot.effectManager.Save(&g_EffectManager);
    g_Snapshot.stage.Save(&g_Stage);
    g_Snapshot.gui.Save(&g_Gui);
    g_Snapshot.guiImpl.Save(g_Gui.impl);
    g_Snapshot.eclManager.Save(&g_EclManager);
    g_Snapshot.eclGlobalVars.Save(&g_GlobalEclVars);
    g_Snapshot.rng.Save(&g_Rng);
    g_Snapshot.hasReplayManager = g_ReplayManager != NULL;
    if (g_Snapshot.hasReplayManager)
    {
        g_Snapshot.replayManager.Save(g_ReplayManager);
    }

    g_Snapshot.stageQuadVms.resize(g_Stage.quadCount);
    if (g_Stage.quadCount > 0)
    {
        std::memcpy((void *)g_Snapshot.stageQuadVms.data(), g_Stage.quadVms, sizeof(AnmVm) * g_Stage.quadCount);
    }
    g_Snapshot.stageObjectFlags.resize(g_Stage.objectsCount);
    for (i = 0; i < g_Stage.objectsCount; i++)
    {
        g_Snapshot.stageObjectFlags[i] = g_Stage.objects[i]->flags;
    }
    g_Snapshot.songIdx = g_State.songIdx;
    g_Snapshot.valid = true;

    g_State.checkpointWanted = false;
    g_State.checkpointForced = false;
    g_State.framesSinceCheckpoint = 0;
    g_State.checkpointMsgFrames = PD_MESSAGE_FRAMES;
    PD_LOG("checkpoint taken (score %u, power %d, bombs %d)", g_GameManager.globals->score,
           (i32)g_GameManager.globals->currentPower, (i32)g_GameManager.globals->bombsRemaining);
}

static void RestoreCheckpoint()
{
    ZunGlobals *globals = g_GameManager.globals;
    GuiImpl *guiImpl = g_Gui.impl;
    i32 i;

    g_Snapshot.gameManager.Load(&g_GameManager);
    g_GameManager.globals = globals;
    g_Snapshot.globals.Load(g_GameManager.globals);
    g_Snapshot.player.Load(&g_Player);
    g_Snapshot.bulletManager.Load(&g_BulletManager);
    g_Snapshot.enemyManager.Load(&g_EnemyManager);
    g_Snapshot.itemManager.Load(&g_ItemManager);
    g_Snapshot.effectManager.Load(&g_EffectManager);
    g_Snapshot.stage.Load(&g_Stage);
    g_Snapshot.gui.Load(&g_Gui);
    g_Gui.impl = guiImpl;
    g_Snapshot.guiImpl.Load(g_Gui.impl);
    g_Snapshot.eclManager.Load(&g_EclManager);
    g_Snapshot.eclGlobalVars.Load(&g_GlobalEclVars);
    g_Snapshot.rng.Load(&g_Rng);
    if (g_Snapshot.hasReplayManager && g_ReplayManager != NULL)
    {
        g_Snapshot.replayManager.Load(g_ReplayManager);
    }

    if (g_Stage.quadCount > 0)
    {
        std::memcpy(g_Stage.quadVms, (const void *)g_Snapshot.stageQuadVms.data(), sizeof(AnmVm) * g_Stage.quadCount);
    }
    for (i = 0; i < g_Stage.objectsCount; i++)
    {
        g_Stage.objects[i]->flags = g_Snapshot.stageObjectFlags[i];
    }

    // The boss theme starts during the pre-boss dialogue, so rewinding to
    // before the boss has to bring the stage theme back.
    if (g_Snapshot.songIdx != g_State.songIdx && g_Snapshot.songIdx >= 0)
    {
        PD_LOG("restoring music track %d", g_Snapshot.songIdx);
        if (g_Supervisor.PlayLoadedAudio(g_Snapshot.songIdx) != ZUN_SUCCESS)
        {
            g_Supervisor.PlayAudio(g_Stage.stdData->bgmPaths[g_Snapshot.songIdx]);
        }
    }
    g_State.songIdx = g_Snapshot.songIdx;

    g_Player.playerState = PLAYER_STATE_INVULNERABLE;
    if (g_Player.invulnerabilityTimer.GetCurrent() < PD_RESTORE_INVULNERABILITY)
    {
        g_Player.invulnerabilityTimer = PD_RESTORE_INVULNERABILITY;
    }

    // Redraw the whole side panel.
    g_Gui.showLives = 2;
    g_Gui.showBombs = 2;
    g_Gui.showPower = 2;
    g_Gui.showGraze = 2;
    g_Gui.showPoint = 2;

    g_State.retries++;
    PD_LOG("rewound to checkpoint, retry %d (score %u, power %d, bombs %d, lives %d)", g_State.retries,
           g_GameManager.globals->score, (i32)g_GameManager.globals->currentPower,
           (i32)g_GameManager.globals->bombsRemaining, (i32)g_GameManager.globals->livesRemaining);
    g_State.retryMsgFrames = PD_MESSAGE_FRAMES;
    g_State.checkpointMsgFrames = 0;
    g_State.checkpointWanted = false;
    g_State.checkpointForced = false;
    g_State.framesSinceCheckpoint = 0;
}

void OnFrameStart()
{
    if (!g_State.runActive)
    {
        return;
    }

    if (g_State.pendingRestore)
    {
        g_State.pendingRestore = false;
        if (g_Snapshot.valid)
        {
            RestoreCheckpoint();
            return;
        }
    }

    g_State.framesSinceCheckpoint++;
    if (g_State.framesSinceCheckpoint >= PD_PERIODIC_INTERVAL)
    {
        g_State.checkpointWanted = true;
    }

    if (g_State.checkpointForced)
    {
        // The very first checkpoint of a stage is taken unconditionally.
        if (!g_Snapshot.valid || IsSafeToCheckpoint(true))
        {
            TakeCheckpoint();
            return;
        }
        if (++g_State.forcedFrames >= PD_FORCED_DEADLINE)
        {
            g_State.checkpointForced = false;
        }
    }
    if (g_State.checkpointWanted && IsSafeToCheckpoint(false))
    {
        TakeCheckpoint();
    }
}

void RequestCheckpoint()
{
    if (!g_State.runActive)
    {
        return;
    }
    g_State.checkpointWanted = true;
    g_State.checkpointForced = true;
    g_State.forcedFrames = 0;
    g_State.autotestBossSeen = true;
    PD_LOG("boss attack started, checkpoint requested");
}

static i32 MenuItemCount()
{
    return g_State.menu == MENU_DEATH ? (i32)DEATH_MENU_COUNT : (i32)PAUSE_MENU_COUNT;
}

static void OpenMenu(MenuKind kind)
{
    g_State.menu = kind;
    g_State.menuCursor = 0;
    g_State.menuFrames = 0;
}

bool IsMenuOpen()
{
    return g_State.menu != MENU_NONE;
}

bool OnDeathFinished()
{
    if (!g_State.runActive || !g_Snapshot.valid)
    {
        return false;
    }
    OpenMenu(MENU_DEATH);
    PD_LOG("player died, lives %d", (i32)g_GameManager.globals->livesRemaining);
    return true;
}

// Stage 6 has a silent intro, which the original pause does not pause either.
static bool HasPausableAudio()
{
    return g_GameManager.currentStage != PD_STAGE6 || g_Gui.frameCounter >= 300;
}

bool OpenPauseMenu()
{
    if (!g_State.runActive)
    {
        return false;
    }
    if (g_State.menu == MENU_NONE)
    {
        OpenMenu(MENU_PAUSE);
        g_State.audioPaused = HasPausableAudio();
        if (g_State.audioPaused)
        {
            g_SoundPlayer.PushCommand(AUDIO_PAUSE, 0, "Pause");
        }
        g_SoundPlayer.PlaySoundByIdx(SOUND_37, 0);
        g_Supervisor.UpdateTime();
    }
    return true;
}

static void CloseMenu()
{
    if (g_State.menu == MENU_PAUSE && g_State.audioPaused)
    {
        g_SoundPlayer.PushCommand(AUDIO_UNPAUSE, 0, (char *)"UnPause");
    }
    g_State.audioPaused = false;
    g_State.menu = MENU_NONE;
    // Do not let the time spent in the menu count as lag.
    g_Supervisor.currentTime = SDL_GetTicks();
}

static void SelectDeathMenuItem(i32 item)
{
    CloseMenu();
    if (item == DEATH_MENU_CONTINUE)
    {
        g_State.pendingRestore = true;
        return;
    }
    // Giving up ends the run the same way declining a continue does.
    PD_LOG("gave up (score %u)", g_GameManager.globals->score);
    g_GameManager.globals->guiScore = g_GameManager.globals->score;
    g_Supervisor.curState = PD_SUPERVISOR_STATE_RESULTSCREEN_FROM_GAME;
}

static void SelectPauseMenuItem(i32 item)
{
    if (item == PAUSE_MENU_SAVE_AND_QUIT)
    {
        if (!WriteSave())
        {
            PD_LOG("could not write %s", SavePath().c_str());
            g_SoundPlayer.PlaySoundByIdx(SOUND_BACK, 0);
            return;
        }
        PD_LOG("saved run (stage %d, score %u) and quit", g_State.save.stage, g_State.save.score);
        g_State.autotestSaved = g_State.autotest == AUTOTEST_SAVE_QUIT;
    }
    if (item == PAUSE_MENU_RESUME)
    {
        CloseMenu();
        return;
    }
    // Leaving for the title screen: the music stops there anyway.
    g_State.audioPaused = false;
    CloseMenu();
    g_Supervisor.curState = PD_SUPERVISOR_STATE_MAINMENU;
}

bool UpdateInGameMenu()
{
    i32 count;

    if (g_State.menu == MENU_NONE)
    {
        return false;
    }
    // Gameplay is frozen, which is not slowdown, and touch input switches to
    // menu gestures (swipe to move, tap to pick, two fingers to go back).
    g_GameManager.notInMenu = 0;
    count = MenuItemCount();
    if (g_State.menuFrames++ < PD_MENU_INPUT_DELAY)
    {
        return true;
    }
    if (WAS_PRESSED_RAW(TH_BUTTON_UP))
    {
        g_State.menuCursor = (g_State.menuCursor + count - 1) % count;
        g_SoundPlayer.PlaySoundByIdx(SOUND_MOVE_MENU, 0);
    }
    if (WAS_PRESSED_RAW(TH_BUTTON_DOWN))
    {
        g_State.menuCursor = (g_State.menuCursor + 1) % count;
        g_SoundPlayer.PlaySoundByIdx(SOUND_MOVE_MENU, 0);
    }
    if (g_State.menu == MENU_PAUSE && WAS_PRESSED_RAW(TH_BUTTON_RETURNMENU))
    {
        g_SoundPlayer.PlaySoundByIdx(SOUND_BACK, 0);
        SelectPauseMenuItem(PAUSE_MENU_RESUME);
    }
    else if (WAS_PRESSED_RAW(TH_BUTTON_SELECTMENU))
    {
        g_SoundPlayer.PlaySoundByIdx(SOUND_SELECT, 0);
        if (g_State.menu == MENU_DEATH)
        {
            SelectDeathMenuItem(g_State.menuCursor);
        }
        else
        {
            SelectPauseMenuItem(g_State.menuCursor);
        }
    }
    return true;
}

void OnMusicChanged(i32 songIdx)
{
    PD_LOG("music changed to track %d", songIdx);
    g_State.songIdx = songIdx;
}

// Presses one menu button at fixed times once the menu accepts input: DOWN
// (when wanted) and then Z.
static u16 AutotestMenuInput(bool moveDown)
{
    if (moveDown && g_State.menuFrames == PD_MENU_INPUT_DELAY + 5)
    {
        return TH_BUTTON_DOWN;
    }
    return g_State.menuFrames == PD_MENU_INPUT_DELAY + 15 ? TH_BUTTON_SHOOT : 0;
}

u16 AutotestInput()
{
    if (g_State.autotest == AUTOTEST_OFF)
    {
        return 0;
    }
    g_State.autotestFrames++;
    if (g_Supervisor.curState == PD_SUPERVISOR_STATE_GAMEMANAGER)
    {
        if (g_State.menu != MENU_NONE && g_State.autotest == AUTOTEST_HOLD_MENU)
        {
            return 0;
        }
        if (g_State.menu == MENU_DEATH)
        {
            return AutotestMenuInput(g_State.autotest == AUTOTEST_GIVE_UP);
        }
        if (g_State.menu == MENU_PAUSE)
        {
            return AutotestMenuInput(true);
        }
        if (g_State.autotest == AUTOTEST_SAVE_QUIT && !g_State.autotestSaved && !g_State.autotestResumed &&
            g_GameManager.currentStage == 2 && g_GameManager.framesThisStage == 600)
        {
            return TH_BUTTON_MENU;
        }
        // The autopilot cannot dodge, so it is kept invulnerable until the
        // first boss attack starts (for =savequit, always).
        if ((!g_State.autotestBossSeen || g_State.autotest == AUTOTEST_SAVE_QUIT) &&
            g_Player.playerState == PLAYER_STATE_ALIVE)
        {
            g_Player.playerState = PLAYER_STATE_INVULNERABLE;
            g_Player.invulnerabilityTimer = 60;
        }
        // Sway left and right, tapping Z so that dialogue advances too.
        return (g_State.autotestFrames % 10 != 0 ? TH_BUTTON_SHOOT : 0) |
               ((g_State.autotestFrames / 50) % 2 ? TH_BUTTON_LEFT : TH_BUTTON_RIGHT);
    }
    return g_State.autotestFrames % 20 == 0 ? TH_BUTTON_SHOOT : 0;
}

static void DrawMenu()
{
    static const char *deathItems[DEATH_MENU_COUNT] = {"Continue from checkpoint", "Give up"};
    static const char *pauseItems[PAUSE_MENU_COUNT] = {"Resume", "Save and quit", "Quit without saving"};
    const char **items;
    i32 count;
    i32 i;
    ZunVec3 pos;

    if (g_State.menu == MENU_NONE)
    {
        return;
    }
    items = g_State.menu == MENU_DEATH ? deathItems : pauseItems;
    count = MenuItemCount();

    pos = ZunVec3(56.0f, 176.0f, 0.0f);
    g_AsciiManager.SetColor(PD_COLOR_LIGHT_RED);
    g_AsciiManager.AddString(&pos, g_State.menu == MENU_DEATH ? "Hit!" : "Paused");
    for (i = 0; i < count; i++)
    {
        pos = ZunVec3(56.0f, 216.0f + 24.0f * i, 0.0f);
        g_AsciiManager.SetColor(i == g_State.menuCursor ? PD_COLOR_WHITE : PD_COLOR_GREY);
        AsciiManager::AddFormatText(&g_AsciiManager, &pos, "%c %s", i == g_State.menuCursor ? '>' : ' ', items[i]);
    }
    if (g_State.menu == MENU_PAUSE && g_State.menuCursor == PAUSE_MENU_SAVE_AND_QUIT)
    {
        pos = ZunVec3(56.0f, 216.0f + 24.0f * count + 16.0f, 0.0f);
        g_AsciiManager.SetColor(PD_COLOR_LIGHT_CYAN);
        AsciiManager::AddFormatText(&g_AsciiManager, &pos, "Back to Stage %d start",
                                    g_State.stageStart.stage);
    }
}

void DrawHud()
{
    ZunVec3 pos;

    if (!g_State.runActive)
    {
        return;
    }

    g_AsciiManager.SetColor(PD_COLOR_LIGHT_RED);
    pos = ZunVec3(432.0f, 250.0f, 0.0f);
    AsciiManager::AddFormatText(&g_AsciiManager, &pos, "Pointdevice");
    pos = ZunVec3(432.0f, 270.0f, 0.0f);
    AsciiManager::AddFormatText(&g_AsciiManager, &pos, "Retry   %d", g_State.retries);

    if (g_State.retryMsgFrames > 0)
    {
        g_State.retryMsgFrames--;
        pos = ZunVec3(40.0f, 440.0f, 0.0f);
        AsciiManager::AddFormatText(&g_AsciiManager, &pos, "Back to checkpoint");
    }
    else if (g_State.checkpointMsgFrames > 0)
    {
        g_State.checkpointMsgFrames--;
        g_AsciiManager.SetColor(PD_COLOR_LIGHT_CYAN);
        pos = ZunVec3(40.0f, 440.0f, 0.0f);
        AsciiManager::AddFormatText(&g_AsciiManager, &pos, "Checkpoint");
    }
    DrawMenu();
    g_AsciiManager.SetColor(PD_COLOR_WHITE);
}

}; // namespace PointDevice
