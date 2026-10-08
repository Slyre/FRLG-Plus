#include <assert.h>
#include <stdio.h>
#include "global.h"
#include "soul_link.h"

static struct SaveBlock2 sBlock2;
static struct SaveBlock1 sBlock1;
struct SaveBlock1 *gSaveBlock1Ptr = &sBlock1;
static struct PokemonStorage sStorage;
struct SaveBlock2 *gSaveBlock2Ptr = &sBlock2;
struct PokemonStorage *gPokemonStoragePtr = &sStorage;

u16 Random(void)
{
    static u16 value = 123;
    return ++value;
}

static void BeginRun(u8 player)
{
    struct SoulLinkRunId run = {{123, 456}};
    const u32 participants[2] = {10, 20};
    SoulLink_ResetSaveData();
    assert(SoulLink_BeginRun(&run, participants, player));
    assert(!SoulLink_BeginRun(&run, participants, player));
}

static struct SoulLinkPair Pending(u16 ordinal)
{
    struct SoulLinkPair pair = {0};
    pair.areaId = 10;
    pair.state = SOUL_LINK_PAIR_PENDING;
    pair.encounterStates[0] = SOUL_LINK_ENCOUNTER_CAPTURED;
    pair.mons[0].otId = 10;
    pair.mons[0].personality = ordinal;
    pair.mons[0].species = SPECIES_BULBASAUR;
    pair.mons[0].encounterOrdinal = ordinal;
    return pair;
}

static void TestDefaultsAndConfiguration(void)
{
    struct SoulLinkConfig config, original;
    struct SoulLinkRunId run;
    memset(&sBlock2, 0, sizeof(sBlock2));
    memset(&sStorage, 0, sizeof(sStorage));
    assert(SoulLink_IsLegacySaveBlock2(&sBlock2, sizeof(sBlock2)));
    assert(!SoulLink_IsLegacySaveBlock2(NULL, sizeof(sBlock2)));
    assert(!SoulLink_IsLegacySaveBlock2(&sBlock2, 100));
    assert(SoulLink_LoadSaveData());
    assert(!SoulLink_IsEnabled());
    assert(SoulLink_GetConfig(&config));
    assert(config.linkedDeaths && config.dupesClause && config.firstEncounterOnly);
    config.enabled = TRUE;
    assert(!SoulLink_SetConfig(&config));
    BeginRun(0);
    assert(SoulLink_SetConfig(&config));
    assert(SoulLink_IsEnabled() && SoulLink_IsLinkedDeathsEnabled() && SoulLink_IsSameAreaRequired());
    config.typeClause = SOUL_LINK_TYPE_ANY;
    assert(SoulLink_SetConfig(&config));
    assert(SoulLink_IsTypeClauseEnabled());
    original = config;
    config.enabled = 2;
    assert(!SoulLink_SetConfig(&config));
    assert(SoulLink_GetConfig(&config) && memcmp(&config, &original, sizeof(config)) == 0);
    sBlock2.soulLink.header.configRevision = 0xFFFFFFFF;
    config.linkedDeaths = FALSE;
    assert(!SoulLink_SetConfig(&config));
    SoulLink_GenerateRunId(&run);
    assert(run.words[0] || run.words[1]);
    assert(!SoulLink_SetGraveBox(14));
    assert(SoulLink_SetGraveBox(13));
}

static void TestTerminalStatesAndIdentity(void)
{
    struct SoulLinkPair pair = Pending(1), restored, conflict;
    SoulLinkPairId id = 0;
    BeginRun(0);
    assert(SoulLink_CreatePair(&pair, &id) && id == 1);
    assert(SoulLink_ReadPair(0, &pair));
    conflict = Pending(2);
    conflict.mons[0].personality = pair.mons[0].personality;
    assert(!SoulLink_CreatePair(&conflict, &id));
    conflict = pair;
    conflict.areaId++;
    assert(!SoulLink_StorePair(&conflict));
    conflict = pair;
    conflict.mons[0].otId++;
    assert(!SoulLink_StorePair(&conflict));
    pair.mons[0].species = SPECIES_IVYSAUR;
    assert(SoulLink_StorePair(&pair));
    restored = pair;
    pair.encounterStates[0] = SOUL_LINK_ENCOUNTER_DEAD;
    pair.state = SOUL_LINK_PAIR_DEAD;
    pair.cleanupFlags = SOUL_LINK_CLEANUP_PLAYER_0;
    assert(SoulLink_StorePair(&pair));
    assert(!SoulLink_StorePair(&restored));
    assert(SoulLink_LoadSaveData());
    assert(SoulLink_ReadPair(0, &restored) && restored.state == SOUL_LINK_PAIR_DEAD);
    conflict = Pending(2);
    memset(&conflict.mons[0], 0, sizeof(conflict.mons[0]));
    conflict.mons[0].encounterOrdinal = 2;
    conflict.encounterStates[0] = SOUL_LINK_ENCOUNTER_FAILED;
    conflict.state = SOUL_LINK_PAIR_FAILED;
    assert(SoulLink_CreatePair(&conflict, &id));
    restored = Pending(2);
    restored.pairId = id;
    assert(!SoulLink_StorePair(&restored));
}

static void TestCapacityAndRoundTrip(void)
{
    struct SaveBlock2 savedBlock;
    struct SaveBlock1 savedBlock1;
    struct PokemonStorage savedStorage;
    struct SoulLinkPair pair;
    struct SoulLinkRunId run;
    SoulLinkPairId id;
    unsigned i;
    BeginRun(0);
    for (i = 1; i <= SOUL_LINK_PAIR_CAPACITY; i++)
    {
        pair = Pending(i);
        assert(SoulLink_CreatePair(&pair, &id) && id == i);
    }
    pair = Pending(89);
    assert(!SoulLink_CreatePair(&pair, &id));
    assert(!SoulLink_ReadPair(88, &pair));
    savedBlock = sBlock2;
    savedBlock1 = sBlock1;
    savedStorage = sStorage;
    memset(&sBlock2, 0xAA, sizeof(sBlock2));
    memset(&sBlock1, 0xAA, sizeof(sBlock1));
    memset(&sStorage, 0xAA, sizeof(sStorage));
    SoulLink_InvalidateRuntime();
    assert(!SoulLink_IsEnabled());
    sBlock2 = savedBlock;
    sBlock1 = savedBlock1;
    sStorage = savedStorage;
    assert(SoulLink_LoadSaveData());
    assert(SoulLink_GetPairCount() == SOUL_LINK_PAIR_CAPACITY);
    for (i = 0; i < SOUL_LINK_PAIR_CAPACITY; i++)
    {
        assert(SoulLink_ReadPair(i, &pair));
        assert(pair.pairId == i + 1 && pair.mons[0].personality == i + 1);
    }
    // Simulate the engine's saveblock relocation without changing runtime status.
    gSaveBlock2Ptr = &savedBlock;
    gSaveBlock1Ptr = &savedBlock1;
    gPokemonStoragePtr = &savedStorage;
    assert(SoulLink_GetRunId(&run) && run.words[0] == 123 && run.words[1] == 456);
    assert(SoulLink_ReadPair(87, &pair) && pair.pairId == 88);
    assert(SoulLink_ReadPair(30, &pair) && pair.pairId == 31);
    gSaveBlock2Ptr = &sBlock2;
    gSaveBlock1Ptr = &sBlock1;
    gPokemonStoragePtr = &sStorage;
}

static void TestCorruptionAndUnknownVersions(void)
{
    struct SaveBlock2 saved;
    struct PokemonStorage savedStorage;
    BeginRun(0);
    sBlock2.soulLink.header.version++;
    saved = sBlock2;
    savedStorage = sStorage;
    assert(!SoulLink_LoadSaveData());
    assert(SoulLink_GetSaveStatus() == SOUL_LINK_SAVE_UNSUPPORTED);
    assert(!SoulLink_IsEnabled());
    assert(memcmp(&saved, &sBlock2, sizeof(saved)) == 0);
    assert(memcmp(&savedStorage, &sStorage, sizeof(savedStorage)) == 0);
    BeginRun(0);
    sBlock2.soulLink.header.pairCount = 89;
    assert(!SoulLink_LoadSaveData() && SoulLink_GetSaveStatus() == SOUL_LINK_SAVE_CORRUPT);
    memset(&sBlock2.soulLink, 0, sizeof(sBlock2.soulLink));
    sStorage.soulLinkPairs[0].pairId = 1;
    savedStorage = sStorage;
    assert(!SoulLink_LoadSaveData());
    assert(memcmp(&savedStorage, &sStorage, sizeof(savedStorage)) == 0);
}

static void TestIdExhaustionAndCoordinator(void)
{
    struct SoulLinkPair pair = Pending(1);
    SoulLinkPairId id;
    BeginRun(1);
    assert(!SoulLink_CreatePair(&pair, &id));
    pair.pairId = 65535;
    assert(SoulLink_StorePair(&pair));
    assert(sBlock2.soulLink.header.nextPairId == 0);
    pair = Pending(2);
    pair.pairId = 3;
    assert(SoulLink_StorePair(&pair));
    assert(SoulLink_LoadSaveData());
    BeginRun(0);
    pair = Pending(1);
    pair.pairId = 65535;
    assert(SoulLink_StorePair(&pair));
    pair = Pending(2);
    assert(!SoulLink_CreatePair(&pair, &id));
}


static void TestTeamMetadataAndMigration(void)
{
    struct SoulLinkPair pair = Pending(1);
    SoulLinkPairId id = 1, ids[2] = {1, 1};
    BeginRun(0);
    sBlock2.soulLink.header.version = 2;
    assert(SoulLink_LoadSaveData());
    assert(sBlock2.soulLink.header.version == SOUL_LINK_SAVE_VERSION);
    assert(sBlock2.soulLink.header.team.count == 0 && !sBlock2.soulLink.header.team.locked);
    sBlock2.soulLink.header.version = 1;
    assert(SoulLink_LoadSaveData());
    pair.state = SOUL_LINK_PAIR_ALIVE;
    pair.encounterStates[1] = SOUL_LINK_ENCOUNTER_CAPTURED;
    pair.mons[1] = (struct SoulLinkMonIdentity){20, 201, SPECIES_CHARMANDER, 1};
    assert(SoulLink_CreatePair(&pair, &id));
    assert(SoulLink_SetTeamSelection(&id, 1));
    assert(!SoulLink_SetTeamSelection(ids, 2));
    assert(!SoulLink_SetTeamSelection(NULL, 1));
    u32 revision = sBlock2.soulLink.header.team.revision;
    assert(SoulLink_SetTeamSelection(&id, 1));
    assert(sBlock2.soulLink.header.team.revision == revision);
    sBlock2.soulLink.header.team.locked = TRUE;
    assert(SoulLink_LoadSaveData());
    assert(sBlock2.soulLink.header.team.pairIds[0] == id && sBlock2.soulLink.header.team.locked);
    sBlock2.soulLink.header.team.pairIds[1] = 2; // Nonzero unused roster entry.
    assert(!SoulLink_LoadSaveData());
    BeginRun(0);sBlock2.soulLink.header.version = 2;sBlock2.soulLink.header.team.locked = TRUE;
    assert(!SoulLink_LoadSaveData()); // Older versions require untouched reserve bytes.
}

static void TestBankIdentityLookup(void)
{
    unsigned checks = 0;
    for (u8 player = 0; player < 2; player++)
        for (u16 count = 0; count <= SOUL_LINK_PAIR_CAPACITY; count++)
        {
            BeginRun(player);
            for (u16 i = 0; i < count; i++)
            {
                struct SoulLinkPair row = {0};
                row.pairId = i + 1; row.areaId = 500 + i; row.state = SOUL_LINK_PAIR_ALIVE;
                for (u8 side = 0; side < 2; side++)
                {
                    row.encounterStates[side] = SOUL_LINK_ENCOUNTER_CAPTURED;
                    row.mons[side] = (struct SoulLinkMonIdentity){10 + side, 1000 + i, SPECIES_BULBASAUR, i + 1};
                }
                assert(SoulLink_StorePair(&row));
            }
            struct SaveBlock1 before1 = sBlock1;
            struct SaveBlock2 before2 = sBlock2;
            struct PokemonStorage beforePC = sStorage;
            for (u16 i = 0; i < SOUL_LINK_PAIR_CAPACITY; i++)
            {
                struct SoulLinkMonIdentity key = {10 + player, 1000 + i, SPECIES_CHARMANDER, i + 1};
                struct SoulLinkPair output, sentinel;
                memset(&sentinel, 0xA5, sizeof(sentinel)); output = sentinel;
                u16 index = 0xBEEF;
                assert(SoulLink_FindLocalPair(&key, &index, &output) == (i < count));
                if (i < count)
                {
                    struct SoulLinkPair expected;
                    assert(SoulLink_ReadPair(i, &expected));
                    assert(index == i && !memcmp(&output, &expected, sizeof(output)));
                }
                else assert(index == 0xBEEF && !memcmp(&output, &sentinel, sizeof(output)));
                key.otId += 100; assert(!SoulLink_FindLocalPair(&key, NULL, NULL));
                key.otId -= 100; key.personality += 10000; assert(!SoulLink_FindLocalPair(&key, NULL, NULL));
                checks += 3;
            }
            assert(!memcmp(&sBlock1, &before1, sizeof(sBlock1)));
            assert(!memcmp(&sBlock2, &before2, sizeof(sBlock2)));
            assert(!memcmp(&sStorage, &beforePC, sizeof(sStorage)));
        }
    printf("PASS save-bank identity lookup: %u exact hits/misses across every count and both roles, unchanged save bytes and untouched missing outputs\n", checks);
}

int main(void)
{
    TestBankIdentityLookup();
    TestTeamMetadataAndMigration();
    TestDefaultsAndConfiguration();
    TestTerminalStatesAndIdentity();
    TestCapacityAndRoundTrip();
    TestCorruptionAndUnknownVersions();
    TestIdExhaustionAndCoordinator();
    puts("Soul Link persistence: all model tests passed");
    return 0;
}
