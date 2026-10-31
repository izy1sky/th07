#include "PointDevice.hpp"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "AnmManager.hpp"
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
#include "PointDeviceArt.hpp"
#include "ReplayManager.hpp"
#include "Rng.hpp"
#include "ScreenEffect.hpp"
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

// Leaving a menu. Resume slides the menu out. Continue also sweeps strips across
// the play area, rewinds while it is covered, then sweeps them off again.
#define PD_MENU_EXIT_FRAMES 10
#define PD_SHUTTER_STRIPS 8
#define PD_SHUTTER_STAGGER 2.0f
#define PD_SHUTTER_CLOSE_FRAMES 24
#define PD_SHUTTER_HOLD_FRAMES 24
#define PD_SHUTTER_OPEN_FRAMES 24
#define PD_SHUTTER_COLOR 0xff0e0b16
#define PD_SHUTTER_EDGE_COLOR 0x00e8dcff

// One saved run per difficulty, character and shot type.
#define PD_SAVE_FILE_FORMAT "pointdevice-%d-%d-%d.sav"
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
    bool inBossAttack;
    // Where the music was, so a rewind takes the music back too.
    bool hasBgmCursor;
    u64 bgmCursor;
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

enum MenuExit
{
    MENU_EXIT_NONE,
    MENU_EXIT_RESUME,
    MENU_EXIT_REWIND,
};

enum ModeSelectStep
{
    MODE_SELECT_CLOSED,
    MODE_SELECT_CHOOSE_MODE,
    MODE_SELECT_ASK_RESUME,
};

enum ModeItem
{
    MODE_ITEM_POINTDEVICE,
    MODE_ITEM_LEGACY,
    MODE_ITEM_COUNT,
};

enum ResumeItem
{
    RESUME_ITEM_RESUME,
    RESUME_ITEM_NEW_RUN,
    RESUME_ITEM_COUNT,
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
    f32 menuHighlight[PAUSE_MENU_COUNT];
    // Set once an item is picked: the menu stays up while it animates away.
    MenuExit menuExit;
    i32 menuExitFrames;
    // The music holds still while a menu is open.
    bool audioPaused;
    ModeSelectStep modeSelect;
    i32 modeSelectCursor;
    i32 modeSelectFrames;
    // Frames since the mode choice opened; unlike modeSelectFrames it does not
    // restart when the resume prompt replaces the mode choice.
    i32 modeSelectOpenFrames;
    // 0..1 per item, eased toward the cursor so a selection fades over.
    f32 modeHighlight[MODE_ITEM_COUNT];
    // The saved run for the difficulty, character and shot type being started.
    bool hasSave;
    SaveData save;
    bool resumeChosen;
    bool resuming;
    // The stats the current stage started with, written out by "Save and quit".
    SaveData stageStart;
    bool runActive;
    bool checkpointWanted;
    bool checkpointForced;
    i32 forcedFrames;
    i32 framesSinceCheckpoint;
    // Between a boss attack's start and end the periodic checkpoints stop, so
    // dying always sends you back to the start of the attack.
    bool inBossAttack;
    i32 songIdx;
    i32 retries;
    i32 checkpointMsgFrames;
    i32 retryMsgFrames;
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

// On the web build the pref path is an IndexedDB mount, so saved runs
// survive a page reload.
static std::string SavePath(i32 difficulty, i32 character, i32 shotType)
{
    char name[64];

    std::snprintf(name, sizeof(name), PD_SAVE_FILE_FORMAT, difficulty, character, shotType);
    return FileSystem::GetPrefPath(name);
}

// Looks up the saved run for this difficulty, character and shot type.
static void LoadSave(i32 difficulty, i32 character, i32 shotType)
{
    FILE *file;
    SaveData save;

    g_State.hasSave = false;
    file = std::fopen(SavePath(difficulty, character, shotType).c_str(), "rb");
    if (file == NULL)
    {
        return;
    }
    if (std::fread(&save, sizeof(save), 1, file) == 1 && save.magic == PD_SAVE_MAGIC &&
        save.version == PD_SAVE_VERSION && save.difficulty == difficulty && save.character == character &&
        save.shotType == shotType && save.stage >= 1 && save.stage <= 8)
    {
        g_State.save = save;
        g_State.hasSave = true;
    }
    std::fclose(file);
}

static bool WriteSave()
{
    const SaveData *save = &g_State.stageStart;
    FILE *file;
    bool ok;

    file = std::fopen(SavePath(save->difficulty, save->character, save->shotType).c_str(), "wb");
    if (file == NULL)
    {
        return false;
    }
    ok = std::fwrite(save, sizeof(*save), 1, file) == 1;
    return std::fclose(file) == 0 && ok;
}

// A saved run is used up when it is resumed.
static void DeleteSave(const SaveData *save)
{
    std::remove(SavePath(save->difficulty, save->character, save->shotType).c_str());
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
}

bool IsEnabled()
{
    return g_State.enabled;
}

bool IsRunActive()
{
    return g_State.runActive;
}


// Selection highlight: 1 for the item under the cursor, 0 for the others,
// eased over a few frames so a change of selection cross-fades.
static void SnapHighlights(f32 *highlights, i32 count, i32 cursor)
{
    i32 i;

    for (i = 0; i < count; i++)
    {
        highlights[i] = i == cursor ? 1.0f : 0.0f;
    }
}

static void StepHighlights(f32 *highlights, i32 count, i32 cursor)
{
    i32 i;

    for (i = 0; i < count; i++)
    {
        highlights[i] += ((i == cursor ? 1.0f : 0.0f) - highlights[i]) * 0.3f;
    }
}

void OpenModeSelect()
{
    g_State.modeSelect = MODE_SELECT_CHOOSE_MODE;
    g_State.modeSelectCursor = g_State.enabled ? MODE_ITEM_POINTDEVICE : MODE_ITEM_LEGACY;
    g_State.modeSelectFrames = 0;
    g_State.modeSelectOpenFrames = 0;
    SnapHighlights(g_State.modeHighlight, MODE_ITEM_COUNT, g_State.modeSelectCursor);
    g_State.resumeChosen = false;
    g_SoundPlayer.PlaySoundByIdx(SOUND_SELECT, 0);
}

bool IsModeSelectOpen()
{
    return g_State.modeSelect != MODE_SELECT_CLOSED;
}

static void MoveModeSelectCursor(i32 count)
{
    if (WAS_PRESSED_RAW(TH_BUTTON_UP))
    {
        g_State.modeSelectCursor = (g_State.modeSelectCursor + count - 1) % count;
        g_SoundPlayer.PlaySoundByIdx(SOUND_MOVE_MENU, 0);
    }
    if (WAS_PRESSED_RAW(TH_BUTTON_DOWN))
    {
        g_State.modeSelectCursor = (g_State.modeSelectCursor + 1) % count;
        g_SoundPlayer.PlaySoundByIdx(SOUND_MOVE_MENU, 0);
    }
}

ModeSelectResult UpdateModeSelect()
{
    if (g_State.modeSelect == MODE_SELECT_CLOSED)
    {
        return MODE_SELECT_CANCELLED;
    }
    g_State.modeSelectOpenFrames++;
    StepHighlights(g_State.modeHighlight, MODE_ITEM_COUNT, g_State.modeSelectCursor);
    if (g_State.modeSelectFrames++ < PD_MENU_INPUT_DELAY)
    {
        return MODE_SELECT_OPEN;
    }

    if (g_State.modeSelect == MODE_SELECT_CHOOSE_MODE)
    {
        MoveModeSelectCursor(MODE_ITEM_COUNT);
        if (WAS_PRESSED_RAW(TH_BUTTON_RETURNMENU))
        {
            g_SoundPlayer.PlaySoundByIdx(SOUND_BACK, 0);
            g_State.modeSelect = MODE_SELECT_CLOSED;
            return MODE_SELECT_CANCELLED;
        }
        if (WAS_PRESSED_RAW(TH_BUTTON_SELECTMENU))
        {
            g_SoundPlayer.PlaySoundByIdx(SOUND_SELECT, 0);
            g_State.enabled = g_State.modeSelectCursor == MODE_ITEM_POINTDEVICE;
            if (g_State.enabled && !g_GameManager.practice)
            {
                // The run's difficulty is only set once the game starts.
                LoadSave(g_Supervisor.cfg.defaultDifficulty, g_GameManager.character, g_GameManager.shotType);
                if (g_State.hasSave)
                {
                    g_State.modeSelect = MODE_SELECT_ASK_RESUME;
                    g_State.modeSelectCursor = RESUME_ITEM_RESUME;
                    g_State.modeSelectFrames = 0;
                    SnapHighlights(g_State.modeHighlight, RESUME_ITEM_COUNT, g_State.modeSelectCursor);
                    return MODE_SELECT_OPEN;
                }
            }
            g_State.modeSelect = MODE_SELECT_CLOSED;
            return MODE_SELECT_START;
        }
        return MODE_SELECT_OPEN;
    }

    MoveModeSelectCursor(RESUME_ITEM_COUNT);
    if (WAS_PRESSED_RAW(TH_BUTTON_RETURNMENU))
    {
        g_SoundPlayer.PlaySoundByIdx(SOUND_BACK, 0);
        g_State.modeSelect = MODE_SELECT_CHOOSE_MODE;
        g_State.modeSelectCursor = MODE_ITEM_POINTDEVICE;
        g_State.modeSelectFrames = 0;
        SnapHighlights(g_State.modeHighlight, MODE_ITEM_COUNT, g_State.modeSelectCursor);
        return MODE_SELECT_OPEN;
    }
    if (WAS_PRESSED_RAW(TH_BUTTON_SELECTMENU))
    {
        g_SoundPlayer.PlaySoundByIdx(SOUND_SELECT, 0);
        g_State.resumeChosen = g_State.modeSelectCursor == RESUME_ITEM_RESUME;
        g_State.modeSelect = MODE_SELECT_CLOSED;
        return MODE_SELECT_START;
    }
    return MODE_SELECT_OPEN;
}

void OnGameLaunch()
{
    const SaveData *save = &g_State.save;

    if (!g_State.resumeChosen)
    {
        return;
    }
    g_State.resumeChosen = false;
    // GameManager::AddedCallback moves on to the next stage.
    g_GameManager.currentStage = save->stage - 1;
    g_State.resuming = true;
    g_State.autotestResumed = g_State.autotest == AUTOTEST_SAVE_QUIT;
    DeleteSave(save);
}

void OnReturnToMenu()
{
    g_State.modeSelect = MODE_SELECT_CLOSED;
    g_State.runActive = false;
    g_State.menu = MENU_NONE;
    g_State.menuExit = MENU_EXIT_NONE;
    g_State.audioPaused = false;
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
    g_GameManager.totalPlayTime = save->playTimeAll;
    g_GameManager.powerItemCountForScore = save->powerItemCountForScore;
    g_State.retries = save->retries;
    g_Gui.lifeDisplayUpdateFrames = 2;
    g_Gui.bombDisplayUpdateFrames = 2;
    g_Gui.powerDisplayUpdateFrames = 2;
    g_Gui.grazeDisplayUpdateFrames = 2;
    g_Gui.pointDisplayUpdateFrames = 2;
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
        if (g_State.runActive)
        {
            // Lives mean nothing here: no stars on the side panel and no
            // life bonus at stage clear.
            g_GameManager.SetLivesRemaining(0);
            g_Gui.lifeDisplayUpdateFrames = 2;
        }
    }
    g_State.menu = MENU_NONE;
    g_State.menuExit = MENU_EXIT_NONE;
    // Snapshots point into stage data, so they never survive a stage change.
    g_Snapshot.valid = false;
    g_State.checkpointWanted = true;
    g_State.checkpointForced = true;
    g_State.forcedFrames = 0;
    g_State.framesSinceCheckpoint = 0;
    g_State.inBossAttack = false;
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
    const ZunVec3 *playerPos = &g_Player.pos;
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
            if (g_BulletManager.lasers[i].isInUse)
            {
                return false;
            }
        }
    }
    for (i = 0; i < (i32)ARRAY_SIZE(g_EnemyManager.enemies); i++)
    {
        if (g_EnemyManager.enemies[i].active &&
            DistanceSquared(&g_EnemyManager.enemies[i].pos, playerPos) <
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
    save->playTimeAll = g_GameManager.totalPlayTime;
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
    g_Snapshot.inBossAttack = g_State.inBossAttack;
    g_Snapshot.hasBgmCursor = g_SoundPlayer.GetBgmCursor(&g_Snapshot.bgmCursor);
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
    const i32 songIdxBefore = g_State.songIdx;
    if (g_Snapshot.songIdx != g_State.songIdx && g_Snapshot.songIdx >= 0)
    {
        PD_LOG("restoring music track %d", g_Snapshot.songIdx);
        if (g_Supervisor.PlayLoadedAudio(g_Snapshot.songIdx) != ZUN_SUCCESS)
        {
            g_Supervisor.PlayAudio(g_Stage.stdData->bgmPaths[g_Snapshot.songIdx]);
        }
    }
    g_State.songIdx = g_Snapshot.songIdx;
    g_State.inBossAttack = g_Snapshot.inBossAttack;
    if (g_Snapshot.hasBgmCursor)
    {
        // A track change above is only queued; start it before seeking.
        while (g_SoundPlayer.ProcessQueues())
            ;
        PD_LOG("music back to frame %llu", (unsigned long long)g_Snapshot.bgmCursor);
        g_SoundPlayer.SeekBgm(g_Snapshot.bgmCursor);
        // A new track starts playing; it stays quiet until the rewind is over.
        if (g_State.audioPaused && g_Snapshot.songIdx != songIdxBefore)
        {
            g_SoundPlayer.PushCommand(AUDIO_PAUSE, 0, "Pause");
            while (g_SoundPlayer.ProcessQueues())
                ;
        }
    }

    g_Player.playerState = PLAYER_STATE_INVULNERABLE;
    // While alive the timer counts up, so the restored value can be huge; the
    // invulnerability after a rewind is always exactly this long.
    g_Player.invulnerabilityTimer = PD_RESTORE_INVULNERABILITY;

    // Redraw the whole side panel.
    g_Gui.lifeDisplayUpdateFrames = 2;
    g_Gui.bombDisplayUpdateFrames = 2;
    g_Gui.powerDisplayUpdateFrames = 2;
    g_Gui.grazeDisplayUpdateFrames = 2;
    g_Gui.pointDisplayUpdateFrames = 2;

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

    if (!g_State.inBossAttack)
    {
        g_State.framesSinceCheckpoint++;
        if (g_State.framesSinceCheckpoint >= PD_PERIODIC_INTERVAL)
        {
            g_State.checkpointWanted = true;
        }
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

static void ForceCheckpoint()
{
    g_State.checkpointWanted = true;
    g_State.checkpointForced = true;
    g_State.forcedFrames = 0;
}

void RequestCheckpoint()
{
    if (!g_State.runActive)
    {
        return;
    }
    ForceCheckpoint();
    g_State.inBossAttack = true;
    g_State.autotestBossSeen = true;
    PD_LOG("boss attack started, checkpoint requested");
}

// So that dying after a midboss's last attack doesn't send you back into it.
void OnBossAttackEnded()
{
    if (!g_State.runActive)
    {
        return;
    }
    ForceCheckpoint();
    g_State.inBossAttack = false;
    PD_LOG("boss attack ended, checkpoint requested");
}

// Practice runs cannot be saved, so their pause menu has no "Save and quit".
static i32 MenuItemCount()
{
    if (g_State.menu == MENU_DEATH)
    {
        return DEATH_MENU_COUNT;
    }
    return g_GameManager.practice ? PAUSE_MENU_COUNT - 1 : PAUSE_MENU_COUNT;
}

static i32 PauseItemAt(i32 cursor)
{
    if (g_GameManager.practice && cursor >= PAUSE_MENU_SAVE_AND_QUIT)
    {
        return cursor + 1;
    }
    return cursor;
}

// Stage 6 has a silent intro, which the original pause does not pause either.
static bool HasPausableAudio()
{
    return g_GameManager.currentStage != PD_STAGE6 || g_Gui.frameCounter >= 300;
}

static void OpenMenu(MenuKind kind)
{
    g_State.menu = kind;
    g_State.menuCursor = 0;
    g_State.menuFrames = 0;
    g_State.menuExit = MENU_EXIT_NONE;
    SnapHighlights(g_State.menuHighlight, PAUSE_MENU_COUNT, 0);
    g_State.audioPaused = HasPausableAudio();
    if (g_State.audioPaused)
    {
        g_SoundPlayer.PushCommand(AUDIO_PAUSE, 0, "Pause");
    }
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

bool OpenPauseMenu()
{
    if (!g_State.runActive)
    {
        return false;
    }
    if (g_State.menu == MENU_NONE)
    {
        OpenMenu(MENU_PAUSE);
        g_SoundPlayer.PlaySoundByIdx(SOUND_PAUSED, 0);
        g_Supervisor.UpdateTime();
    }
    return true;
}

static void ResumeAudio()
{
    if (g_State.audioPaused)
    {
        g_SoundPlayer.PushCommand(AUDIO_UNPAUSE, 0, (char *)"UnPause");
    }
    g_State.audioPaused = false;
}

static void CloseMenu()
{
    ResumeAudio();
    g_State.menu = MENU_NONE;
    g_State.menuExit = MENU_EXIT_NONE;
    // Do not let the time spent in the menu count as lag.
    g_Supervisor.currentTime = SDL_GetTicks();
}

static void LeaveMenu(MenuExit exit)
{
    g_State.menuExit = exit;
    g_State.menuExitFrames = 0;
}

static i32 MenuExitLength()
{
    return g_State.menuExit == MENU_EXIT_REWIND
               ? PD_SHUTTER_CLOSE_FRAMES + PD_SHUTTER_HOLD_FRAMES + PD_SHUTTER_OPEN_FRAMES
               : PD_MENU_EXIT_FRAMES;
}

// The rewind happens once the strips cover the play area, and the music comes
// back as they start to sweep off.
static void StepMenuExit()
{
    const i32 frame = g_State.menuExitFrames++;

    if (g_State.menuExit == MENU_EXIT_REWIND)
    {
        if (frame == PD_SHUTTER_CLOSE_FRAMES)
        {
            RestoreCheckpoint();
        }
        if (frame == PD_SHUTTER_CLOSE_FRAMES + PD_SHUTTER_HOLD_FRAMES)
        {
            ResumeAudio();
        }
    }
    if (frame + 1 >= MenuExitLength())
    {
        CloseMenu();
    }
}

static void SelectDeathMenuItem(i32 item)
{
    if (item == DEATH_MENU_CONTINUE)
    {
        LeaveMenu(MENU_EXIT_REWIND);
        return;
    }
    CloseMenu();
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
            PD_LOG("could not write the saved run");
            g_SoundPlayer.PlaySoundByIdx(SOUND_BACK, 0);
            return;
        }
        PD_LOG("saved run (stage %d, score %u) and quit", g_State.stageStart.stage, g_State.stageStart.score);
        g_State.autotestSaved = g_State.autotest == AUTOTEST_SAVE_QUIT;
    }
    if (item == PAUSE_MENU_RESUME)
    {
        LeaveMenu(MENU_EXIT_RESUME);
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
    if (g_State.menuExit != MENU_EXIT_NONE)
    {
        StepMenuExit();
        return true;
    }
    count = MenuItemCount();
    StepHighlights(g_State.menuHighlight, count, g_State.menuCursor);
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
            SelectPauseMenuItem(PauseItemAt(g_State.menuCursor));
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
            // Only the save test saves; a pause from losing focus just resumes.
            return AutotestMenuInput(g_State.autotest == AUTOTEST_SAVE_QUIT && !g_State.autotestResumed);
        }
        if (g_State.autotest == AUTOTEST_SAVE_QUIT && !g_State.autotestSaved && !g_State.autotestResumed &&
            g_GameManager.currentStage == 2 && g_GameManager.framesThisStage == 600)
        {
            return TH_BUTTON_MENU;
        }
        // The autopilot cannot dodge, so it is kept invulnerable until the
        // first boss attack starts (for =savequit, always).
        if ((!g_State.autotestBossSeen && g_Player.playerState == PLAYER_STATE_ALIVE) ||
            (g_State.autotest == AUTOTEST_SAVE_QUIT &&
             (g_Player.playerState == PLAYER_STATE_ALIVE || g_Player.playerState == PLAYER_STATE_INVULNERABLE)))
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

// ---------------------------------------------------------------------------
// Menu art. The menus are drawn from one atlas (PointDeviceArt.hpp) made to
// match the game's own screens: the Choose Level screen for the mode choice and
// resume prompt, and the pause menu's look for the in-game menus.

static GfxTextureHandle g_ArtTexture;
static bool g_ArtLoaded;
static bool g_ArtFailed;

static bool LoadArt()
{
    SDL_Surface *png;
    SDL_Surface *rgba;

    if (g_ArtLoaded || g_ArtFailed)
    {
        return g_ArtLoaded;
    }
    png = IMG_Load_IO(SDL_IOFromConstMem(PointDeviceArt::kAtlasPng, sizeof(PointDeviceArt::kAtlasPng)), true);
    rgba = png != NULL ? SDL_ConvertSurface(png, SDL_PIXELFORMAT_ABGR8888) : NULL;
    if (png != NULL)
    {
        SDL_DestroySurface(png);
    }
    if (rgba == NULL)
    {
        PD_LOG("could not load the menu art: %s", SDL_GetError());
        g_ArtFailed = true;
        return false;
    }
    g_AnmManager->Flush();
    g_ArtTexture = g_Supervisor.gfxDevice->CreateTexture();
    g_Supervisor.gfxDevice->BindTexture(g_ArtTexture);
    g_Supervisor.gfxDevice->SetTextureFilter();
    g_Supervisor.gfxDevice->SetTextureImage(rgba->w, rgba->h, PIXEL_RGBA, PIXEL_UNSIGNED_BYTE, rgba->pixels);
    SDL_DestroySurface(rgba);
    // AnmManager caches the bound texture; make it bind its own again.
    g_AnmManager->SetTexture(0);
    g_ArtLoaded = true;
    return true;
}

static const PointDeviceArt::Sprite *FindArt(const char *name)
{
    i32 i;

    for (i = 0; i < (i32)ARRAY_SIZE(PointDeviceArt::kSprites); i++)
    {
        if (std::strcmp(PointDeviceArt::kSprites[i].name, name) == 0)
        {
            return &PointDeviceArt::kSprites[i];
        }
    }
    return NULL;
}

static f32 ArtWidth(const char *name)
{
    const PointDeviceArt::Sprite *sprite = FindArt(name);
    return sprite != NULL ? sprite->contentWidth : 0.0f;
}

// Draws a sprite with its content's top-left corner at (x, y) in 640x480 screen pixels.
static void DrawArt(const char *name, f32 x, f32 y, f32 alpha = 1.0f)
{
    const PointDeviceArt::Sprite *sprite = FindArt(name);
    VertexTex1DiffuseXyzrhw vertices[4];
    f32 left, top, right, bottom;
    f32 u0, v0, u1, v1;
    i32 i;

    if (sprite == NULL || !LoadArt())
    {
        return;
    }
    left = x - sprite->originX;
    top = y - sprite->originY;
    right = left + sprite->width;
    bottom = top + sprite->height;
    u0 = sprite->x / (f32)PointDeviceArt::kAtlasWidth;
    v0 = sprite->y / (f32)PointDeviceArt::kAtlasHeight;
    u1 = (sprite->x + sprite->w) / (f32)PointDeviceArt::kAtlasWidth;
    v1 = (sprite->y + sprite->h) / (f32)PointDeviceArt::kAtlasHeight;
    vertices[0].pos = ZunVec3(left, top, 0.0f);
    vertices[1].pos = ZunVec3(right, top, 0.0f);
    vertices[2].pos = ZunVec3(left, bottom, 0.0f);
    vertices[3].pos = ZunVec3(right, bottom, 0.0f);
    vertices[0].textureUV.x = u0;
    vertices[0].textureUV.y = v0;
    vertices[1].textureUV.x = u1;
    vertices[1].textureUV.y = v0;
    vertices[2].textureUV.x = u0;
    vertices[2].textureUV.y = v1;
    vertices[3].textureUV.x = u1;
    vertices[3].textureUV.y = v1;
    for (i = 0; i < 4; i++)
    {
        vertices[i].w = 1.0f;
        vertices[i].diffuse.color = ((u32)(alpha * 255.0f) << 24) | 0xffffff;
    }

    g_Supervisor.gfxDevice->BindTexture(g_ArtTexture);
    // Modulate the texture by the vertex colour, which carries the fade.
    g_Supervisor.gfxDevice->SetTextureArg(TEX_ARG_DIFFUSE);
    if (!g_Supervisor.cfg.disableZBuffer)
    {
        g_Supervisor.gfxDevice->SetDepthMask(false);
    }
    g_Supervisor.gfxDevice->SetBlendMode(BLEND_ALPHA, BLEND_ALPHA);
    g_Supervisor.gfxDevice->SetColorOp(COMPONENT_RGB, COLOR_OP_MODULATE);
    g_Supervisor.gfxDevice->SetColorOp(COMPONENT_ALPHA, COLOR_OP_MODULATE);
    g_Supervisor.gfxDevice->DrawPrimitiveUP(PRIM_TRIANGLE_STRIP, 2, vertices, sizeof(VertexTex1DiffuseXyzrhw));

    // Leave the state the way AnmManager expects, as ScreenEffect::DrawSquare does.
    g_AnmManager->SetVertexShader(255);
    g_AnmManager->SetSprite(NULL);
    g_AnmManager->SetTexture(0);
    g_AnmManager->SetColorOp(255);
    g_AnmManager->SetBlendMode(255);
    g_AnmManager->SetZWriteDisable(255);
    g_Supervisor.gfxDevice->SetTextureArg(TEX_ARG_TEXTURE);
}

static void DrawArtCentered(const char *name, f32 centerX, f32 y, f32 alpha = 1.0f)
{
    DrawArt(name, centerX - ArtWidth(name) / 2.0f, y, alpha);
}

// Glyph strings: digits, ',', '(', ')' and '#' for 点, drawn from "<prefix><glyph>" sprites.
static void GlyphName(char *name, size_t size, const char *prefix, char glyph)
{
    const char *suffix = glyph == ',' ? "comma" : glyph == '(' ? "lp" : glyph == ')' ? "rp" : glyph == '#' ? "ten" : NULL;
    if (suffix != NULL)
    {
        std::snprintf(name, size, "%s%s", prefix, suffix);
    }
    else
    {
        std::snprintf(name, size, "%s%c", prefix, glyph);
    }
}

static f32 DrawGlyphs(const char *prefix, const char *text, f32 x, f32 y, f32 alpha = 1.0f)
{
    char name[32];

    for (; *text != '\0'; text++)
    {
        GlyphName(name, sizeof(name), prefix, *text);
        DrawArt(name, x, y, alpha);
        x += ArtWidth(name);
    }
    return x;
}

static void FormatScore(char *out, size_t size, u32 score)
{
    char digits[16];
    i32 length;
    i32 i;
    size_t used = 0;

    length = std::snprintf(digits, sizeof(digits), "%u", score);
    for (i = 0; i < length && used + 2 < size; i++)
    {
        if (i > 0 && (length - i) % 3 == 0)
        {
            out[used++] = ',';
        }
        out[used++] = digits[i];
    }
    out[used] = '\0';
}

// 0 at `start` frames, easing out to 1 over `length` frames.
static f32 Ease(i32 frames, f32 start, f32 length)
{
    f32 t = (frames - start) / length;

    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

// Cross-fades between the "<base>_off" and "<base>_on" sprites.
static void DrawArtHighlight(const char *base, f32 highlight, f32 x, f32 y, f32 alpha)
{
    char name[32];

    std::snprintf(name, sizeof(name), "%s_off", base);
    DrawArt(name, x, y, alpha * (1.0f - highlight));
    std::snprintf(name, sizeof(name), "%s_on", base);
    DrawArt(name, x, y, alpha * highlight);
}

static void DimRect(f32 left, f32 top, f32 right, f32 bottom, u32 color)
{
    ZunRect rect;

    rect.left = left;
    rect.top = top;
    rect.right = right;
    rect.bottom = bottom;
    ScreenEffect::DrawSquare(&rect, color);
}

// Laid out like the Choose Level screen: a heading and two items on wisps.
// The screen dims, the heading drops in, then the items drift in from the right.
static void DrawModeSelectArt()
{
    const f32 *highlight = g_State.modeHighlight;
    const f32 dim = Ease(g_State.modeSelectOpenFrames, 0.0f, 10.0f);
    const f32 heading = Ease(g_State.modeSelectFrames, 0.0f, 12.0f);
    f32 items[2];
    char desc[32];
    char text[32];
    char score[24];
    f32 alpha;
    f32 x;
    i32 i;

    for (i = 0; i < 2; i++)
    {
        items[i] = Ease(g_State.modeSelectFrames, 4.0f + 5.0f * i, 14.0f);
    }
    DimRect(0.0f, 0.0f, 640.0f, 480.0f, (u32)(0x99 * dim) << 24);
    if (g_State.modeSelect == MODE_SELECT_CHOOSE_MODE)
    {
        DrawArtCentered("hdr_rule", 320.0f, 34.0f - 10.0f * (1.0f - heading), heading);
        DrawArtHighlight("item_pd", highlight[MODE_ITEM_POINTDEVICE], 226.0f + 40.0f * (1.0f - items[0]), 128.0f,
                         items[0]);
        DrawArtHighlight("item_legacy", highlight[MODE_ITEM_LEGACY], 180.0f + 40.0f * (1.0f - items[1]), 248.0f,
                         items[1]);
        return;
    }
    DrawArtCentered("hdr_saved", 320.0f, 34.0f - 10.0f * (1.0f - heading), heading);
    x = 226.0f + 40.0f * (1.0f - items[0]);
    DrawArtHighlight("item_cont", highlight[RESUME_ITEM_RESUME], x, 128.0f, items[0]);
    // "Stage n の最初から再開する方向け (score点)" under the Continue item.
    alpha = items[0] * (0.55f + 0.45f * highlight[RESUME_ITEM_RESUME]);
    std::snprintf(desc, sizeof(desc), "cont_desc_%d", g_State.save.stage < 8 ? g_State.save.stage : 8);
    DrawArt(desc, x, 189.0f, alpha);
    FormatScore(score, sizeof(score), g_State.save.score);
    std::snprintf(text, sizeof(text), "(%s#)", score);
    DrawGlyphs("desc_", text, x + ArtWidth(desc) + 4.0f, 189.0f, alpha);
    DrawArtHighlight("item_new", highlight[RESUME_ITEM_NEW_RUN], 180.0f + 40.0f * (1.0f - items[1]), 248.0f,
                     items[1]);
}

// Laid out like the game's own pause menu. The play area dims, the title
// drops in, then the items slide in from the left. Once an item is picked they
// all slide on to the right and fade.
static void DrawInGameMenuArt()
{
    static const char *deathItems[DEATH_MENU_COUNT] = {"opt_retry", "opt_giveup"};
    static const char *pauseItems[PAUSE_MENU_COUNT] = {"opt_resume", "opt_savequit", "opt_quit"};
    const f32 exit =
        g_State.menuExit != MENU_EXIT_NONE ? Ease(g_State.menuExitFrames, 0.0f, PD_MENU_EXIT_FRAMES) : 0.0f;
    const f32 shown = 1.0f - exit;
    const f32 left = 24.0f * exit;
    const i32 frames = g_State.menuFrames;
    const f32 dim = Ease(frames, 0.0f, 8.0f) * shown;
    const f32 title = Ease(frames, 0.0f, 10.0f) * shown;
    char digits[16];
    char name[32];
    f32 itemsTop;
    f32 spacing;
    f32 item;
    i32 count;
    i32 i;

    if (shown <= 0.0f)
    {
        return;
    }
    DimRect(32.0f, 16.0f, 416.0f, 464.0f, (u32)(0x60 * dim) << 24);
    count = MenuItemCount();
    if (g_State.menu == MENU_DEATH)
    {
        DrawArt("title_hit", 140.0f + left, 170.0f - 8.0f * (1.0f - title), title);
        // "Miss · Retry n": this would be retry number n.
        std::snprintf(digits, sizeof(digits), "%d", g_State.retries + 1);
        DrawGlyphs("serif_", digits, 140.0f + left + FindArt("title_hit")->captionEnd + 4.0f,
                   195.0f - 8.0f * (1.0f - title), title);
        itemsTop = 236.0f;
        spacing = 48.0f;
    }
    else
    {
        DrawArt("title_pause", 140.0f + left, 160.0f - 8.0f * (1.0f - title), title);
        itemsTop = 220.0f;
        spacing = 44.0f;
    }
    for (i = 0; i < count; i++)
    {
        item = Ease(frames, 4.0f + 3.0f * i, 10.0f) * shown;
        DrawArtHighlight(g_State.menu == MENU_DEATH ? deathItems[i] : pauseItems[PauseItemAt(i)],
                         g_State.menuHighlight[i], 100.0f + left - 24.0f * (1.0f - item), itemsTop + spacing * i,
                         item);
    }
    if (g_State.menu == MENU_PAUSE && !g_GameManager.practice)
    {
        // The hint fades with the "Save and quit" highlight.
        std::snprintf(name, sizeof(name), "hint_%d", g_State.stageStart.stage < 8 ? g_State.stageStart.stage : 8);
        DrawArt(name, 100.0f + left, itemsTop + spacing * count + 14.0f,
                Ease(frames, 10.0f, 8.0f) * shown * g_State.menuHighlight[PAUSE_MENU_SAVE_AND_QUIT]);
    }
}

// The cut out and cut in around a rewind: strips sweep across the play area
// from alternate sides, one after another, until it is covered. The rewind
// happens behind them, "Miss · Retry n" shows, then the strips carry on across
// and off the other side.
static void DrawShutter()
{
    const i32 frame = g_State.menuExitFrames;
    const f32 regionLeft = 32.0f;
    const f32 regionTop = 16.0f;
    const f32 width = 384.0f;
    const f32 height = 448.0f / PD_SHUTTER_STRIPS;
    const f32 sweep = PD_SHUTTER_CLOSE_FRAMES - PD_SHUTTER_STAGGER * (PD_SHUTTER_STRIPS - 1);
    const f32 openStart = PD_SHUTTER_CLOSE_FRAMES + PD_SHUTTER_HOLD_FRAMES;
    const PointDeviceArt::Sprite *title = FindArt("title_hit");
    char digits[16];
    f32 in, out, left, right, top, edge;
    f32 text;
    f32 x;
    i32 i;

    for (i = 0; i < PD_SHUTTER_STRIPS; i++)
    {
        const bool fromLeft = i % 2 == 0;
        const f32 delay = PD_SHUTTER_STAGGER * i;

        in = Ease(frame, delay, sweep);
        out = Ease(frame, openStart + delay, sweep);
        if (in <= 0.0f || out >= 1.0f)
        {
            continue;
        }
        left = fromLeft ? regionLeft + width * out : regionLeft + width * (1.0f - in);
        right = fromLeft ? regionLeft + width * in : regionLeft + width * (1.0f - out);
        top = regionTop + height * i;
        DimRect(left, top, right, top + height, PD_SHUTTER_COLOR);
        // A bright edge leads each strip in and trails it out.
        if (in < 1.0f || out > 0.0f)
        {
            edge = in < 1.0f ? (fromLeft ? right : left) : (fromLeft ? left : right);
            DimRect(edge - 1.5f, top, edge + 1.5f, top + height,
                    PD_SHUTTER_EDGE_COLOR | ((u32)(0xe0 * (in < 1.0f ? 1.0f - in : out)) << 24));
        }
    }

    text = Ease(frame, PD_SHUTTER_CLOSE_FRAMES, 8.0f) * (1.0f - Ease(frame, openStart, 10.0f));
    if (text <= 0.0f || title == NULL)
    {
        return;
    }
    std::snprintf(digits, sizeof(digits), "%d", g_State.retries);
    x = regionLeft + width / 2.0f - title->contentWidth / 2.0f;
    top = 200.0f + 6.0f * (1.0f - text);
    DrawArt("title_hit", x, top, text);
    DrawGlyphs("serif_", digits, x + title->captionEnd + 4.0f, top + 25.0f, text);
}

void DrawMenus()
{
    if (g_State.modeSelect == MODE_SELECT_CLOSED && (g_State.menu == MENU_NONE || !g_State.runActive))
    {
        return;
    }
    // Sprites drawn so far may still be batched; draw them first so the menus cover them.
    g_AnmManager->Flush();
    // The menus are laid out in whole-screen coordinates, whatever viewport the
    // last drawn element left behind.
    g_Supervisor.viewport.x = 0;
    g_Supervisor.viewport.y = 0;
    g_Supervisor.viewport.width = 640;
    g_Supervisor.viewport.height = 480;
    g_Supervisor.gfxDevice->SetViewport(g_Supervisor.viewport);
    if (g_State.modeSelect != MODE_SELECT_CLOSED)
    {
        DrawModeSelectArt();
    }
    else
    {
        DrawInGameMenuArt();
        if (g_State.menuExit == MENU_EXIT_REWIND)
        {
            DrawShutter();
        }
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
    g_AsciiManager.SetColor(PD_COLOR_WHITE);
}

}; // namespace PointDevice
