#include "global.h"
#include "main.h"
#include "malloc.h"
#include "overworld.h"
#include "pokemon.h"
#include "battle_setup.h"
#include "pokemon_storage_system.h"
#include "field_specials.h"
#include "event_data.h"
#include "constants/flags.h"
#include "constants/species.h"
#include "constants/region_map_sections.h"
#include "soul_link.h"
#include "soul_link_companion.h"
#include "soul_link_link.h"
#include "soul_link_local.h"

EWRAM_DATA struct SoulLinkCompanionControl *gSoulLinkCompanionControl = NULL;
static EWRAM_DATA u16 sViewTicks = 0;
static EWRAM_DATA u16 sCommandTicks = 0;
static EWRAM_DATA struct SoulLinkApplyJob *sApplyJob = NULL;
static EWRAM_DATA u32 sApplyCommandId = 0;
STATIC_ASSERT(sizeof(struct SoulLinkCompanionView) == 2952, CompanionViewSize);
STATIC_ASSERT(sizeof(struct SoulLinkCompanionControl) == 5952, CompanionControlSize);

bool8 SoulLink_CompanionRecoverStarter(void)
{
    struct SoulLinkPair pair;
    struct SoulLinkMonIdentity identity;
    struct Pokemon *candidate = NULL;
    u16 i;
    u8 local;
    if (!SoulLink_IsEnabled() || !IsOverworldIdle()) return FALSE;
    local = gSaveBlock2Ptr->soulLink.header.localPlayer;
    if (local > 1) return FALSE;
    // Terminal records count too: a saved death must never be replaced.
    for (i = 0; i < SoulLink_GetPairCount(); i++)
        if (SoulLink_ReadPair(i, &pair) && pair.areaId == SOUL_LINK_AREA_STARTER
         && pair.encounterStates[local] != SOUL_LINK_ENCOUNTER_NONE) return FALSE;
    for (i = 0; i < PARTY_SIZE; i++)
    {
        struct Pokemon *mon = &gPlayerParty[i];
        u16 species = GetMonData(mon, MON_DATA_SPECIES, NULL);
        if (species < SPECIES_BULBASAUR || species > SPECIES_BLASTOISE
         || GetMonData(mon, MON_DATA_IS_EGG, NULL)
         || GetMonData(mon, MON_DATA_OT_ID, NULL) != GetPlayerTrainerId()
         || GetMonData(mon, MON_DATA_MET_LOCATION, NULL) != MAPSEC_PALLET_TOWN
         || GetMonData(mon, MON_DATA_MET_LEVEL, NULL) != 5) continue;
        if (candidate != NULL) return FALSE; // Ambiguous provenance.
        candidate = mon;
    }
    if (candidate == NULL) return FALSE;
    identity.otId = GetMonData(candidate, MON_DATA_OT_ID, NULL);
    identity.personality = GetMonData(candidate, MON_DATA_PERSONALITY, NULL);
    identity.species = GetMonData(candidate, MON_DATA_SPECIES, NULL);
    identity.encounterOrdinal = 0;
    if (SoulLink_FindLocalPair(&identity, NULL, NULL)
     || !SoulLink_RecordLocalEncounter(SOUL_LINK_AREA_STARTER, &identity, SOUL_LINK_ENCOUNTER_CAPTURED)) return FALSE;
    SetNuzlockeDupeFlags(SpeciesToNationalPokedexNum(identity.species));
    if (GetMonData(candidate, MON_DATA_HP, NULL) == 0) SoulLink_MarkLocalDeath(&identity);
    return TRUE;
}

static bool8 HasLocalEncounter(u16 area)
{
    struct SoulLinkPair pair;
    u16 i;
    u8 local = gSaveBlock2Ptr->soulLink.header.localPlayer;
    for (i = 0; i < SoulLink_GetPairCount(); i++)
        if (SoulLink_ReadPair(i, &pair) && pair.areaId == area
         && pair.encounterStates[local] != SOUL_LINK_ENCOUNTER_NONE)
            return TRUE;
    return FALSE;
}

static bool8 IsOwnedCapture(struct BoxPokemon *mon, u16 area)
{
    u8 section = GetBoxMonData(mon, MON_DATA_MET_LOCATION, NULL);
    return GetBoxMonData(mon, MON_DATA_SPECIES, NULL) != SPECIES_NONE
        && !GetBoxMonData(mon, MON_DATA_IS_EGG, NULL)
        && GetBoxMonData(mon, MON_DATA_MET_LEVEL, NULL) != 0
        && GetBoxMonData(mon, MON_DATA_OT_ID, NULL) == GetPlayerTrainerId()
        && SoulLink_GetCanonicalArea(section, 0xFF, 0xFF) == area;
}

bool8 SoulLink_CompanionRecoverEarlyCaptures(void)
{
    struct SoulLinkMonIdentity identity;
    u16 i, j, box, area, matches;
    // Before activation, vanilla Nuzlocke already records consumed areas.
    // Recover only a unique owned acquisition in such an area. Never infer
    // FIFO order, Safari subdivisions, or overwrite a failed/dead encounter.
    if (!SoulLink_IsEnabled() || !IsOverworldIdle()
     || !gSaveBlock1Ptr->keyFlags.nuzlocke
     || !gSaveBlock2Ptr->soulLink.header.config.firstEncounterOnly
     || !gSaveBlock2Ptr->soulLink.header.config.sameAreaPairing) return FALSE;
    for (i = 0; i < PARTY_SIZE; i++)
    {
        struct Pokemon *mon = &gPlayerParty[i];
        area = SoulLink_GetCanonicalArea(GetMonData(mon, MON_DATA_MET_LOCATION, NULL), 0xFF, 0xFF);
        if (area == 0 || area > 0x56 || area == 0x27
         || !FlagGet(NUZLOCKE_FLAGS_START + area - 1)
         || HasLocalEncounter(area) || !IsOwnedCapture(&mon->box, area)) continue;
        matches = 0;
        for (j = 0; j < PARTY_SIZE; j++)
            if (IsOwnedCapture(&gPlayerParty[j].box, area)) matches++;
        for (box = 0; box < TOTAL_BOXES_COUNT && matches < 2; box++)
            for (j = 0; j < IN_BOX_COUNT && matches < 2; j++)
                if (IsOwnedCapture(GetBoxedMonPtr(box, j), area)) matches++;
        for (j = 0; j < 3 && matches < 2; j++)
            if (IsOwnedCapture(j < 2 ? &gSaveBlock1Ptr->daycare.mons[j].mon : &gSaveBlock1Ptr->route5DayCareMon.mon, area)) matches++;
        if (matches != 1) continue;
        identity.otId = GetMonData(mon, MON_DATA_OT_ID, NULL);
        identity.personality = GetMonData(mon, MON_DATA_PERSONALITY, NULL);
        identity.species = GetMonData(mon, MON_DATA_SPECIES, NULL);
        identity.encounterOrdinal = 0;
        if (SoulLink_FindLocalPair(&identity, NULL, NULL)
         || !SoulLink_RecordLocalEncounter(area, &identity, SOUL_LINK_ENCOUNTER_CAPTURED)) continue;
        SetNuzlockeDupeFlags(SpeciesToNationalPokedexNum(identity.species));
        if (GetMonData(mon, MON_DATA_HP, NULL) == 0) SoulLink_MarkLocalDeath(&identity);
        return TRUE; // Bound recovery to one acquisition per safe snapshot.
    }
    return FALSE;
}

static u32 CanonicalHash(const struct SoulLinkPair *pair, u32 hash)
{
    const u8 *bytes = (const u8 *)pair;
    u16 i;
    // Cleanup flags are local storage housekeeping, not a new shared event.
    for (i = 0; i < sizeof(*pair) - 1; i++) hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

static bool8 IsRemoteViewValid(const struct SoulLinkCompanionView *remote)
{
    struct SoulLinkRunId run;
    u16 i;
    u32 hash = 2166136261u;
    if (remote == NULL || remote->magic != SOUL_LINK_COMPANION_MAGIC || remote->generation == 0
     || remote->saveStatus != SOUL_LINK_SAVE_READY || !remote->enabled
     || !IsOverworldIdle() || !SoulLink_IsEnabled() || !SoulLink_GetRunId(&run)
     || remote->player != (gSaveBlock2Ptr->soulLink.header.localPlayer ^ 1)
     || remote->pairCount > SOUL_LINK_PAIR_CAPACITY
     || remote->sessionStatus == SOUL_LINK_SESSION_CONNECTING || remote->sessionStatus == SOUL_LINK_SESSION_SYNCING
     || memcmp(&remote->run, &run, sizeof(run))
     || memcmp(remote->participants, gSaveBlock2Ptr->soulLink.header.participantIds, sizeof(remote->participants))
     || memcmp(&remote->config, &gSaveBlock2Ptr->soulLink.header.config, sizeof(remote->config)))
        return FALSE;
    for (i = 0; i < remote->pairCount; i++) hash = CanonicalHash(&remote->pairs[i], hash);
    if (hash != remote->pairDigest) return FALSE;
    return TRUE;
}

bool8 SoulLink_CompanionApplyView(const struct SoulLinkCompanionView *remote)
{
    struct SoulLinkPair *merged;
    bool8 ok;
    if (!IsRemoteViewValid(remote)) return FALSE;
    merged = Alloc(sizeof(*merged) * SOUL_LINK_PAIR_CAPACITY);
    if (merged == NULL) return FALSE;
    // The coordinator allocates IDs. The follower rejects a stale proposal
    // that omits its newer local capture; its next reading lets A reconcile it.
    ok = SoulLink_ApplyRemoteState(remote->pairs, remote->pairCount, remote->player, merged);
    Free(merged);
    return ok;
}

static void CancelApplyView(void)
{
    if (sApplyJob != NULL) Free(sApplyJob);
    sApplyJob = NULL;
    sApplyCommandId = 0;
}

bool8 SoulLink_CompanionStartApplyView(const struct SoulLinkCompanionView *remote)
{
    CancelApplyView();
    if (!IsRemoteViewValid(remote)) return FALSE;
    sApplyJob = Alloc(SoulLink_ApplyJobSize());
    if (sApplyJob == NULL) return FALSE;
    if (!SoulLink_BeginApplyJob(sApplyJob, remote->pairs, remote->pairCount, remote->player))
    {
        CancelApplyView();
        return FALSE;
    }
    return TRUE;
}

u8 SoulLink_CompanionStepApplyView(void)
{
    u8 result;
    if (sApplyJob == NULL) return 2;
    result = IsOverworldIdle() ? SoulLink_StepApplyJob(sApplyJob) : 3;
    if (result != 0) CancelApplyView();
    return result;
}

void SoulLink_CompanionReset(void)
{
    SoulLink_ProtocolResetHeapCache();
    // Every world/title heap reset also invalidates this heap-owned view.
    gSoulLinkCompanionControl = NULL;
    sApplyJob = NULL;
    sApplyCommandId = 0;
    sViewTicks = sCommandTicks = 0;
}

static void Command(void)
{
    struct SoulLinkCompanionControl *control = gSoulLinkCompanionControl;
    struct SoulLinkRunId run;
    struct SoulLinkConfig config;
    bool8 ok = FALSE;
    if (sApplyJob != NULL && (control->commandId != sApplyCommandId
     || control->command != SOUL_LINK_COMMAND_APPLY_VIEW || control->commandId == control->appliedId)) CancelApplyView();
    if (control->commandId == control->appliedId) { sCommandTicks = 0; return; }
    if (sApplyJob != NULL && !IsOverworldIdle())
    {
        CancelApplyView();
        control->result = 3;
        control->remoteCommandId = 0;
        control->appliedId = control->commandId;
        sCommandTicks = 0;
        return;
    }
    // Requests never alter battle/menu data. Busy requests expire instead of
    // leaving the other game locked until a long battle ends.
    if (!IsOverworldIdle())
    {
        if (++sCommandTicks < 120) return;
        control->result = 3;
    }
    else if (SoulLink_GetSaveStatus() != SOUL_LINK_SAVE_READY || control->role > 1)
        control->result = 2;
    else if (control->command == SOUL_LINK_COMMAND_INIT)
    {
        if (!SoulLink_GetRunId(&run) && SoulLink_GetPairCount() == 0)
        {
            ok = SoulLink_BeginRun(&control->run, control->participants, control->role);
            if (ok && SoulLink_GetConfig(&config))
            {
                u8 box, slot;
                config.enabled = TRUE;
                // Existing rules are preserved; teams become enforced after
                // the first composition explicitly accepted by both players.
                ok = SoulLink_SetConfig(&config);
                SoulLink_CompanionRecoverStarter();
                for (box = TOTAL_BOXES_COUNT; box > 0; box--)
                {
                    for (slot = 0; slot < IN_BOX_COUNT; slot++)
                        if (GetBoxMonData(GetBoxedMonPtr(box - 1, slot), MON_DATA_SPECIES, NULL) != SPECIES_NONE) break;
                    if (slot == IN_BOX_COUNT) { SoulLink_SetGraveBox(box - 1); break; }
                }
            }
        }
        control->result = ok ? 1 : 2;
    }
    else if (gSaveBlock2Ptr->soulLink.header.localPlayer != control->role || !SoulLink_IsEnabled())
        control->result = 2;
    else if (control->command == SOUL_LINK_COMMAND_DEATHS)
    {
        u8 n, local = control->role;
        u16 row;
        struct SoulLinkPair pair;
        ok = control->count > 0 && control->count <= 6 && SoulLink_GetRunId(&run)
          && memcmp(&run, &control->run, sizeof(run)) == 0
          && memcmp(gSaveBlock2Ptr->soulLink.header.participantIds, control->participants, sizeof(control->participants)) == 0;
        // Death notifications are monotonic and independent of a full exchange.
        // Only known pairs in this exact run can be changed, never battle data.
        for (n = 0; ok && n < control->count; n++)
        {
            bool8 found = FALSE;
            for (row = 0; row < SoulLink_GetPairCount(); row++)
                if (SoulLink_ReadPair(row, &pair) && pair.pairId == control->team[n])
                {
                    found = pair.encounterStates[local ^ 1] == SOUL_LINK_ENCOUNTER_CAPTURED
                         || pair.encounterStates[local ^ 1] == SOUL_LINK_ENCOUNTER_DEAD;
                    break;
                }
            if (!found) ok = FALSE;
        }
        for (n = 0; ok && n < control->count; n++)
            for (row = 0; row < SoulLink_GetPairCount(); row++)
                if (SoulLink_ReadPair(row, &pair) && pair.pairId == control->team[n])
                {
                    pair.encounterStates[local ^ 1] = SOUL_LINK_ENCOUNTER_DEAD;
                    pair.state = SOUL_LINK_PAIR_DEAD;
                    if (!SoulLink_StorePair(&pair)) ok = FALSE;
                    break;
                }
        if (ok && SoulLink_ApplyPendingLinkedDeaths()) SoulLink_NotifyDeathSync();
        control->result = ok ? 1 : 2;
    }
    else if (control->command == SOUL_LINK_COMMAND_APPLY_VIEW)
    {
        if (sApplyJob == NULL)
        {
            if (control->remoteCommandId != control->commandId
             || !SoulLink_CompanionStartApplyView(&control->remoteView))
            {
                control->result = 2;
                control->remoteCommandId = 0;
                control->appliedId = control->commandId;
                return;
            }
            sApplyCommandId = control->commandId;
            return; // Setup and reconciliation use separate frames.
        }
        control->result = SoulLink_CompanionStepApplyView();
        if (control->result == 0) return;
        control->remoteCommandId = 0;
    }
    else if (control->command == SOUL_LINK_COMMAND_SYNC)
    {
        SoulLink_CompanionRecoverStarter();
        control->result = SoulLink_StartCompanionSession() ? 1 : 3;
    }
    else if (control->command == SOUL_LINK_COMMAND_TEAM && control->count > 0 && control->count <= 6)
    {
        if (SoulLink_SetTeamSelection(control->team, control->count) && SoulLink_GetConfig(&config))
        {
            config.enforcePartyPairs = TRUE;
            ok = SoulLink_SetConfig(&config) && SoulLink_StartCompanionSession();
        }
        control->result = ok ? 1 : 2;
    }
    else control->result = 2;
    control->appliedId = control->commandId;
    sCommandTicks = 0;
}

void SoulLink_CompanionUpdate(void)
{
    struct SoulLinkCompanionView *view;
    struct SoulLinkRunId run;
    u16 i;
    u8 player;
    u32 hash = 2166136261u;
    if (gSoulLinkRemoteEnabled != SOUL_LINK_REMOTE_MAGIC) return;
    if (gSoulLinkCompanionControl == NULL)
    {
        gSoulLinkCompanionControl = AllocZeroed(sizeof(*gSoulLinkCompanionControl));
        if (gSoulLinkCompanionControl == NULL) return;
        gSoulLinkCompanionControl->magic = SOUL_LINK_COMPANION_MAGIC;
    }
    Command();
    view = &gSoulLinkCompanionControl->view;
    view->magic = SOUL_LINK_COMPANION_MAGIC;
    view->safe = IsOverworldIdle();
    view->inBattle = gMain.inBattle;
    view->sessionStatus = SoulLink_GetSessionStatus();
    view->saveStatus = SoulLink_GetSaveStatus();
    view->enabled = SoulLink_IsEnabled();
    if (++sViewTicks < 30) return;
    sViewTicks = 0;
    if (view->saveStatus != SOUL_LINK_SAVE_READY) { view->generation++; return; }
    // A starter acquired after an empty initial sync must itself change the
    // registry, rather than waiting for the next capture to trigger a sync.
    if (view->safe)
    {
        SoulLink_CompanionRecoverStarter();
        SoulLink_CompanionRecoverEarlyCaptures();
    }
    // The registry contains plain records: publish deaths even during battle or
    // the defeat dialogue. Party decryption still waits for a safe frame.
    player = gSaveBlock2Ptr->soulLink.header.localPlayer;
    view->player = player;
    memset(&view->run, 0, sizeof(view->run));
    if (SoulLink_GetRunId(&run)) view->run = run;
    memcpy(view->participants, gSaveBlock2Ptr->soulLink.header.participantIds, sizeof(view->participants));
    view->graveBox = gSaveBlock2Ptr->soulLink.header.graveBox;
    view->pairCount = SoulLink_GetPairCount();
    memcpy(view->team, gSaveBlock2Ptr->soulLink.header.team.pairIds, sizeof(view->team));
    view->teamCount = gSaveBlock2Ptr->soulLink.header.team.count;
    view->locked = gSaveBlock2Ptr->soulLink.header.team.locked;
    view->config = gSaveBlock2Ptr->soulLink.header.config;
    view->configRevision = gSaveBlock2Ptr->soulLink.header.configRevision;
    memset(view->pairs, 0, sizeof(view->pairs));
    for (i = 0; i < view->pairCount; i++)
        if (SoulLink_ReadPair(i, &view->pairs[i])) hash = CanonicalHash(&view->pairs[i], hash);
    view->pairDigest = hash;
    if (!view->safe && view->sessionStatus != SOUL_LINK_SESSION_CONNECTING && view->sessionStatus != SOUL_LINK_SESSION_SYNCING)
    {
        view->generation++;
        return;
    }
    memset(view->party, 0, sizeof(view->party));
    for (i = 0; i < PARTY_SIZE; i++)
    {
        struct SoulLinkMonIdentity identity;
        struct SoulLinkPair pair;
        struct Pokemon *mon = &gPlayerParty[i];
        view->party[i].species = GetMonData(mon, MON_DATA_SPECIES, NULL);
        if (view->party[i].species == SPECIES_NONE) continue;
        view->party[i].hp = GetMonData(mon, MON_DATA_HP, NULL);
        view->party[i].maxHp = GetMonData(mon, MON_DATA_MAX_HP, NULL);
        view->party[i].level = GetMonData(mon, MON_DATA_LEVEL, NULL);
        identity.otId = GetMonData(mon, MON_DATA_OT_ID, NULL);
        identity.personality = GetMonData(mon, MON_DATA_PERSONALITY, NULL);
        identity.species = view->party[i].species; identity.encounterOrdinal = 0;
        if (player < 2 && SoulLink_FindLocalPair(&identity, NULL, &pair)) view->party[i].pairId = pair.pairId;
    }
    view->generation++;
}
