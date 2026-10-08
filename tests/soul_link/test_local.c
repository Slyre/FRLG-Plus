#include <assert.h>
#include <stdio.h>
#include "global.h"
#include "soul_link.h"
#include "soul_link_local.h"
#include "nuzlocke.h"
#include "pokemon_storage_system.h"
#include "constants/battle.h"
#include "constants/flags.h"
#include "constants/items.h"
#include "constants/maps.h"
#include "constants/region_map_sections.h"
#include "../../src/data/nuzlocke_areas.h"

static struct SaveBlock1 sBlock1;
static struct SaveBlock2 sBlock2;
static struct PokemonStorage sStorage;
struct SaveBlock1 *gSaveBlock1Ptr = &sBlock1;
struct SaveBlock2 *gSaveBlock2Ptr = &sBlock2;
struct PokemonStorage *gPokemonStoragePtr = &sStorage;
struct Pokemon gPlayerParty[PARTY_SIZE], gEnemyParty[PARTY_SIZE];
u8 gPlayerPartyCount;
struct Main gMain;
u32 gBattleTypeFlags;
static u8 sSection;
static bool8 sFlags[0x1000], sDupes[NUM_SPECIES];
static unsigned sNotices;
static void (*sNoticeTask)(u8);

u16 Random(void) { return 123; }
bool8 FlagGet(u16 flag) { assert(flag < sizeof(sFlags)); return sFlags[flag]; }
bool8 FlagSet(u16 flag) { assert(flag < sizeof(sFlags)); sFlags[flag] = TRUE; return TRUE; }
bool8 IsWildMonNuzlockeDupe(u16 species) { return sDupes[species]; }
void SetNuzlockeDupeFlags(u16 species) { sDupes[species] = TRUE; }
u16 SpeciesToNationalPokedexNum(u16 species) { return species; }
u8 GetCurrentRegionMapSectionId(void) { return sSection; }
bool8 ItemIsMail(u16 item) { return item == ITEM_ORANGE_MAIL; }
bool8 IsFieldMessageBoxHidden(void) { return TRUE; }
bool8 ShowFieldMessage(const u8 *message) { assert(message != NULL); sNotices++; return TRUE; }
void HideFieldMessageBox(void) {}
void LockPlayerFieldControls(void) {}
void UnlockPlayerFieldControls(void) {}
u8 CreateTask(void (*callback)(u8), u8 priority) { (void)priority; sNoticeTask = callback; return 0; }
void DestroyTask(u8 taskId) { (void)taskId; sNoticeTask = NULL; }
u8 *StringCopy(u8 *dest, const u8 *source) { return (u8 *)strcpy((char *)dest, (const char *)source); }
struct BoxPokemon *GetBoxedMonPtr(u8 box, u8 pos) { assert(box < 14 && pos < 30); return &sStorage.boxes[box][pos]; }
u8 *GetBoxNamePtr(u8 box) { return sStorage.boxNames[box]; }
void ZeroMonData(struct Pokemon *mon) { memset(mon, 0, sizeof(*mon)); }
void ZeroBoxMonData(struct BoxPokemon *mon) { memset(mon, 0, sizeof(*mon)); }
void ZeroBoxMonAt(u8 box, u8 pos) { ZeroBoxMonData(GetBoxedMonPtr(box, pos)); }
u8 CalculatePlayerPartyCount(void)
{
    unsigned i;
    gPlayerPartyCount = 0;
    for (i = 0; i < PARTY_SIZE; i++)
        if (gPlayerParty[i].box.species != SPECIES_NONE)
            gPlayerPartyCount++;
    return gPlayerPartyCount;
}
s16 CompactPartySlots(void)
{
    unsigned read, write = 0;
    for (read = 0; read < PARTY_SIZE; read++)
        if (gPlayerParty[read].box.species != SPECIES_NONE)
            gPlayerParty[write++] = gPlayerParty[read];
    for (read = write; read < PARTY_SIZE; read++)
        ZeroMonData(&gPlayerParty[read]);
    return write;
}
u32 GetBoxMonData(struct BoxPokemon *mon, s32 field, u8 *dest)
{
    (void)dest;
    switch (field)
    {
    case MON_DATA_PERSONALITY: return mon->personality;
    case MON_DATA_OT_ID: return mon->otId;
    case MON_DATA_SPECIES: return mon->species;
    case MON_DATA_IS_EGG: return mon->isEgg;
    case MON_DATA_HELD_ITEM: return mon->heldItem;
    case MON_DATA_BOX_HP: return SoulLink_IsBoxMonDead(mon) ? 0 : mon->boxHP;
    default: assert(0); return 0;
    }
}
u32 GetMonData(struct Pokemon *mon, s32 field, u8 *dest)
{
    if (field == MON_DATA_HP) return SoulLink_IsBoxMonDead(&mon->box) ? 0 : mon->hp;
    if (field == MON_DATA_MAIL) return mon->mail;
    return GetBoxMonData(&mon->box, field, dest);
}
void SetBoxMonData(struct BoxPokemon *mon, s32 field, const void *value)
{
    assert(field == MON_DATA_BOX_HP);
    mon->boxHP = *(const u16 *)value;
}

static struct Pokemon Mon(u32 personality, u16 species)
{
    struct Pokemon mon = {0};
    mon.box.personality = personality;
    mon.box.otId = 10;
    mon.box.species = species;
    mon.box.boxHP = mon.hp = mon.maxHP = 20;
    mon.mail = MAIL_NONE;
    return mon;
}
static void Reset(u8 player)
{
    struct SoulLinkRunId run = {{123, 456}};
    u32 participants[2] = {10, 20};
    struct SoulLinkConfig config;
    memset(&sBlock1, 0, sizeof(sBlock1));
    memset(&sBlock2, 0, sizeof(sBlock2));
    memset(&sStorage, 0, sizeof(sStorage));
    memset(gPlayerParty, 0, sizeof(gPlayerParty));
    memset(gEnemyParty, 0, sizeof(gEnemyParty));
    memset(sFlags, 0, sizeof(sFlags));
    memset(sDupes, 0, sizeof(sDupes));
    gMain.inBattle = FALSE;
    gBattleTypeFlags = 0;
    sSection = MAPSEC_ROUTE_1;
    gPlayerPartyCount = 0;
    SoulLink_ResetSaveData();
    assert(SoulLink_BeginRun(&run, participants, player));
    assert(SoulLink_GetConfig(&config));
    config.enabled = TRUE;
    assert(SoulLink_SetConfig(&config));
    FlagSet(FLAG_SYS_POKEDEX_GET);
    SoulLink_RequestCleanup();
    SoulLink_BeginBattle();
    SoulLink_EndBattle(); // Clear a previous test's snapshot before its first field tick.
    SoulLink_ResetSaveData();
    assert(SoulLink_BeginRun(&run, participants, player));
    assert(SoulLink_SetConfig(&config));
    sFlags[NUZLOCKE_FLAGS_START + 7] = FALSE;
}
static void FieldTicks(void)
{
    unsigned i;
    gMain.newKeys = A_BUTTON;
    for (i = 0; i < 256; i++)
    {
        SoulLink_ProcessField();
        if (sNoticeTask != NULL) sNoticeTask(0);
    }
}
static void Register(struct Pokemon *mon, u16 area)
{
    struct SoulLinkMonIdentity identity = {mon->box.otId, mon->box.personality, mon->box.species, 0};
    assert(SoulLink_RecordLocalEncounter(area, &identity, SOUL_LINK_ENCOUNTER_CAPTURED));
}

static void TestCanonicalAreas(void)
{
    assert(SoulLink_GetCanonicalArea(MAPSEC_ROUTE_3, 0, 0) == 0xA);
    assert(SoulLink_GetCanonicalArea(MAPSEC_ROUTE_4, 0, 0) == 0xB);
    assert(SoulLink_GetCanonicalArea(MAPSEC_MT_MOON, MAP_GROUP(MT_MOON_1F), MAP_NUM(MT_MOON_1F))
        == SoulLink_GetCanonicalArea(MAPSEC_MT_MOON, MAP_GROUP(MT_MOON_B2F), MAP_NUM(MT_MOON_B2F)));
    assert(SoulLink_GetCanonicalArea(MAPSEC_KANTO_SAFARI_ZONE, MAP_GROUP(SAFARI_ZONE_EAST), MAP_NUM(SAFARI_ZONE_EAST)) == 0x4F);
    assert(SoulLink_GetCanonicalArea(MAPSEC_KANTO_SAFARI_ZONE, MAP_GROUP(SAFARI_ZONE_NORTH_EAST), MAP_NUM(SAFARI_ZONE_NORTH_EAST)) == 0x53);
    assert(SoulLink_GetCanonicalArea(MAPSEC_KANTO_SAFARI_ZONE, 0xFF, MAP_NUM(SAFARI_ZONE_EAST)) == 0x27);
    assert(SoulLink_GetCanonicalArea(0xFF, 0, 0) == SOUL_LINK_AREA_NONE);
}
static void TestEncounterLifecycle(void)
{
    struct SoulLinkPair pair;
    struct SoulLinkConfig config;
    struct Pokemon mon = Mon(1, SPECIES_BULBASAUR);
    Reset(0);
    gEnemyParty[0] = mon;
    SoulLink_BeginBattle();
    gMain.inBattle = TRUE;
    assert(SoulLink_PrepareAcquisition(&mon));
    SoulLink_FinishAcquisition(&mon, MON_CANT_GIVE);
    assert(SoulLink_GetPairCount() == 0);
    gPlayerParty[0] = mon;
    SoulLink_FinishAcquisition(&mon, MON_GIVEN_TO_PARTY);
    assert(SoulLink_GetPairCount() == 1);
    gMain.inBattle = FALSE;
    SoulLink_EndBattle();
    assert(SoulLink_GetPairCount() == 1 && SoulLink_ReadPair(0, &pair));
    assert(pair.areaId == 8 && pair.state == SOUL_LINK_PAIR_PENDING);
    sSection = MAPSEC_ROUTE_2;
    gEnemyParty[0] = Mon(2, SPECIES_CHARMANDER);
    SoulLink_BeginBattle();
    SoulLink_EndBattle(); // Fleeing / KO / failed acquisition consumes the eligible encounter.
    assert(SoulLink_ReadPair(1, &pair) && pair.state == SOUL_LINK_PAIR_FAILED);
    SoulLink_BeginBattle(); SoulLink_EndBattle();
    assert(SoulLink_GetPairCount() == 2);
    sSection = MAPSEC_ROUTE_3;
    gEnemyParty[0] = mon;
    SoulLink_BeginBattle(); SoulLink_EndBattle(); // Dupe does not consume Route 3.
    assert(SoulLink_GetPairCount() == 2);
    SoulLink_GetConfig(&config);
    config.dupesClause = FALSE;
    config.firstEncounterOnly = FALSE;
    assert(SoulLink_SetConfig(&config));
    mon = Mon(3, SPECIES_BULBASAUR);
    assert(SoulLink_PrepareAcquisition(&mon));
    SoulLink_FinishAcquisition(&mon, MON_GIVEN_TO_PC);
    mon.box.personality++;
    assert(SoulLink_PrepareAcquisition(&mon));
    SoulLink_FinishAcquisition(&mon, MON_GIVEN_TO_PC);
    assert(SoulLink_GetPairCount() == 4);
}
static void TestStarterEggAndExclusions(void)
{
    struct Pokemon mon = Mon(10, SPECIES_SQUIRTLE);
    struct SoulLinkPair pair;
    Reset(0);
    sFlags[FLAG_SYS_POKEDEX_GET] = FALSE;
    sBlock1.location.mapGroup = MAP_GROUP(PALLET_TOWN_PROFESSOR_OAKS_LAB);
    sBlock1.location.mapNum = MAP_NUM(PALLET_TOWN_PROFESSOR_OAKS_LAB);
    assert(SoulLink_PrepareAcquisition(&mon));
    SoulLink_FinishAcquisition(&mon, MON_GIVEN_TO_PARTY);
    assert(SoulLink_ReadPair(0, &pair) && pair.areaId == SOUL_LINK_AREA_STARTER);
    assert(!SoulLink_PrepareAcquisition(&mon));
    sBlock1.location.mapGroup = sBlock1.location.mapNum = 0;
    FlagSet(FLAG_SYS_POKEDEX_GET);
    sSection = MAPSEC_ROUTE_2;
    mon = Mon(11, SPECIES_BULBASAUR);
    mon.box.isEgg = TRUE;
    assert(SoulLink_PrepareAcquisition(&mon));
    SoulLink_FinishAcquisition(&mon, MON_GIVEN_TO_PARTY);
    sSection = MAPSEC_ROUTE_3;
    mon.box.isEgg = FALSE;
    assert(SoulLink_ReadPair(1, &pair) && pair.areaId == 9); // Receipt area survives hatching elsewhere.
    gPlayerParty[0] = mon;
    gBattleTypeFlags = BATTLE_TYPE_LINK;
    SoulLink_BeginBattle(); SoulLink_RecordBattleDeath(0); SoulLink_EndBattle();
    assert(!SoulLink_IsBoxMonDead(&gPlayerParty[0].box));
    gBattleTypeFlags = BATTLE_TYPE_BATTLE_TOWER;
    SoulLink_BeginBattle(); SoulLink_RecordBattleDeath(0); SoulLink_EndBattle();
    assert(!SoulLink_IsBoxMonDead(&gPlayerParty[0].box));
}
static void TestGraveAndTerminalDeath(void)
{
    struct SoulLinkPair pair;
    struct SoulLinkConfig config;
    Reset(0);
    gPlayerParty[0] = Mon(100, SPECIES_BULBASAUR);
    gPlayerParty[0].box.heldItem = ITEM_POTION;
    gPlayerParty[1] = Mon(101, SPECIES_CHARMANDER);
    Register(&gPlayerParty[0], 7);
    gPlayerParty[0].hp = 0;
    SoulLink_RecordMonDeath(&gPlayerParty[0]);
    assert(SoulLink_IsBoxMonDead(&gPlayerParty[0].box));
    gPlayerParty[0].hp = 20; // Simulate an unconditional special heal.
    assert(GetMonData(&gPlayerParty[0], MON_DATA_HP, NULL) == 0);
    SoulLink_GetConfig(&config); config.enabled = FALSE; SoulLink_SetConfig(&config);
    assert(SoulLink_IsBoxMonDead(&gPlayerParty[0].box));
    assert(!Nuzlocke_IsActive() && Nuzlocke_HasPermanentDeaths());
    SoulLink_RequestCleanup(); FieldTicks();
    assert(gPlayerParty[0].box.personality == 101 && gPlayerPartyCount == 1);
    assert(sBlock2.soulLink.header.graveBox == 13 && SoulLink_IsGraveBox(13));
    assert(sStorage.boxes[13][0].personality == 100 && sStorage.boxes[13][0].heldItem == ITEM_POTION);
    assert(sStorage.boxes[13][0].boxHP == 0 && sStorage.boxes[13][0].species == SPECIES_BULBASAUR);
    assert(SoulLink_ReadPair(0, &pair) && pair.state == SOUL_LINK_PAIR_DEAD && pair.cleanupFlags == 0);
    assert(SoulLink_LoadSaveData());
    assert(SoulLink_IsBoxMonDead(&sStorage.boxes[13][0]));
    // A PC withdrawal cannot make it usable and is safely undone after closing the PC.
    gPlayerParty[1] = Mon(100, SPECIES_IVYSAUR);
    ZeroBoxMonAt(13, 0);
    SoulLink_RequestCleanup(); FieldTicks();
    assert(gPlayerPartyCount == 1 && sStorage.boxes[13][0].species == SPECIES_IVYSAUR);
}
static void TestBlockedCleanupAndAmbiguity(void)
{
    struct SoulLinkPair pair;
    unsigned i;
    Reset(0);
    gPlayerParty[0] = Mon(200, SPECIES_BULBASAUR);
    Register(&gPlayerParty[0], 7);
    gPlayerParty[0].hp = 0;
    SoulLink_RecordMonDeath(&gPlayerParty[0]); FieldTicks();
    assert(gPlayerParty[0].box.personality == 200); // Entirely fainted party remains intact for whiteout recovery.
    gPlayerParty[1] = Mon(201, SPECIES_CHARMANDER);
    gPlayerParty[0].mail = 0;
    SoulLink_RequestCleanup(); FieldTicks();
    assert(gPlayerParty[0].box.personality == 200);
    gPlayerParty[0].mail = MAIL_NONE;
    SoulLink_SetGraveBox(13);
    for (i = 0; i < 30; i++) sStorage.boxes[13][i] = Mon(300 + i, SPECIES_PIDGEY).box;
    SoulLink_RequestCleanup(); FieldTicks();
    assert(gPlayerParty[0].box.personality == 200 && sStorage.boxes[13][0].personality == 300);
    assert(SoulLink_ReadPair(0, &pair) && pair.cleanupFlags == 1);
    ZeroBoxMonAt(13, 0);
    sStorage.boxes[0][0] = gPlayerParty[0].box;
    SoulLink_RequestCleanup(); FieldTicks();
    assert(gPlayerParty[0].box.personality == 200 && sStorage.boxes[13][0].species == SPECIES_NONE);
    // Shedinja's inherited PID is explicitly excluded instead of picking the wrong Pokémon.
    sStorage.boxes[0][0].species = SPECIES_SHEDINJA;
    assert(!SoulLink_IsBoxMonDead(&sStorage.boxes[0][0]));
    SoulLink_RequestCleanup(); FieldTicks();
    assert(gPlayerParty[0].box.personality == 201 && sStorage.boxes[13][0].personality == 200);
}
static void TestDaycareCandidatesAndTrade(void)
{
    struct Pokemon mon, received;
    struct SoulLinkMonIdentity identity;
    struct SoulLinkPair pair;
    Reset(1);
    mon = Mon(400, SPECIES_BULBASAUR); Register(&mon, 7);
    received = Mon(401, SPECIES_CHARMANDER); Register(&received, 8);
    assert(SoulLink_ReadPair(0, &pair) && pair.pairId == 0);
    assert(SoulLink_ReadPair(1, &pair) && pair.pairId == 0 && pair.mons[1].encounterOrdinal == 2);
    assert(SoulLink_LoadSaveData());
    identity = (struct SoulLinkMonIdentity){10, 401, SPECIES_CHARMANDER, 0};
    assert(SoulLink_MarkLocalDeath(&identity));
    assert(SoulLink_ReadPair(0, &pair) && pair.encounterStates[1] == SOUL_LINK_ENCOUNTER_CAPTURED);
    sBlock1.daycare.mons[0].mon = received.box;
    SoulLink_RequestCleanup(); FieldTicks();
    assert(sBlock1.daycare.mons[0].mon.species == SPECIES_NONE && sStorage.boxes[13][0].personality == 401);
    received = Mon(402, SPECIES_MR_MIME);
    assert(SoulLink_TransferNpcTrade(&mon, &received));
    assert(!SoulLink_IsBoxMonDead(&received.box));
    identity = (struct SoulLinkMonIdentity){10, 402, SPECIES_MR_MIME, 0};
    assert(SoulLink_FindLocalPair(&identity, NULL, &pair) && pair.areaId == 7 && pair.mons[1].encounterOrdinal == 1);
    mon = received;
    received = Mon(401, SPECIES_CHARMANDER); // Identity already belongs to the dead second candidate.
    assert(!SoulLink_TransferNpcTrade(&mon, &received));
    assert(SoulLink_FindLocalPair(&identity, NULL, &pair));
}

static void TestStorageAndRecordLimits(void)
{
    struct Pokemon mon = Mon(1000, SPECIES_SQUIRTLE);
    struct SoulLinkConfig config;
    struct SoulLinkMonIdentity identity;
    unsigned box, pos;
    Reset(0);
    for (pos = 0; pos < PARTY_SIZE; pos++) gPlayerParty[pos] = Mon(1100 + pos, SPECIES_PIDGEY);
    for (box = 0; box < 14; box++)
        for (pos = 0; pos < 30; pos++) sStorage.boxes[box][pos] = Mon(1200 + box * 30 + pos, SPECIES_PIDGEY).box;
    assert(!SoulLink_PrepareAcquisition(&mon));
    SoulLink_SetGraveBox(13);
    for (pos = 0; pos < 30; pos++) ZeroBoxMonAt(13, pos);
    assert(!SoulLink_PrepareAcquisition(&mon)); // Grave space does not count as living capture storage.
    ZeroBoxMonAt(0, 0);
    assert(SoulLink_PrepareAcquisition(&mon));
    Reset(0);
    SoulLink_GetConfig(&config); config.firstEncounterOnly = config.dupesClause = FALSE;
    SoulLink_SetConfig(&config);
    for (pos = 0; pos < 88; pos++)
    {
        identity = (struct SoulLinkMonIdentity){10, pos + 2000, SPECIES_PIDGEY, 0};
        assert(SoulLink_RecordLocalEncounter(8, &identity, SOUL_LINK_ENCOUNTER_CAPTURED));
    }
    assert(!SoulLink_PrepareAcquisition(&mon));
    assert(SoulLink_GetPairCount() == 88);
    Reset(0);
    sBlock2.soulLink.header.nextPairId = 0;
    assert(!SoulLink_PrepareAcquisition(&mon));
    Reset(0);
    config.enabled = FALSE;
    SoulLink_SetConfig(&config);
    assert(!Nuzlocke_IsActive() && !Nuzlocke_HasPermanentDeaths());
    assert(SoulLink_PrepareAcquisition(&mon));
    sBlock1.keyFlags.nuzlocke = TRUE;
    assert(Nuzlocke_IsActive() && Nuzlocke_IsFirstEncounterOnly() && Nuzlocke_IsDupesClauseEnabled());
}

static void TestVersionOneMigration(void)
{
    struct Pokemon mon = Mon(5000, SPECIES_BULBASAUR);
    struct SoulLinkPair before, after;
    Reset(0);
    Register(&mon, 8);
    assert(SoulLink_ReadPair(0, &before));
    sBlock2.soulLink.header.version = 1;
    assert(SoulLink_LoadSaveData());
    assert(sBlock2.soulLink.header.version == SOUL_LINK_SAVE_VERSION);
    assert(SoulLink_ReadPair(0, &after) && memcmp(&before, &after, sizeof(before)) == 0);
}

static void TestReservedCaptureCapacity(void)
{
    struct SoulLinkPair pair = {0};
    struct Pokemon mon = Mon(9001, SPECIES_CHARMANDER);
    u16 i;
    Reset(1);
    for (i = 1; i <= SOUL_LINK_PAIR_CAPACITY; i++)
    {
        memset(&pair, 0, sizeof(pair));
        pair.pairId = i; pair.areaId = i + 7; pair.state = SOUL_LINK_PAIR_PENDING;
        pair.encounterStates[0] = SOUL_LINK_ENCOUNTER_CAPTURED;
        pair.mons[0].otId = 10; pair.mons[0].personality = i;
        pair.mons[0].species = SPECIES_BULBASAUR; pair.mons[0].encounterOrdinal = i;
        assert(SoulLink_StorePair(&pair));
    }
    mon.box.otId = 20;
    gEnemyParty[0] = mon; gMain.inBattle = TRUE;
    SoulLink_BeginBattle();
    assert(SoulLink_PrepareAcquisition(&mon));
    gPlayerParty[0] = mon; gPlayerPartyCount = 1;
    SoulLink_FinishAcquisition(&mon, MON_GIVEN_TO_PARTY);
    SoulLink_EndBattle(); gMain.inBattle = FALSE;
    assert(SoulLink_GetPairCount() == SOUL_LINK_PAIR_CAPACITY);
    assert(SoulLink_ReadPair(0, &pair) && pair.state == SOUL_LINK_PAIR_ALIVE && pair.pairId == 1);
    assert(pair.mons[1].personality == 9001);

    Reset(0); memset(&pair, 0, sizeof(pair));
    pair.pairId = 1; pair.areaId = 8; pair.state = SOUL_LINK_PAIR_PENDING;
    pair.encounterStates[1] = SOUL_LINK_ENCOUNTER_CAPTURED;
    pair.mons[1].otId = 20; pair.mons[1].personality = 9002;
    pair.mons[1].species = SPECIES_BULBASAUR; pair.mons[1].encounterOrdinal = 1;
    assert(SoulLink_StorePair(&pair)); sBlock2.soulLink.header.nextPairId = 0;
    gEnemyParty[0] = mon; gMain.inBattle = TRUE; SoulLink_BeginBattle();
    assert(SoulLink_PrepareAcquisition(&mon));
    gPlayerParty[0] = mon; gPlayerPartyCount = 1;
    SoulLink_FinishAcquisition(&mon, MON_GIVEN_TO_PARTY);
    SoulLink_EndBattle(); gMain.inBattle = FALSE;
    assert(SoulLink_ReadPair(0, &pair) && pair.state == SOUL_LINK_PAIR_ALIVE);
    assert(sBlock2.soulLink.header.nextPairId == 0);
}

static void TestRemoteDeathCleanup(void)
{
    struct SoulLinkPair pair = {0}, result;
    Reset(0);
    gPlayerParty[0] = Mon(900, SPECIES_BULBASAUR);
    gPlayerParty[0].box.heldItem = ITEM_ORAN_BERRY;
    gPlayerParty[1] = Mon(901, SPECIES_CHARMANDER);
    gPlayerPartyCount = 2;
    pair.pairId = 1; pair.areaId = 8; pair.state = SOUL_LINK_PAIR_DEAD;
    pair.mons[0] = (struct SoulLinkMonIdentity){10, 900, SPECIES_BULBASAUR, 1};
    pair.mons[1] = (struct SoulLinkMonIdentity){20, 1900, SPECIES_SQUIRTLE, 1};
    pair.encounterStates[0] = SOUL_LINK_ENCOUNTER_CAPTURED;
    pair.encounterStates[1] = SOUL_LINK_ENCOUNTER_DEAD;
    assert(SoulLink_StorePair(&pair));
    gMain.inBattle = TRUE;
    assert(GetMonData(&gPlayerParty[0], MON_DATA_HP, NULL) == 0);
    SoulLink_ProcessField();
    assert(gPlayerPartyCount == 2 && gPlayerParty[0].box.personality == 900);
    assert(SoulLink_ReadPair(0, &result) && result.encounterStates[0] == SOUL_LINK_ENCOUNTER_CAPTURED);
    gMain.inBattle = FALSE; FieldTicks();
    assert(gPlayerPartyCount == 1 && gPlayerParty[0].box.personality == 901);
    assert(sStorage.boxes[13][0].personality == 900);
    assert(sStorage.boxes[13][0].heldItem == ITEM_ORAN_BERRY);
    assert(SoulLink_ReadPair(0, &result) && result.encounterStates[0] == SOUL_LINK_ENCOUNTER_DEAD);
    assert(result.cleanupFlags == 0);
}

int main(void)
{
    TestCanonicalAreas();
    TestEncounterLifecycle();
    TestStarterEggAndExclusions();
    TestGraveAndTerminalDeath();
    TestBlockedCleanupAndAmbiguity();
    TestDaycareCandidatesAndTrade();
    TestStorageAndRecordLimits();
    TestVersionOneMigration();
    TestReservedCaptureCapacity();
    TestRemoteDeathCleanup();
    assert(sNotices != 0);
    puts("Soul Link local gameplay: all encounter, death and grave tests passed");
    return 0;
}

bool8 SoulLink_CanUseSpecies(struct BoxPokemon *mon, u16 species) { (void)mon; (void)species; return TRUE; }
