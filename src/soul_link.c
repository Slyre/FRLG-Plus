#include "global.h"
#include "pokemon_storage_system.h"
#include "random.h"
#include "soul_link.h"

STATIC_ASSERT(sizeof(struct SoulLinkTeamSelection) == 20, SoulLinkTeamSize);
STATIC_ASSERT(sizeof(struct SoulLinkConfig) == 8, SoulLinkConfigSize);
STATIC_ASSERT(sizeof(struct SoulLinkPair) == 32, SoulLinkPairSize);
STATIC_ASSERT(sizeof(struct SoulLinkSaveHeader) == 64, SoulLinkHeaderSize);
STATIC_ASSERT(sizeof(struct SoulLinkSave) == 1024, SoulLinkSaveSize);

static EWRAM_DATA u8 sSaveStatus = SOUL_LINK_SAVE_UNLOADED;

static struct SoulLinkSaveHeader *GetHeader(void)
{
    return &gSaveBlock2Ptr->soulLink.header;
}

static struct SoulLinkPair *GetPair(u16 index)
{
    if (index < SOUL_LINK_PRIMARY_PAIR_COUNT)
        return &gSaveBlock2Ptr->soulLink.pairs[index];
    index -= SOUL_LINK_PRIMARY_PAIR_COUNT;
    if (index < 2)
        return &gSaveBlock1Ptr->soulLinkItemReservePairs[index];
    index -= 2;
    if (index < 2)
        return &gSaveBlock1Ptr->soulLinkOldManReservePairs[index];
    index -= 2;
    if (index < 1)
        return &gSaveBlock1Ptr->soulLinkEasyChatReservePairs[index];
    index -= 1;
    if (index < 5)
        return &gSaveBlock1Ptr->soulLinkFreeWorkPairs[index];
    index -= 5;
    if (index < 1)
        return &gSaveBlock1Ptr->soulLinkFameReservePairs[index];
    return &gPokemonStoragePtr->soulLinkPairs[index - 1];
}

static bool8 IsZero(const void *data, u32 size)
{
    const u8 *bytes = data;
    u32 i;
    for (i = 0; i < size; i++)
        if (bytes[i] != 0)
            return FALSE;
    return TRUE;
}

static bool8 IsRunIdSet(const struct SoulLinkRunId *runId)
{
    return runId->words[0] != 0 || runId->words[1] != 0;
}

static bool8 IsConfigValid(const struct SoulLinkConfig *config)
{
    return config->enabled <= TRUE
        && config->linkedDeaths <= TRUE
        && config->sameAreaPairing <= TRUE
        && config->firstEncounterOnly <= TRUE
        && config->dupesClause <= TRUE
        && config->typeClause < SOUL_LINK_TYPE_COUNT
        && config->enforcePartyPairs <= TRUE
        && config->reserved == 0;
}

static u8 GetPairState(const struct SoulLinkPair *pair)
{
    if (pair->encounterStates[0] == SOUL_LINK_ENCOUNTER_DEAD
     || pair->encounterStates[1] == SOUL_LINK_ENCOUNTER_DEAD)
        return SOUL_LINK_PAIR_DEAD;
    if (pair->encounterStates[0] == SOUL_LINK_ENCOUNTER_FAILED
     || pair->encounterStates[1] == SOUL_LINK_ENCOUNTER_FAILED)
        return SOUL_LINK_PAIR_FAILED;
    if (pair->encounterStates[0] == SOUL_LINK_ENCOUNTER_CAPTURED
     && pair->encounterStates[1] == SOUL_LINK_ENCOUNTER_CAPTURED)
        return SOUL_LINK_PAIR_ALIVE;
    return SOUL_LINK_PAIR_PENDING;
}

static bool8 IsPairValidForPlayer(const struct SoulLinkPair *pair, u8 localPlayer)
{
    u32 i;
    if (pair->areaId == SOUL_LINK_AREA_NONE
     || pair->areaId == 0xFFFF || pair->cleanupFlags & ~SOUL_LINK_CLEANUP_MASK)
        return FALSE;
    if (pair->pairId == SOUL_LINK_PAIR_NONE
     && (localPlayer != 1 || pair->encounterStates[0] != SOUL_LINK_ENCOUNTER_NONE))
        return FALSE;
    if (pair->encounterStates[0] == SOUL_LINK_ENCOUNTER_NONE
     && pair->encounterStates[1] == SOUL_LINK_ENCOUNTER_NONE)
        return FALSE;
    if (pair->state != GetPairState(pair))
        return FALSE;
    for (i = 0; i < SOUL_LINK_PLAYER_COUNT; i++)
    {
        const struct SoulLinkMonIdentity *mon = &pair->mons[i];
        u8 state = pair->encounterStates[i];
        if (state > SOUL_LINK_ENCOUNTER_DEAD)
            return FALSE;
        if (state == SOUL_LINK_ENCOUNTER_NONE)
        {
            if (!IsZero(mon, sizeof(*mon)))
                return FALSE;
        }
        else if (mon->encounterOrdinal == 0)
            return FALSE;
        if ((state == SOUL_LINK_ENCOUNTER_CAPTURED || state == SOUL_LINK_ENCOUNTER_DEAD)
         && (mon->species == SPECIES_NONE || mon->species >= NUM_SPECIES || mon->species == SPECIES_EGG))
            return FALSE;
        if (state == SOUL_LINK_ENCOUNTER_FAILED
         && (mon->species != SPECIES_NONE || mon->otId != 0 || mon->personality != 0))
            return FALSE;
        if ((pair->cleanupFlags & (1 << i)) && state != SOUL_LINK_ENCOUNTER_DEAD)
            return FALSE;
    }
    return TRUE;
}

static bool8 IsPairValid(const struct SoulLinkPair *pair)
{
    return IsPairValidForPlayer(pair, GetHeader()->localPlayer);
}

static bool8 SameMon(const struct SoulLinkMonIdentity *a, const struct SoulLinkMonIdentity *b)
{
    return a->otId == b->otId && a->personality == b->personality;
}

static bool8 AreasMatch(u16 a, u16 b);

static u16 FindOpenPair(u16 area, u8 player)
{
    u16 i, best = SOUL_LINK_PAIR_CAPACITY;
    for (i = 0; i < GetHeader()->pairCount; i++)
    {
        const struct SoulLinkPair *pair = GetPair(i);
        if (pair->encounterStates[player] == SOUL_LINK_ENCOUNTER_NONE && AreasMatch(pair->areaId, area)
         && (best == SOUL_LINK_PAIR_CAPACITY
          || pair->mons[player ^ 1].encounterOrdinal < GetPair(best)->mons[player ^ 1].encounterOrdinal
          || (pair->mons[player ^ 1].encounterOrdinal == GetPair(best)->mons[player ^ 1].encounterOrdinal
           && pair->pairId < GetPair(best)->pairId)))
            best = i;
    }
    return best;
}

static bool8 HasConflict(const struct SoulLinkPair *pair, u16 skipIndex)
{
    u32 i, player;
    for (i = 0; i < GetHeader()->pairCount; i++)
    {
        const struct SoulLinkPair *existing = GetPair(i);
        if (i == skipIndex)
            continue;
        if (pair->pairId != SOUL_LINK_PAIR_NONE && existing->pairId == pair->pairId)
            return TRUE;
        for (player = 0; player < SOUL_LINK_PLAYER_COUNT; player++)
        {
            if (pair->mons[player].species != SPECIES_NONE
             && existing->mons[player].species != SPECIES_NONE
             && SameMon(&pair->mons[player], &existing->mons[player]))
                return TRUE;
            if (pair->encounterStates[player] != SOUL_LINK_ENCOUNTER_NONE
             && existing->encounterStates[player] != SOUL_LINK_ENCOUNTER_NONE
             && pair->mons[player].encounterOrdinal == existing->mons[player].encounterOrdinal)
                return TRUE;
        }
    }
    return FALSE;
}

void SoulLink_ResetSaveData(void)
{
    struct SoulLinkSaveHeader *header;
    u32 i;
    memset(&gSaveBlock2Ptr->soulLink, 0, sizeof(gSaveBlock2Ptr->soulLink));
    for (i = SOUL_LINK_PRIMARY_PAIR_COUNT; i < SOUL_LINK_PAIR_CAPACITY; i++)
        memset(GetPair(i), 0, sizeof(struct SoulLinkPair));
    header = GetHeader();
    header->magic = SOUL_LINK_SAVE_MAGIC;
    header->version = SOUL_LINK_SAVE_VERSION;
    header->areaVersion = SOUL_LINK_AREA_VERSION;
    header->nextPairId = 1;
    header->localPlayer = SOUL_LINK_PLAYER_NONE;
    header->graveBox = SOUL_LINK_BOX_NONE;
    header->config.linkedDeaths = TRUE;
    header->config.sameAreaPairing = TRUE;
    header->config.firstEncounterOnly = TRUE;
    header->config.dupesClause = TRUE;
    sSaveStatus = SOUL_LINK_SAVE_READY;
}

void SoulLink_InvalidateRuntime(void)
{
    sSaveStatus = SOUL_LINK_SAVE_UNLOADED;
}

bool8 SoulLink_IsLegacySaveBlock2(const void *data, u16 size)
{
    u32 offset = offsetof(struct SaveBlock2, soulLink);
    if (data == NULL || size < offset + sizeof(struct SoulLinkSave))
        return FALSE;
    return IsZero((const u8 *)data + offset, sizeof(struct SoulLinkSave));
}

bool8 SoulLink_LoadSaveData(void)
{
    struct SoulLinkSaveHeader *header = GetHeader();
    u32 i;
    sSaveStatus = SOUL_LINK_SAVE_CORRUPT;
    // Only the untouched FRLG+ reserve is eligible for initialization.
    if (IsZero(&gSaveBlock2Ptr->soulLink, sizeof(gSaveBlock2Ptr->soulLink))
     && IsZero(gPokemonStoragePtr->soulLinkPairs, sizeof(gPokemonStoragePtr->soulLinkPairs)))
    {
        for (i = SOUL_LINK_PRIMARY_PAIR_COUNT; i < SOUL_LINK_PRIMARY_PAIR_COUNT + SOUL_LINK_SECONDARY_PAIR_COUNT; i++)
            if (!IsZero(GetPair(i), sizeof(struct SoulLinkPair)))
                return FALSE;
        SoulLink_ResetSaveData();
        return TRUE;
    }
    if (header->magic != SOUL_LINK_SAVE_MAGIC || (header->version != SOUL_LINK_SAVE_VERSION && header->version != 1 && header->version != 2)
     || header->areaVersion != SOUL_LINK_AREA_VERSION)
    {
        sSaveStatus = SOUL_LINK_SAVE_UNSUPPORTED;
        return FALSE;
    }
    if (!IsConfigValid(&header->config) || header->pairCount > SOUL_LINK_PAIR_CAPACITY
     || header->reservedFlags != 0
     || (header->version < 3 && !IsZero(&header->team, sizeof(header->team)))
     || header->team.count > 6 || header->team.locked > TRUE || header->team.reserved != 0
     || (header->graveBox != SOUL_LINK_BOX_NONE && header->graveBox >= TOTAL_BOXES_COUNT))
        return FALSE;
    if (IsRunIdSet(&header->runId))
    {
        if (header->localPlayer >= SOUL_LINK_PLAYER_COUNT || header->participantIds[0] == 0
         || header->participantIds[1] == 0 || header->participantIds[0] == header->participantIds[1])
            return FALSE;
    }
    else if (header->localPlayer != SOUL_LINK_PLAYER_NONE || header->participantIds[0] != 0
          || header->participantIds[1] != 0 || header->pairCount != 0 || header->config.enabled)
        return FALSE;
    for (i = 0; i < header->pairCount; i++)
    {
        const struct SoulLinkPair *pair = GetPair(i);
        if (!IsPairValid(pair) || HasConflict(pair, i)
         || (header->version == 1 && pair->pairId == SOUL_LINK_PAIR_NONE)
         || (header->nextPairId != 0 && pair->pairId >= header->nextPairId))
            return FALSE;
    }
    for (i = 0; i < 6; i++)
    {
        u16 j;
        if (i >= header->team.count)
        {
            if (header->team.pairIds[i] != 0)
                return FALSE;
            continue;
        }
        if (header->team.pairIds[i] == 0 || (i && header->team.pairIds[i] <= header->team.pairIds[i - 1]))
            return FALSE;
        for (j = 0; j < header->pairCount; j++)
            if (GetPair(j)->pairId == header->team.pairIds[i])
                break;
        if (j == header->pairCount)
            return FALSE;
    }
    sSaveStatus = SOUL_LINK_SAVE_READY;
    header->version = SOUL_LINK_SAVE_VERSION;
    return TRUE;
}

u8 SoulLink_GetSaveStatus(void)
{
    return sSaveStatus;
}

bool8 SoulLink_IsEnabled(void)
{
    return sSaveStatus == SOUL_LINK_SAVE_READY && GetHeader()->config.enabled;
}

bool8 SoulLink_IsLinkedDeathsEnabled(void)
{
    return SoulLink_IsEnabled() && GetHeader()->config.linkedDeaths;
}

bool8 SoulLink_IsSameAreaRequired(void)
{
    return SoulLink_IsEnabled() && GetHeader()->config.sameAreaPairing;
}

bool8 SoulLink_IsTypeClauseEnabled(void)
{
    return SoulLink_IsEnabled() && GetHeader()->config.typeClause != SOUL_LINK_TYPE_NONE;
}

bool8 SoulLink_ArePartyChangesLocked(void)
{
    return SoulLink_IsEnabled() && GetHeader()->config.enforcePartyPairs && GetHeader()->team.locked;
}

bool8 SoulLink_SetTeamSelection(const SoulLinkPairId *ids, u8 count)
{
    struct SoulLinkSaveHeader *header = GetHeader();
    u8 i;
    u16 j;
    if (sSaveStatus != SOUL_LINK_SAVE_READY || count > 6 || (count && ids == NULL))
        return FALSE;
    for (i = 0; i < count; i++)
    {
        if (ids[i] == 0 || (i && ids[i] <= ids[i - 1]))
            return FALSE;
        for (j = 0; j < header->pairCount; j++)
            if (GetPair(j)->pairId == ids[i] && GetPair(j)->state == SOUL_LINK_PAIR_ALIVE)
                break;
        if (j == header->pairCount)
            return FALSE;
    }
    if (count == header->team.count && (!count || !memcmp(ids, header->team.pairIds, count * sizeof(*ids))))
        return TRUE;
    if (header->team.revision == 0xFFFFFFFF)
        return FALSE;
    memset(header->team.pairIds, 0, sizeof(header->team.pairIds));
    if (count)
        memcpy(header->team.pairIds, ids, count * sizeof(*ids));
    header->team.count = count;
    header->team.revision++;
    return TRUE;
}

bool8 SoulLink_GetConfig(struct SoulLinkConfig *config)
{
    if (config == NULL || sSaveStatus != SOUL_LINK_SAVE_READY)
        return FALSE;
    *config = GetHeader()->config;
    return TRUE;
}

bool8 SoulLink_SetConfig(const struct SoulLinkConfig *config)
{
    struct SoulLinkSaveHeader *header = GetHeader();
    if (config == NULL || sSaveStatus != SOUL_LINK_SAVE_READY || !IsConfigValid(config)
     || (config->enabled && !IsRunIdSet(&header->runId)))
        return FALSE;
    if (memcmp(config, &header->config, sizeof(*config)) == 0)
        return TRUE;
    if (header->configRevision == 0xFFFFFFFF)
        return FALSE;
    header->config = *config;
    header->configRevision++;
    return TRUE;
}

void SoulLink_GenerateRunId(struct SoulLinkRunId *runId)
{
    if (runId == NULL)
        return;
    runId->words[0] = ((u32)Random() << 16) | Random();
    runId->words[1] = ((u32)Random() << 16) | Random();
    if (!IsRunIdSet(runId))
        runId->words[0] = 1;
}

bool8 SoulLink_BeginRun(const struct SoulLinkRunId *runId, const u32 *participantIds, u8 localPlayer)
{
    struct SoulLinkSaveHeader *header = GetHeader();
    if (sSaveStatus != SOUL_LINK_SAVE_READY || runId == NULL || participantIds == NULL
     || !IsRunIdSet(runId) || localPlayer >= SOUL_LINK_PLAYER_COUNT
     || participantIds[0] == 0 || participantIds[1] == 0 || participantIds[0] == participantIds[1]
     || IsRunIdSet(&header->runId))
        return FALSE;
    header->runId = *runId;
    header->participantIds[0] = participantIds[0];
    header->participantIds[1] = participantIds[1];
    header->localPlayer = localPlayer;
    return TRUE;
}

bool8 SoulLink_GetRunId(struct SoulLinkRunId *runId)
{
    if (runId == NULL || sSaveStatus != SOUL_LINK_SAVE_READY || !IsRunIdSet(&GetHeader()->runId))
        return FALSE;
    *runId = GetHeader()->runId;
    return TRUE;
}

u16 SoulLink_GetPairCount(void)
{
    return sSaveStatus == SOUL_LINK_SAVE_READY ? GetHeader()->pairCount : 0;
}

bool8 SoulLink_ReadPair(u16 index, struct SoulLinkPair *pair)
{
    if (pair == NULL || sSaveStatus != SOUL_LINK_SAVE_READY || index >= GetHeader()->pairCount)
        return FALSE;
    *pair = *GetPair(index);
    return TRUE;
}

static bool8 CanUpdatePair(const struct SoulLinkPair *old, const struct SoulLinkPair *pair)
{
    u32 player;
    if (old->areaId != pair->areaId || (old->state == SOUL_LINK_PAIR_DEAD && pair->state != SOUL_LINK_PAIR_DEAD)
     || (old->state == SOUL_LINK_PAIR_FAILED && pair->state != SOUL_LINK_PAIR_FAILED && pair->state != SOUL_LINK_PAIR_DEAD))
        return FALSE;
    for (player = 0; player < SOUL_LINK_PLAYER_COUNT; player++)
    {
        u8 before = old->encounterStates[player];
        u8 after = pair->encounterStates[player];
        if (before != SOUL_LINK_ENCOUNTER_NONE && before != after
         && !(before == SOUL_LINK_ENCOUNTER_CAPTURED && after == SOUL_LINK_ENCOUNTER_DEAD))
            return FALSE;
        if (before != SOUL_LINK_ENCOUNTER_NONE
         && old->mons[player].encounterOrdinal != pair->mons[player].encounterOrdinal)
            return FALSE;
        if (old->mons[player].species != SPECIES_NONE && !SameMon(&old->mons[player], &pair->mons[player]))
            return FALSE;
    }
    return TRUE;
}

bool8 SoulLink_StorePair(const struct SoulLinkPair *pair)
{
    struct SoulLinkSaveHeader *header = GetHeader();
    u16 index;
    if (pair == NULL || sSaveStatus != SOUL_LINK_SAVE_READY || !IsRunIdSet(&header->runId) || !IsPairValid(pair))
        return FALSE;
    for (index = 0; index < header->pairCount; index++)
        if (GetPair(index)->pairId == pair->pairId
         && (pair->pairId != SOUL_LINK_PAIR_NONE
          || GetPair(index)->mons[header->localPlayer].encounterOrdinal == pair->mons[header->localPlayer].encounterOrdinal))
            break;
    if (index == header->pairCount)
    {
        if (index == SOUL_LINK_PAIR_CAPACITY)
            return FALSE;
    }
    else if (!CanUpdatePair(GetPair(index), pair))
        return FALSE;
    if (HasConflict(pair, index))
        return FALSE;
    *GetPair(index) = *pair;
    if (index == header->pairCount)
    {
        header->pairCount++;
        if (header->nextPairId != 0 && pair->pairId >= header->nextPairId)
            header->nextPairId = pair->pairId + 1;
    }
    return TRUE;
}

bool8 SoulLink_CreatePair(const struct SoulLinkPair *pair, SoulLinkPairId *pairId)
{
    struct SoulLinkPair candidate;
    if (pair == NULL || pairId == NULL || sSaveStatus != SOUL_LINK_SAVE_READY
     || GetHeader()->localPlayer != 0 || GetHeader()->nextPairId == SOUL_LINK_PAIR_NONE)
        return FALSE;
    candidate = *pair;
    candidate.pairId = GetHeader()->nextPairId;
    if (!SoulLink_StorePair(&candidate))
        return FALSE;
    *pairId = candidate.pairId;
    return TRUE;
}

bool8 SoulLink_SetGraveBox(u8 boxId)
{
    if (sSaveStatus != SOUL_LINK_SAVE_READY || (boxId != SOUL_LINK_BOX_NONE && boxId >= TOTAL_BOXES_COUNT))
        return FALSE;
    GetHeader()->graveBox = boxId;
    return TRUE;
}

bool8 SoulLink_RecordLocalEncounter(u16 areaId, const struct SoulLinkMonIdentity *mon, u8 result)
{
    struct SoulLinkPair pair;
    struct SoulLinkSaveHeader *header = GetHeader();
    u32 i;
    u16 ordinal = 0;
    u8 player = header->localPlayer;
    SoulLinkPairId id;
    if (!SoulLink_IsEnabled() || player >= SOUL_LINK_PLAYER_COUNT
     || (result != SOUL_LINK_ENCOUNTER_CAPTURED && result != SOUL_LINK_ENCOUNTER_FAILED)
     || (result == SOUL_LINK_ENCOUNTER_CAPTURED && mon == NULL))
        return FALSE;
    for (i = 0; i < header->pairCount; i++)
        if (GetPair(i)->mons[player].encounterOrdinal > ordinal)
            ordinal = GetPair(i)->mons[player].encounterOrdinal;
    if (ordinal == 0xFFFF)
        return FALSE;
    memset(&pair, 0, sizeof(pair));
    pair.areaId = areaId;
    pair.encounterStates[player] = result;
    if (mon != NULL && result == SOUL_LINK_ENCOUNTER_CAPTURED)
        pair.mons[player] = *mon;
    pair.mons[player].encounterOrdinal = ordinal + 1;
    pair.state = GetPairState(&pair);
    i = FindOpenPair(areaId, player);
    if (i != SOUL_LINK_PAIR_CAPACITY)
    {
        struct SoulLinkPair existing = *GetPair(i);
        existing.mons[player] = pair.mons[player];
        existing.encounterStates[player] = result;
        existing.state = GetPairState(&existing);
        return SoulLink_StorePair(&existing);
    }
    if (player == 0)
        return SoulLink_CreatePair(&pair, &id);
    return SoulLink_StorePair(&pair);
}

bool8 SoulLink_CanRecordLocalEncounter(u16 areaId)
{
    struct SoulLinkSaveHeader *header = GetHeader();
    u16 i;
    bool8 vacant = FALSE;
    if (!SoulLink_IsEnabled() || areaId == SOUL_LINK_AREA_NONE || areaId == 0xFFFF)
        return FALSE;
    for (i = 0; i < header->pairCount; i++)
    {
        const struct SoulLinkPair *pair = GetPair(i);
        if (pair->mons[header->localPlayer].encounterOrdinal == 0xFFFF)
            return FALSE;
        if (pair->encounterStates[header->localPlayer] == SOUL_LINK_ENCOUNTER_NONE
         && (pair->areaId == areaId || (!header->config.sameAreaPairing
          && pair->areaId != SOUL_LINK_AREA_STARTER && areaId != SOUL_LINK_AREA_STARTER)))
            vacant = TRUE;
    }
    return vacant || (header->pairCount < SOUL_LINK_PAIR_CAPACITY
        && (header->localPlayer != 0 || header->nextPairId != SOUL_LINK_PAIR_NONE));
}

bool8 SoulLink_IsLegacyShedinja(const struct SoulLinkMonIdentity *mon)
{
    struct SoulLinkMonIdentity parent;
    struct SoulLinkPair pair;
    u16 species;
    if (mon == NULL || mon->species != SPECIES_SHEDINJA || !SoulLink_IsEnabled())
        return FALSE;
    parent = *mon;
    parent.species = SPECIES_NINJASK;
    if (!SoulLink_FindLocalPair(&parent, NULL, &pair))
        return FALSE;
    species = pair.mons[GetHeader()->localPlayer].species;
    return species == SPECIES_NINCADA || species == SPECIES_NINJASK;
}

bool8 SoulLink_FindLocalPair(const struct SoulLinkMonIdentity *mon, u16 *index, struct SoulLinkPair *pair)
{
    static const u16 bankEnds[] = {30, 32, 34, 35, 40, 41, SOUL_LINK_PAIR_CAPACITY};
    u16 i = 0, bank, count = GetHeader()->pairCount;
    u8 player = GetHeader()->localPlayer;
    if (mon == NULL || sSaveStatus != SOUL_LINK_SAVE_READY || player >= SOUL_LINK_PLAYER_COUNT
     || mon->species == SPECIES_NONE || mon->species == SPECIES_SHEDINJA)
        return FALSE;
    // Each save bank is contiguous; resolve its first address once. Never
    // advance a pair pointer across the boundary into unrelated save fields.
    for (bank = 0; bank < ARRAY_COUNT(bankEnds) && i < count; bank++)
    {
        const struct SoulLinkPair *candidate = GetPair(i);
        u16 end = min(count, bankEnds[bank]);
        for (; i < end; i++, candidate++)
        {
            if (candidate->mons[player].species != SPECIES_NONE && SameMon(mon, &candidate->mons[player]))
            {
                if (index != NULL)
                    *index = i;
                if (pair != NULL)
                    *pair = *candidate;
                return TRUE;
            }
        }
    }
    return FALSE;
}

bool8 SoulLink_MarkLocalDeath(const struct SoulLinkMonIdentity *mon)
{
    struct SoulLinkPair pair;
    u8 player = GetHeader()->localPlayer;
    if (!SoulLink_IsEnabled() || !SoulLink_FindLocalPair(mon, NULL, &pair))
        return FALSE;
    if (pair.encounterStates[player] == SOUL_LINK_ENCOUNTER_DEAD)
        return TRUE;
    pair.encounterStates[player] = SOUL_LINK_ENCOUNTER_DEAD;
    if (SoulLink_IsLinkedDeathsEnabled() && pair.encounterStates[player ^ 1] == SOUL_LINK_ENCOUNTER_CAPTURED)
        pair.encounterStates[player ^ 1] = SOUL_LINK_ENCOUNTER_DEAD;
    pair.state = SOUL_LINK_PAIR_DEAD;
    pair.cleanupFlags |= 1 << player;
    return SoulLink_StorePair(&pair);
}

bool8 SoulLink_ReplaceLocalIdentity(const struct SoulLinkMonIdentity *oldMon, const struct SoulLinkMonIdentity *newMon)
{
    struct SoulLinkPair pair;
    u16 index;
    u8 player = GetHeader()->localPlayer;
    if (newMon == NULL || sSaveStatus != SOUL_LINK_SAVE_READY || !SoulLink_FindLocalPair(oldMon, &index, &pair)
     || (pair.encounterStates[player] != SOUL_LINK_ENCOUNTER_CAPTURED && pair.encounterStates[player] != SOUL_LINK_ENCOUNTER_DEAD))
        return FALSE;
    pair.mons[player] = *newMon;
    pair.mons[player].encounterOrdinal = GetPair(index)->mons[player].encounterOrdinal;
    if (pair.encounterStates[player] == SOUL_LINK_ENCOUNTER_DEAD)
        pair.cleanupFlags |= 1 << player;
    if (!IsPairValid(&pair) || HasConflict(&pair, index))
        return FALSE;
    *GetPair(index) = pair;
    return TRUE;
}

static bool8 HasUniqueTableField(const struct SoulLinkPair *rows, u16 count, u8 field, u8 side)
{
    // Reuse a bounded index for each field. A hash collision always compares
    // the full key and probes again; it never substitutes for an identity check.
    // At most 88 of 256 slots are occupied, so every probe terminates.
    u8 slots[256] = {0};
    u16 i;
    for (i = 0; i < count; i++)
    {
        const struct SoulLinkMonIdentity *mon = &rows[i].mons[side];
        u32 key;
        u8 slot;
        if (field == 0)
        {
            key = rows[i].pairId;
            if (key == 0) continue;
        }
        else
        {
            if (rows[i].encounterStates[side] == SOUL_LINK_ENCOUNTER_NONE) continue;
            if (field == 1) key = mon->encounterOrdinal;
            else
            {
                if (mon->species == SPECIES_NONE) continue;
                key = mon->otId ^ (mon->personality * 33u);
            }
        }
        key ^= key >> 16;
        key *= 0x9E3779B1u;
        key ^= key >> 16;
        slot = key;
        while (slots[slot])
        {
            const struct SoulLinkPair *other = &rows[slots[slot] - 1];
            if ((field == 0 && rows[i].pairId == other->pairId)
             || (field == 1 && mon->encounterOrdinal == other->mons[side].encounterOrdinal)
             || (field == 2 && SameMon(mon, &other->mons[side]))) return FALSE;
            slot++;
        }
        slots[slot] = i + 1;
    }
    return TRUE;
}

static bool8 IsTableValid(const struct SoulLinkPair *rows, u16 count, u8 player)
{
    u16 i;
    u8 side;
    if (count > SOUL_LINK_PAIR_CAPACITY || (count && rows == NULL)) return FALSE;
    for (i = 0; i < count; i++)
        if (!IsPairValidForPlayer(&rows[i], player)) return FALSE;
    if (!HasUniqueTableField(rows, count, 0, 0)) return FALSE;
    for (side = 0; side < SOUL_LINK_PLAYER_COUNT; side++)
        if (!HasUniqueTableField(rows, count, 1, side) || !HasUniqueTableField(rows, count, 2, side)) return FALSE;
    return TRUE;
}

static u8 PairIndexSlot(u16 key)
{
    u32 hash = key * 0x9E3779B1u;
    return hash ^ (hash >> 16);
}

static u16 FindIndexedRow(const struct SoulLinkPair *const *rows, const u8 *slots, u16 key, bool8 ordinal, u8 side)
{
    u8 slot = PairIndexSlot(key);
    if (key == 0) return SOUL_LINK_PAIR_CAPACITY;
    while (slots[slot])
    {
        u16 index = slots[slot] - 1;
        u16 candidate = ordinal ? rows[index]->mons[side].encounterOrdinal : rows[index]->pairId;
        if (candidate == key) return index;
        slot++;
    }
    return SOUL_LINK_PAIR_CAPACITY;
}

static bool8 BuildPairIndex(const struct SoulLinkPair *const *rows, u16 count, u8 *slots, bool8 ordinal, u8 side)
{
    u16 i;
    memset(slots, 0, 256);
    for (i = 0; i < count; i++)
    {
        u16 key = ordinal ? rows[i]->mons[side].encounterOrdinal : rows[i]->pairId;
        u8 slot = PairIndexSlot(key);
        if (key == 0) continue;
        while (slots[slot])
        {
            u16 other = slots[slot] - 1;
            if (key == (ordinal ? rows[other]->mons[side].encounterOrdinal : rows[other]->pairId)) return FALSE;
            slot++;
        }
        slots[slot] = i + 1;
    }
    return TRUE;
}

static bool8 PrepareExistingState(const struct SoulLinkPair *remote, u16 count, u8 player,
                                struct SoulLinkPair *merged, u16 *mergedCount)
{
    u16 i, j, total;
    u8 local = GetHeader()->localPlayer;
    if (!SoulLink_IsEnabled() || player >= SOUL_LINK_PLAYER_COUNT || player == local
     || merged == NULL || mergedCount == NULL || !IsTableValid(remote, count, player))
        return FALSE;
    total = GetHeader()->pairCount;
    for (i = 0; i < total; i++)
        merged[i] = *GetPair(i);
    for (i = 0; i < count; i++)
    {
        const struct SoulLinkPair *incoming = &remote[i];
        struct SoulLinkPair *row;
        u8 before, after;
        if (incoming->pairId == SOUL_LINK_PAIR_NONE)
            continue;
        for (j = 0; j < total; j++)
            if (merged[j].pairId == incoming->pairId)
                break;
        if (j == total)
        {
            if (total == SOUL_LINK_PAIR_CAPACITY)
                return FALSE;
            merged[total++] = *incoming;
            merged[j].cleanupFlags = incoming->encounterStates[local] == SOUL_LINK_ENCOUNTER_DEAD ? 1 << local : 0;
            continue;
        }
        row = &merged[j];
        if (row->areaId != incoming->areaId)
            return FALSE;
        if (row->encounterStates[local] == SOUL_LINK_ENCOUNTER_NONE
         && incoming->encounterStates[local] != SOUL_LINK_ENCOUNTER_NONE)
        {
            row->mons[local] = incoming->mons[local];
            row->encounterStates[local] = incoming->encounterStates[local];
            if (row->encounterStates[local] == SOUL_LINK_ENCOUNTER_DEAD)
                row->cleanupFlags |= 1 << local;
            row->state = GetPairState(row);
        }
        if (row->encounterStates[local] != SOUL_LINK_ENCOUNTER_NONE
         && incoming->encounterStates[local] != SOUL_LINK_ENCOUNTER_NONE
         && row->mons[local].encounterOrdinal != incoming->mons[local].encounterOrdinal)
            return FALSE;
        // The partner may remember a death lost by loading an older local save.
        // Restore only a tombstone for this exact already-known local identity.
        if (row->encounterStates[local] == SOUL_LINK_ENCOUNTER_CAPTURED
         && incoming->encounterStates[local] == SOUL_LINK_ENCOUNTER_DEAD)
        {
            if (!SameMon(&row->mons[local], &incoming->mons[local]))
                return FALSE;
            row->encounterStates[local] = SOUL_LINK_ENCOUNTER_DEAD;
            row->cleanupFlags |= 1 << local;
            row->state = GetPairState(row);
        }
        before = row->encounterStates[player];
        after = incoming->encounterStates[player];
        if (before != SOUL_LINK_ENCOUNTER_NONE && after != SOUL_LINK_ENCOUNTER_NONE
         && row->mons[player].encounterOrdinal != incoming->mons[player].encounterOrdinal)
            return FALSE;
        if (after == SOUL_LINK_ENCOUNTER_NONE || before == SOUL_LINK_ENCOUNTER_DEAD
         || before == SOUL_LINK_ENCOUNTER_FAILED)
            continue; // A stale snapshot cannot undo a terminal observation.
        if (before == SOUL_LINK_ENCOUNTER_CAPTURED && after == SOUL_LINK_ENCOUNTER_FAILED)
            return FALSE;
        row->mons[player] = incoming->mons[player];
        row->encounterStates[player] = after;
        row->cleanupFlags &= ~(1 << player);
        row->state = GetPairState(row);
    }
    if (!IsTableValid(merged, total, local))
        return FALSE;
    *mergedCount = total;
    return TRUE;
}

static bool8 AreasMatch(u16 a, u16 b)
{
    return a == b || (!GetHeader()->config.sameAreaPairing
        && a != SOUL_LINK_AREA_STARTER && b != SOUL_LINK_AREA_STARTER);
}

static bool8 CanCommitRemoteState(const struct SoulLinkPair *merged, u16 count);

static void PropagateLinkedDeaths(struct SoulLinkPair *rows, u16 count)
{
    u16 i;
    u8 side, local = GetHeader()->localPlayer;
    if (!SoulLink_IsLinkedDeathsEnabled())
        return;
    for (i = 0; i < count; i++)
        if (rows[i].encounterStates[0] == SOUL_LINK_ENCOUNTER_DEAD || rows[i].encounterStates[1] == SOUL_LINK_ENCOUNTER_DEAD)
            for (side = 0; side < SOUL_LINK_PLAYER_COUNT; side++)
                if (rows[i].encounterStates[side] == SOUL_LINK_ENCOUNTER_CAPTURED)
                {
                    rows[i].encounterStates[side] = SOUL_LINK_ENCOUNTER_DEAD;
                    if (side == local)
                        rows[i].cleanupFlags |= 1 << local;
                    rows[i].state = SOUL_LINK_PAIR_DEAD;
                }
}

u16 SoulLink_ApplyPendingLinkedDeaths(void)
{
    u16 i, changed = 0;
    u8 local;
    if (!SoulLink_IsLinkedDeathsEnabled())
        return 0;
    local = GetHeader()->localPlayer;
    for (i = 0; i < GetHeader()->pairCount; i++)
    {
        struct SoulLinkPair pair = *GetPair(i);
        if (pair.encounterStates[local] == SOUL_LINK_ENCOUNTER_CAPTURED
         && pair.encounterStates[local ^ 1] == SOUL_LINK_ENCOUNTER_DEAD)
        {
            PropagateLinkedDeaths(&pair, 1);
            if (SoulLink_StorePair(&pair))
                changed++;
        }
    }
    return changed;
}

static bool8 PrepareCoordinatorState(const struct SoulLinkPair *remote, u16 count,
                                    struct SoulLinkPair *merged, u16 *mergedCount)
{
    const struct SoulLinkPair *localRows[SOUL_LINK_PAIR_CAPACITY];
    u8 ids[256], ordinals[256];
    u16 i, j;
    if (!IsTableValid(remote, count, 0))
        return FALSE;
    if (GetHeader()->pairCount > SOUL_LINK_PAIR_CAPACITY) return FALSE;
    for (j = 0; j < GetHeader()->pairCount; j++) localRows[j] = GetPair(j);
    if (!BuildPairIndex(localRows, GetHeader()->pairCount, ids, FALSE, 1)
     || !BuildPairIndex(localRows, GetHeader()->pairCount, ordinals, TRUE, 1)) return FALSE;
    for (i = 0; i < count; i++)
    {
        const struct SoulLinkPair *incoming = &remote[i];
        const struct SoulLinkPair *old = NULL;
        struct SoulLinkPair *row = &merged[i];
        *row = *incoming;
        row->cleanupFlags = 0;
        // Prefer the local encounter over a possibly empty replica of this ID.
        j = incoming->encounterStates[1] == SOUL_LINK_ENCOUNTER_NONE ? SOUL_LINK_PAIR_CAPACITY
            : FindIndexedRow(localRows, ordinals, incoming->mons[1].encounterOrdinal, TRUE, 1);
        if (j != SOUL_LINK_PAIR_CAPACITY)
        {
            const struct SoulLinkPair *candidate = localRows[j];
            if ((candidate->pairId != 0 && candidate->pairId != incoming->pairId)
             || !AreasMatch(candidate->areaId, incoming->areaId)) return FALSE;
            old = candidate;
        }
        else
        {
            j = FindIndexedRow(localRows, ids, incoming->pairId, FALSE, 1);
            if (j != SOUL_LINK_PAIR_CAPACITY) old = localRows[j];
        }
        if (old != NULL)
        {
            u8 before = old->encounterStates[1];
            u8 after = incoming->encounterStates[1];
            if (old->pairId != 0 && old->areaId != incoming->areaId)
                return FALSE;
            if (old->pairId != 0 && old->encounterStates[0] != SOUL_LINK_ENCOUNTER_NONE
             && (incoming->encounterStates[0] == SOUL_LINK_ENCOUNTER_NONE
              || old->mons[0].encounterOrdinal != incoming->mons[0].encounterOrdinal
              || (old->encounterStates[0] == SOUL_LINK_ENCOUNTER_FAILED
               && incoming->encounterStates[0] != SOUL_LINK_ENCOUNTER_FAILED)))
                return FALSE;
            if (before != SOUL_LINK_ENCOUNTER_NONE)
            {
                if (after == SOUL_LINK_ENCOUNTER_NONE
                 || old->mons[1].encounterOrdinal != incoming->mons[1].encounterOrdinal
                 || (old->mons[1].species != SPECIES_NONE && !SameMon(&old->mons[1], &incoming->mons[1]))
                 || (before == SOUL_LINK_ENCOUNTER_FAILED && after != SOUL_LINK_ENCOUNTER_FAILED)
                 || (before != SOUL_LINK_ENCOUNTER_FAILED && after == SOUL_LINK_ENCOUNTER_FAILED))
                    return FALSE;
                row->mons[1] = old->mons[1];
                row->encounterStates[1] = before == SOUL_LINK_ENCOUNTER_DEAD ? before : after;
                row->cleanupFlags = old->cleanupFlags & SOUL_LINK_CLEANUP_PLAYER_1;
                if (row->encounterStates[1] == SOUL_LINK_ENCOUNTER_DEAD && before != SOUL_LINK_ENCOUNTER_DEAD)
                    row->cleanupFlags |= SOUL_LINK_CLEANUP_PLAYER_1;
            }
            // Preserve a remote terminal observation from an older coordinator save.
            if (old->pairId != 0 && old->encounterStates[0] == SOUL_LINK_ENCOUNTER_DEAD)
            {
                if (incoming->encounterStates[0] == SOUL_LINK_ENCOUNTER_NONE
                 || old->mons[0].encounterOrdinal != incoming->mons[0].encounterOrdinal
                 || !SameMon(&old->mons[0], &incoming->mons[0]))
                    return FALSE;
                row->mons[0] = old->mons[0];
                row->encounterStates[0] = SOUL_LINK_ENCOUNTER_DEAD;
            }
        }
        else if (incoming->encounterStates[1] == SOUL_LINK_ENCOUNTER_DEAD)
            row->cleanupFlags |= SOUL_LINK_CLEANUP_PLAYER_1;
        row->state = GetPairState(row);
    }
    // Every local candidate and every allocated ID must survive the proposal.
    for (j = 0; j < GetHeader()->pairCount; j++)
    {
        const struct SoulLinkPair *old = localRows[j];
        for (i = 0; i < count; i++)
            if ((old->pairId != 0 && old->pairId == merged[i].pairId)
             || (old->pairId == 0 && old->mons[1].encounterOrdinal == merged[i].mons[1].encounterOrdinal))
                break;
        if (i == count)
            return FALSE;
    }
    PropagateLinkedDeaths(merged, count);
    if (!CanCommitRemoteState(merged, count))
        return FALSE;
    *mergedCount = count;
    return TRUE;
}

bool8 SoulLink_PrepareRemoteState(const struct SoulLinkPair *remote, u16 count, u8 player,
                                struct SoulLinkPair *merged, u16 *mergedCount)
{
    u16 total, i, j, ordinal = 0;
    u32 nextId;
    if (!SoulLink_IsEnabled() || remote == NULL || merged == NULL || mergedCount == NULL
     || player >= SOUL_LINK_PLAYER_COUNT || player == GetHeader()->localPlayer)
        return FALSE;
    if (player == 0)
        return PrepareCoordinatorState(remote, count, merged, mergedCount);
    if (!PrepareExistingState(remote, count, player, merged, &total))
        return FALSE;
    nextId = GetHeader()->nextPairId;
    for (i = 0; i < total; i++)
        if (nextId != 0 && merged[i].pairId >= nextId)
            nextId = (u32)merged[i].pairId + 1;
    for (;;)
    {
        const struct SoulLinkPair *candidate = NULL;
        u16 best = SOUL_LINK_PAIR_CAPACITY;
        // Match candidates by their encounter order, independently of table order.
        for (i = 0; i < count; i++)
            if (remote[i].pairId == 0 && remote[i].mons[1].encounterOrdinal > ordinal
             && (candidate == NULL || remote[i].mons[1].encounterOrdinal < candidate->mons[1].encounterOrdinal))
                candidate = &remote[i];
        if (candidate == NULL)
            break;
        ordinal = candidate->mons[1].encounterOrdinal;
        for (j = 0; j < total; j++)
            if (merged[j].encounterStates[1] != SOUL_LINK_ENCOUNTER_NONE
             && merged[j].mons[1].encounterOrdinal == ordinal)
                break;
        if (j == total)
        {
            for (i = 0; i < total; i++)
                if (merged[i].encounterStates[1] == SOUL_LINK_ENCOUNTER_NONE && AreasMatch(merged[i].areaId, candidate->areaId)
                 && (best == SOUL_LINK_PAIR_CAPACITY
                  || merged[i].mons[0].encounterOrdinal < merged[best].mons[0].encounterOrdinal
                  || (merged[i].mons[0].encounterOrdinal == merged[best].mons[0].encounterOrdinal && merged[i].pairId < merged[best].pairId)))
                    best = i;
            j = best;
            if (j == SOUL_LINK_PAIR_CAPACITY)
            {
                if (total == SOUL_LINK_PAIR_CAPACITY || nextId == 0 || nextId > 0xFFFF)
                    return FALSE;
                j = total++;
                memset(&merged[j], 0, sizeof(merged[j]));
                merged[j].pairId = nextId++;
                merged[j].areaId = candidate->areaId;
            }
        }
        if (!AreasMatch(merged[j].areaId, candidate->areaId))
            return FALSE;
        if (merged[j].encounterStates[1] != SOUL_LINK_ENCOUNTER_DEAD
         && merged[j].encounterStates[1] != SOUL_LINK_ENCOUNTER_FAILED)
        {
            merged[j].mons[1] = candidate->mons[1];
            merged[j].encounterStates[1] = candidate->encounterStates[1];
        }
        merged[j].cleanupFlags &= ~SOUL_LINK_CLEANUP_PLAYER_1;
        merged[j].state = GetPairState(&merged[j]);
    }
    PropagateLinkedDeaths(merged, total);
    if (!CanCommitRemoteState(merged, total))
        return FALSE;
    *mergedCount = total;
    return TRUE;
}

static bool8 CanCommitRemoteState(const struct SoulLinkPair *merged, u16 count)
{
    struct SoulLinkSaveHeader *header = GetHeader();
    u8 ids[256] = {0};
    u16 i;
    if (!SoulLink_IsEnabled()
     || !IsTableValid(merged, count, header->localPlayer))
        return FALSE;
    for (i = 0; i < count; i++)
        if (merged[i].pairId)
        {
            u8 slot = PairIndexSlot(merged[i].pairId);
            while (ids[slot]) slot++;
            ids[slot] = i + 1;
        }
    for (i = 0; i < header->pairCount; i++)
    {
        u16 j;
        const struct SoulLinkPair *old = GetPair(i);
        const struct SoulLinkPair *updated;
        u8 local = header->localPlayer;
        bool8 restoredDeath;
        struct SoulLinkPair compatible;
        if (old->pairId)
        {
            u8 slot = PairIndexSlot(old->pairId);
            while (ids[slot] && merged[ids[slot] - 1].pairId != old->pairId) slot++;
            j = ids[slot] ? ids[slot] - 1 : count;
        }
        else
            for (j = 0; j < count; j++)
                if (old->mons[local].encounterOrdinal == merged[j].mons[local].encounterOrdinal) break;
        if (j == count)
            return FALSE;
        updated = &merged[j];
        restoredDeath = old->encounterStates[local] == SOUL_LINK_ENCOUNTER_CAPTURED
            && updated->encounterStates[local] == SOUL_LINK_ENCOUNTER_DEAD && (updated->cleanupFlags & (1 << local));
        if (old->encounterStates[local] != SOUL_LINK_ENCOUNTER_NONE)
            if ((!restoredDeath && updated->encounterStates[local] != old->encounterStates[local])
             || memcmp(&updated->mons[local], &old->mons[local], sizeof(struct SoulLinkMonIdentity))
             || (!restoredDeath && ((updated->cleanupFlags ^ old->cleanupFlags) & (1 << local))))
                return FALSE;
        compatible = *updated;
        compatible.mons[local ^ 1] = old->mons[local ^ 1];
        compatible.mons[local ^ 1].encounterOrdinal = updated->mons[local ^ 1].encounterOrdinal;
        if (old->pairId == 0 && AreasMatch(old->areaId, updated->areaId))
            compatible.areaId = old->areaId;
        if (!CanUpdatePair(old, &compatible))
            return FALSE;
    }
    return TRUE;
}

static void CommitValidatedRemoteState(const struct SoulLinkPair *merged, u16 count)
{
    struct SoulLinkSaveHeader *header = GetHeader();
    u16 i;
    for (i = 0; i < count; i++)
    {
        *GetPair(i) = merged[i];
        if (header->nextPairId != 0 && merged[i].pairId >= header->nextPairId)
            header->nextPairId = merged[i].pairId + 1;
    }
    for (i = count; i < header->pairCount; i++)
        memset(GetPair(i), 0, sizeof(struct SoulLinkPair));
    header->pairCount = count;
}

bool8 SoulLink_CommitRemoteState(const struct SoulLinkPair *merged, u16 count)
{
    if (!CanCommitRemoteState(merged, count)) return FALSE;
    CommitValidatedRemoteState(merged, count);
    return TRUE;
}

bool8 SoulLink_ApplyRemoteState(const struct SoulLinkPair *remote, u16 count, u8 player,
                               struct SoulLinkPair *scratch)
{
    u16 mergedCount;
    // This synchronous path never hands prepared rows back to a caller or
    // waits for a network/frame callback. Preparation already runs the full
    // commit validation. The staged protocol keeps its separate revalidation.
    if (remote == scratch || !SoulLink_PrepareRemoteState(remote, count, player, scratch, &mergedCount)) return FALSE;
    CommitValidatedRemoteState(scratch, mergedCount);
    return TRUE;
}

#include "soul_link_apply.inc"
