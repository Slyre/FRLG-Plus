#ifndef GUARD_SOUL_LINK_TYPES_H
#define GUARD_SOUL_LINK_TYPES_H

#include "gba/types.h"

#define SOUL_LINK_SAVE_MAGIC 0x4B4E4C53 // SLNK
#define SOUL_LINK_SAVE_VERSION 3
#define SOUL_LINK_AREA_STARTER 0x57
#define SOUL_LINK_AREA_VERSION 1
#define SOUL_LINK_PAIR_CAPACITY 88
#define SOUL_LINK_PRIMARY_PAIR_COUNT 30
#define SOUL_LINK_SECONDARY_PAIR_COUNT 11
#define SOUL_LINK_OVERFLOW_PAIR_COUNT (SOUL_LINK_PAIR_CAPACITY - SOUL_LINK_PRIMARY_PAIR_COUNT - SOUL_LINK_SECONDARY_PAIR_COUNT)
#define SOUL_LINK_PLAYER_COUNT 2
#define SOUL_LINK_PLAYER_NONE 0xFF
#define SOUL_LINK_BOX_NONE 0xFF
#define SOUL_LINK_PAIR_NONE 0
#define SOUL_LINK_AREA_NONE 0
#define SOUL_LINK_LEGACY_STORAGE_SIZE 0x83D0
#define SOUL_LINK_CLEANUP_PLAYER_0 (1 << 0)
#define SOUL_LINK_CLEANUP_PLAYER_1 (1 << 1)
#define SOUL_LINK_CLEANUP_MASK (SOUL_LINK_CLEANUP_PLAYER_0 | SOUL_LINK_CLEANUP_PLAYER_1)

typedef u16 SoulLinkPairId;

enum SoulLinkTypeClause
{
    SOUL_LINK_TYPE_NONE,
    SOUL_LINK_TYPE_PRIMARY,
    SOUL_LINK_TYPE_ANY,
    SOUL_LINK_TYPE_COUNT,
};

enum SoulLinkPairState
{
    SOUL_LINK_PAIR_EMPTY,
    SOUL_LINK_PAIR_PENDING,
    SOUL_LINK_PAIR_ALIVE,
    SOUL_LINK_PAIR_DEAD,
    SOUL_LINK_PAIR_FAILED,
};

enum SoulLinkEncounterState
{
    SOUL_LINK_ENCOUNTER_NONE,
    SOUL_LINK_ENCOUNTER_CAPTURED,
    SOUL_LINK_ENCOUNTER_FAILED,
    SOUL_LINK_ENCOUNTER_DEAD,
};

enum SoulLinkSaveStatus
{
    SOUL_LINK_SAVE_UNLOADED,
    SOUL_LINK_SAVE_READY,
    SOUL_LINK_SAVE_UNSUPPORTED,
    SOUL_LINK_SAVE_CORRUPT,
};

struct SoulLinkConfig
{
    bool8 enabled;
    bool8 linkedDeaths;
    bool8 sameAreaPairing;
    bool8 firstEncounterOnly;
    bool8 dupesClause;
    u8 typeClause;
    bool8 enforcePartyPairs;
    u8 reserved;
};

struct SoulLinkRunId
{
    u32 words[2];
};

struct SoulLinkMonIdentity
{
    u32 otId;
    u32 personality;
    u16 species;
    u16 encounterOrdinal;
};

struct SoulLinkPair
{
    struct SoulLinkMonIdentity mons[SOUL_LINK_PLAYER_COUNT];
    SoulLinkPairId pairId;
    u16 areaId;
    u8 encounterStates[SOUL_LINK_PLAYER_COUNT];
    u8 state;
    u8 cleanupFlags;
};

struct SoulLinkTeamSelection
{
    SoulLinkPairId pairIds[6];
    u8 count;
    bool8 locked;
    u16 reserved;
    u32 revision;
};

struct SoulLinkSaveHeader
{
    u32 magic;
    u16 version;
    u16 areaVersion;
    struct SoulLinkRunId runId;
    u32 participantIds[SOUL_LINK_PLAYER_COUNT];
    SoulLinkPairId nextPairId; // Zero means the ID space is exhausted.
    u16 pairCount;
    u8 localPlayer;
    u8 graveBox;
    u16 reservedFlags;
    struct SoulLinkConfig config;
    u32 configRevision;
    struct SoulLinkTeamSelection team;
};

struct SoulLinkSave
{
    struct SoulLinkSaveHeader header;
    struct SoulLinkPair pairs[SOUL_LINK_PRIMARY_PAIR_COUNT];
};

#endif
