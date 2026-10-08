#include <assert.h>
#include <stdio.h>
#include "global.h"
#include "soul_link.h"
#include "pokemon_storage_system.h"

// Exercise the real slot reader/writer; only hardware and party deserialization are stubbed.
#include "../../src/save.c"

static struct SaveBlock1 sBlock1;
static struct SaveBlock2 sBlock2;
static struct PokemonStorage sStorage;
static struct SaveSector sFlash[SECTORS_COUNT];
struct SaveBlock1 *gSaveBlock1Ptr = &sBlock1;
struct SaveBlock2 *gSaveBlock2Ptr = &sBlock2;
struct PokemonStorage *gPokemonStoragePtr = &sStorage;
bool32 gFlashMemoryPresent = TRUE;
u8 gDecompressionBuffer[0x4000];
struct Main gMain;

u16 Random(void)
{
    return 123;
}

void LoadSerializedGame(void)
{
}

void ReadFlash(u16 sectorNum, u32 offset, void *dest, u32 size)
{
    assert(sectorNum < SECTORS_COUNT && offset + size <= SECTOR_SIZE);
    memcpy(dest, (u8 *)&sFlash[sectorNum] + offset, size);
}

u32 ProgramFlashSectorAndVerify(u16 sectorNum, u8 *src)
{
    assert(sectorNum < SECTORS_COUNT);
    memcpy(&sFlash[sectorNum], src, SECTOR_SIZE);
    return 0;
}

static void ClearRam(void)
{
    memset(&sBlock1, 0, sizeof(sBlock1));
    memset(&sBlock2, 0, sizeof(sBlock2));
    memset(&sStorage, 0, sizeof(sStorage));
    Save_ResetSaveCounters();
    SoulLink_InvalidateRuntime();
}

static void MakeLegacySlot(u16 start, u32 counter, u16 rotation)
{
    unsigned id;
    UpdateSaveAddresses();
    for (id = 0; id < NUM_SECTORS_PER_SLOT; id++)
    {
        struct SaveSector *sector = &sFlash[start + (id + rotation) % NUM_SECTORS_PER_SLOT];
        u16 size = gRamSaveSectorLocations[id].size;
        if (id == SECTOR_ID_PKMN_STORAGE_END)
            size = SOUL_LINK_LEGACY_STORAGE_SIZE - 8 * SECTOR_DATA_SIZE;
        memset(sector, 0, sizeof(*sector));
        memcpy(sector->data, gRamSaveSectorLocations[id].data, size);
        // Legacy readers ignore this region. The migrated runtime must never import it.
        if (id == SECTOR_ID_PKMN_STORAGE_END)
            memset(sector->data + size, 0xA5, SECTOR_DATA_SIZE - size);
        sector->id = id;
        sector->counter = counter;
        sector->signature = SECTOR_SIGNATURE;
        sector->checksum = CalculateChecksum(sector->data, size);
    }
}

static void TestLegacyMigration(void)
{
    struct PokemonStorage legacyStorage;
    struct SaveBlock1 legacyBlock1;
    memset(sFlash, 0xFF, sizeof(sFlash));
    ClearRam();
    memset(sBlock1.legacyRest, 0x91, sizeof(sBlock1.legacyRest));
    memset(sStorage.legacyStorage, 0x34, sizeof(sStorage.legacyStorage));
    legacyBlock1 = sBlock1;
    legacyStorage = sStorage;
    MakeLegacySlot(0, 6, 5);
    ClearRam();
    memset(sStorage.soulLinkPairs, 0xCC, sizeof(sStorage.soulLinkPairs));
    assert(LoadGameSave(SAVE_NORMAL) == SAVE_STATUS_OK);
    assert(SoulLink_GetSaveStatus() == SOUL_LINK_SAVE_READY && !SoulLink_IsEnabled());
    assert(memcmp(&sBlock1, &legacyBlock1, sizeof(sBlock1)) == 0);
    assert(memcmp(&sStorage, &legacyStorage, SOUL_LINK_LEGACY_STORAGE_SIZE) == 0);
    assert(sStorage.soulLinkPairs[0].pairId == 0 && sStorage.soulLinkPairs[SOUL_LINK_OVERFLOW_PAIR_COUNT - 1].pairId == 0);
}

static void TestRealFlashRoundTrip(void)
{
    struct SaveBlock1 expectedBlock1;
    struct SaveBlock2 expectedBlock2;
    struct PokemonStorage expectedStorage;
    struct SoulLinkRunId run = {{123, 456}};
    u32 participants[2] = {1, 2};
    struct SoulLinkConfig config;
    struct SoulLinkPair pair;
    SoulLinkPairId id;
    unsigned i;
    assert(SoulLink_BeginRun(&run, participants, 0));
    assert(SoulLink_GetConfig(&config));
    config.enabled = TRUE;
    assert(SoulLink_SetConfig(&config));
    for (i = 1; i <= SOUL_LINK_PAIR_CAPACITY; i++)
    {
        memset(&pair, 0, sizeof(pair));
        pair.areaId = i;
        pair.mons[0].otId = 1;
        pair.mons[0].personality = i;
        pair.mons[0].species = SPECIES_BULBASAUR;
        pair.mons[0].encounterOrdinal = i;
        pair.encounterStates[0] = SOUL_LINK_ENCOUNTER_CAPTURED;
        pair.state = SOUL_LINK_PAIR_PENDING;
        assert(SoulLink_CreatePair(&pair, &id));
    }
    assert(SoulLink_ReadPair(87, &pair));
    pair.state = SOUL_LINK_PAIR_DEAD;
    pair.encounterStates[0] = SOUL_LINK_ENCOUNTER_DEAD;
    pair.cleanupFlags = SOUL_LINK_CLEANUP_PLAYER_0;
    assert(SoulLink_StorePair(&pair));
    expectedBlock2 = sBlock2;
    expectedBlock1 = sBlock1;
    expectedStorage = sStorage;
    UpdateSaveAddresses();
    assert(WriteSaveSectorOrSlot(FULL_SAVE_SLOT, gRamSaveSectorLocations) == SAVE_STATUS_OK);
    ClearRam();
    assert(LoadGameSave(SAVE_NORMAL) == SAVE_STATUS_OK);
    assert(SoulLink_IsEnabled() && SoulLink_GetPairCount() == 88);
    assert(memcmp(&sBlock2, &expectedBlock2, sizeof(sBlock2)) == 0);
    assert(memcmp(&sBlock1, &expectedBlock1, sizeof(sBlock1)) == 0);
    assert(memcmp(&sStorage, &expectedStorage, sizeof(sStorage)) == 0);
    assert(SoulLink_ReadPair(87, &pair) && pair.state == SOUL_LINK_PAIR_DEAD && pair.cleanupFlags == 1);
}

static void TestUnknownVersion(void)
{
    struct SaveBlock2 expected;
    sBlock2.soulLink.header.version++;
    expected = sBlock2;
    UpdateSaveAddresses();
    assert(WriteSaveSectorOrSlot(FULL_SAVE_SLOT, gRamSaveSectorLocations) == SAVE_STATUS_OK);
    ClearRam();
    assert(LoadGameSave(SAVE_NORMAL) == SAVE_STATUS_OK);
    assert(SoulLink_GetSaveStatus() == SOUL_LINK_SAVE_UNSUPPORTED && !SoulLink_IsEnabled());
    assert(memcmp(&expected, &sBlock2, sizeof(expected)) == 0);
}

static void TestInvalidSectorIdAndChecksum(void)
{
    unsigned i;
    u16 currentSlot = NUM_SECTORS_PER_SLOT * (gSaveCounter % NUM_SAVE_SLOTS);
    for (i = 0; i < NUM_SECTORS_PER_SLOT; i++)
    {
        struct SaveSector *sector = &sFlash[currentSlot + i];
        if (sector->id == SECTOR_ID_PKMN_STORAGE_END)
        {
            sector->id = 0xFFFF;
            break;
        }
    }
    ClearRam();
    assert(LoadGameSave(SAVE_NORMAL) == SAVE_STATUS_ERROR);
    assert(SoulLink_GetSaveStatus() == SOUL_LINK_SAVE_READY);
    currentSlot = NUM_SECTORS_PER_SLOT * (gSaveCounter % NUM_SAVE_SLOTS);
    for (i = 0; i < NUM_SECTORS_PER_SLOT; i++)
    {
        struct SaveSector *sector = &sFlash[currentSlot + i];
        if (sector->id == SECTOR_ID_PKMN_STORAGE_END)
            sector->checksum++;
    }
    ClearRam();
    assert(LoadGameSave(SAVE_NORMAL) == SAVE_STATUS_INVALID);
    assert(SoulLink_GetSaveStatus() == SOUL_LINK_SAVE_UNLOADED);
}

int main(void)
{
    TestLegacyMigration();
    TestRealFlashRoundTrip();
    TestUnknownVersion();
    TestInvalidSectorIdAndChecksum();
    SoulLink_ResetSaveData();
    gFlashMemoryPresent = FALSE;
    assert(LoadGameSave(SAVE_NORMAL) == SAVE_STATUS_ERROR);
    assert(gSaveFileStatus == SAVE_STATUS_NO_FLASH);
    assert(SoulLink_GetSaveStatus() == SOUL_LINK_SAVE_UNLOADED);
    puts("Soul Link persistence: real flash reader/writer tests passed");
    return 0;
}
