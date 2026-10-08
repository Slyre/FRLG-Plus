#ifndef TEST_SOUL_LINK_GLOBAL_H
#define TEST_SOUL_LINK_GLOBAL_H
#define GUARD_GLOBAL_H
#include <stddef.h>
#include <string.h>
#include "soul_link_types.h"
#include "constants/species.h"
#include "constants/global.h"
#define TRUE 1
#define FALSE 0
#define EWRAM_DATA
#define STATIC_ASSERT(expr, id) typedef char id[(expr) ? 1 : -1]
#define ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))
#define min(a, b) ((a) < (b) ? (a) : (b))
struct Main { u32 *vblankCounter1; bool8 inBattle; u16 newKeys; };
#define A_BUTTON 1
#define B_BUTTON 2
extern struct Main gMain;
extern bool8 gSoftResetDisabled;
struct BoxPokemon
{
    u32 personality, otId;
    u16 species, boxHP, heldItem;
    u8 isEgg;
    u8 unused[65];
};
struct Pokemon
{
    struct BoxPokemon box;
    u32 status;
    u16 hp, maxHP;
    u8 mail;
    u8 unused[11];
};
struct DaycareMon { struct BoxPokemon mon; u8 mail[56]; u32 steps; };
struct DayCare { struct DaycareMon mons[2]; u32 offspringPersonality; u8 stepCounter; };
struct SaveBlock1
{
    struct { u8 mapGroup, mapNum; } location;
    struct { bool8 nuzlocke; } keyFlags;
    u8 legacyPrefix[0x59C - 3];
    struct SoulLinkPair soulLinkItemReservePairs[2];
    u8 legacyMiddle1[0x2F18 - 0x5DC];
    struct SoulLinkPair soulLinkOldManReservePairs[2];
    struct SoulLinkPair soulLinkEasyChatReservePairs[1];
    u8 legacyMiddle2[4];
    struct DayCare daycare;
    u8 legacyAfterDaycare[0x348C - 0x309C];
    struct SoulLinkPair soulLinkFreeWorkPairs[5];
    u8 legacyMiddle3[0x3A94 - 0x352C];
    struct SoulLinkPair soulLinkFameReservePairs[1];
    u8 legacyAfterFame[0x3C98 - 0x3AB4];
    struct DaycareMon route5DayCareMon;
    u8 legacyRest[0x3D68 - 0x3D24];
};
struct SaveBlock2
{
    u8 legacyPrefix[0xB20];
    struct SoulLinkSave soulLink;
    u32 encryptionKey;
};
struct PokemonStorage
{
    union
    {
        u8 legacyStorage[SOUL_LINK_LEGACY_STORAGE_SIZE];
        struct
        {
            u32 currentBox;
            struct BoxPokemon boxes[14][30];
            u8 boxNames[14][9];
            u8 boxWallpapers[14];
        };
    };
    struct SoulLinkPair soulLinkPairs[SOUL_LINK_OVERFLOW_PAIR_COUNT];
};
extern struct SaveBlock2 *gSaveBlock2Ptr;
extern struct SaveBlock1 *gSaveBlock1Ptr;
extern struct PokemonStorage *gPokemonStoragePtr;
extern struct Pokemon gPlayerParty[6], gEnemyParty[6];
extern u8 gPlayerPartyCount;
#define _(text) text
#endif
