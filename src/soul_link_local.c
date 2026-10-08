#include "global.h"
#include "soul_link_rules.h"
#include "battle.h"
#include "battle_setup.h"
#include "event_data.h"
#include "field_message_box.h"
#include "item.h"
#include "mail_data.h"
#include "overworld.h"
#include "pokemon_storage_system.h"
#include "soul_link.h"
#include "soul_link_local.h"
#include "string_util.h"
#include "script.h"
#include "task.h"
#include "constants/maps.h"
#include "constants/battle.h"
#include "constants/items.h"
#include "constants/pokemon.h"
#include "constants/region_map_sections.h"

extern const u8 NuzlockeLUT[MAPSEC_COUNT];

// This encounter snapshot is never serialized: the engine cannot save mid-battle.
static EWRAM_DATA struct
{
    u16 area;
    bool8 active;
    bool8 eligible;
    bool8 acquired;
    bool8 battleAllowed;
    bool8 notice;
    u8 fieldTicks;
    u8 cleanupCursor;
    bool8 tradeConflict;
} sLocal = {};

static const u8 sGraveName[] = _("S. GRAVE"); // PC names have eight characters.
static const u8 sDeathNotice[] = _("Pair lost!\nDead POKéMON go to the PC.");
static const u8 sPendingNotice[] = _("Pair lost!\nPC transfer pending.");
static const u8 sTradeNotice[] = _("SOUL LINK identity conflict.\nThe trade was cancelled.");
static const u8 sPairNotice[] = _("New SOUL LINK pair!\nBoth captures are linked.");
static const u8 sFailedPairNotice[] = _("SOUL LINK encounter failed.\nYour captures are kept.");

static void Task_DeathNotice(u8 taskId)
{
    if (IsFieldMessageBoxHidden() && (gMain.newKeys & (A_BUTTON | B_BUTTON)))
    {
        HideFieldMessageBox();
        UnlockPlayerFieldControls();
        DestroyTask(taskId);
    }
}

static void GetIdentity(struct BoxPokemon *mon, struct SoulLinkMonIdentity *identity)
{
    identity->otId = GetBoxMonData(mon, MON_DATA_OT_ID, NULL);
    identity->personality = GetBoxMonData(mon, MON_DATA_PERSONALITY, NULL);
    identity->species = GetBoxMonData(mon, MON_DATA_SPECIES, NULL);
    identity->encounterOrdinal = 0;
}

u16 SoulLink_GetCanonicalArea(u8 section, u8 mapGroup, u8 mapNum)
{
    u16 area;
    if (section >= MAPSEC_COUNT)
        return SOUL_LINK_AREA_NONE;
    area = NuzlockeLUT[section];
    if (area == 0x27)
    {
        if (mapGroup == MAP_GROUP(SAFARI_ZONE_EAST) && mapNum == MAP_NUM(SAFARI_ZONE_EAST))
            return 0x4F;
        if (mapGroup == MAP_GROUP(SAFARI_ZONE_NORTH) && mapNum == MAP_NUM(SAFARI_ZONE_NORTH))
            return 0x50;
        if (mapGroup == MAP_GROUP(SAFARI_ZONE_WEST) && mapNum == MAP_NUM(SAFARI_ZONE_WEST))
            return 0x51;
        if (mapGroup == MAP_GROUP(SAFARI_ZONE_NORTH_WEST) && mapNum == MAP_NUM(SAFARI_ZONE_NORTH_WEST))
            return 0x52;
        if (mapGroup == MAP_GROUP(SAFARI_ZONE_NORTH_EAST) && mapNum == MAP_NUM(SAFARI_ZONE_NORTH_EAST))
            return 0x53;
    }
    return area;
}

u16 SoulLink_GetCurrentArea(void)
{
    return SoulLink_GetCanonicalArea(GetCurrentRegionMapSectionId(), gSaveBlock1Ptr->location.mapGroup, gSaveBlock1Ptr->location.mapNum);
}

static bool8 IsStarter(void)
{
    return !gMain.inBattle
        && gSaveBlock1Ptr->location.mapGroup == MAP_GROUP(PALLET_TOWN_PROFESSOR_OAKS_LAB)
        && gSaveBlock1Ptr->location.mapNum == MAP_NUM(PALLET_TOWN_PROFESSOR_OAKS_LAB);
}

static bool8 IsAreaUsed(u16 area)
{
    struct SoulLinkPair pair;
    u16 i;
    u8 player = gSaveBlock2Ptr->soulLink.header.localPlayer;
    if (area <= 0x56 && FlagGet(NUZLOCKE_FLAGS_START + area - 1))
        return TRUE;
    for (i = 0; i < SoulLink_GetPairCount(); i++)
        if (SoulLink_ReadPair(i, &pair) && pair.areaId == area
         && (area == SOUL_LINK_AREA_STARTER || gSaveBlock2Ptr->soulLink.header.config.sameAreaPairing)
         && pair.encounterStates[player] != SOUL_LINK_ENCOUNTER_NONE)
            return TRUE;
    return FALSE;
}

static bool8 IsEligible(u16 area, u16 species)
{
    const struct SoulLinkConfig *config = &gSaveBlock2Ptr->soulLink.header.config;
    struct SoulLinkPair pair;
    u16 i;
    u8 player = gSaveBlock2Ptr->soulLink.header.localPlayer;
    if (area == SOUL_LINK_AREA_NONE || species == SPECIES_NONE || species == SPECIES_SHEDINJA
     || !SoulLink_CanRecordLocalEncounter(area))
        return FALSE;
    for (i = 0; i < SoulLink_GetPairCount(); i++)
        if (SoulLink_ReadPair(i, &pair) && pair.mons[player].encounterOrdinal == 0xFFFF)
            return FALSE;
    if (area == SOUL_LINK_AREA_STARTER)
        return !IsAreaUsed(area);
    if (config->firstEncounterOnly && IsAreaUsed(area))
        return FALSE;
    return !config->dupesClause || !IsWildMonNuzlockeDupe(species);
}

static bool8 IsOrdinaryBattle(void)
{
    return !(gBattleTypeFlags & (BATTLE_TYPE_LINK | BATTLE_TYPE_BATTLE_TOWER | BATTLE_TYPE_TRAINER_TOWER
        | BATTLE_TYPE_FIRST_BATTLE | BATTLE_TYPE_POKEDUDE | BATTLE_TYPE_OLD_MAN_TUTORIAL))
        && !FlagGet(FLAG_MASTER_TRAINER_BATTLE);
}

void SoulLink_BeginBattle(void)
{
    sLocal.active = FALSE;
    sLocal.acquired = FALSE;
    sLocal.battleAllowed = SoulLink_IsEnabled() && IsOrdinaryBattle();
    if (!sLocal.battleAllowed || (gBattleTypeFlags & (BATTLE_TYPE_TRAINER | BATTLE_TYPE_GHOST))
     || !FlagGet(FLAG_SYS_POKEDEX_GET))
        return;
    sLocal.area = SoulLink_GetCurrentArea();
    sLocal.active = sLocal.area != SOUL_LINK_AREA_NONE;
    sLocal.eligible = IsEligible(sLocal.area, GetMonData(&gEnemyParty[0], MON_DATA_SPECIES, NULL));
}

static void ConsumeArea(u16 area)
{
    if (area != SOUL_LINK_AREA_NONE && area <= 0x56)
        FlagSet(NUZLOCKE_FLAGS_START + area - 1);
}

static void NotifyLatestPair(void)
{
    struct SoulLinkPair pair, latest;
    u16 i, ordinal = 0;
    u8 player = gSaveBlock2Ptr->soulLink.header.localPlayer;
    for (i = 0; i < SoulLink_GetPairCount(); i++)
        if (SoulLink_ReadPair(i, &pair) && pair.mons[player].encounterOrdinal > ordinal)
        {
            latest = pair;
            ordinal = pair.mons[player].encounterOrdinal;
        }
    if (ordinal != 0 && latest.encounterStates[0] != SOUL_LINK_ENCOUNTER_NONE
     && latest.encounterStates[1] != SOUL_LINK_ENCOUNTER_NONE)
        SoulLink_NotifyPairSync(latest.encounterStates[0] == SOUL_LINK_ENCOUNTER_FAILED
                            || latest.encounterStates[1] == SOUL_LINK_ENCOUNTER_FAILED);
}

void SoulLink_EndBattle(void)
{
    if (sLocal.active && sLocal.eligible && !sLocal.acquired && SoulLink_IsEnabled())
        if (SoulLink_RecordLocalEncounter(sLocal.area, NULL, SOUL_LINK_ENCOUNTER_FAILED))
        {
            ConsumeArea(sLocal.area);
            NotifyLatestPair();
        }
    sLocal.active = FALSE;
    sLocal.battleAllowed = FALSE;
}

bool8 SoulLink_PrepareAcquisition(struct Pokemon *mon)
{
    struct SoulLinkMonIdentity identity;
    u16 area;
    u16 box, pos;
    bool8 room = FALSE;
    if (!SoulLink_IsEnabled())
        return TRUE;
    GetIdentity(&mon->box, &identity);
    if (SoulLink_FindLocalPair(&identity, NULL, NULL))
        return FALSE;
    for (pos = 0; pos < PARTY_SIZE; pos++)
        if (GetMonData(&gPlayerParty[pos], MON_DATA_SPECIES, NULL) == SPECIES_NONE)
            room = TRUE;
    for (box = 0; !room && box < TOTAL_BOXES_COUNT; box++)
        if (!SoulLink_IsGraveBox(box))
            for (pos = 0; pos < IN_BOX_COUNT; pos++)
                if (GetBoxMonData(GetBoxedMonPtr(box, pos), MON_DATA_SPECIES, NULL) == SPECIES_NONE)
                {
                    room = TRUE;
                    break;
                }
    if (!room)
        return FALSE;
    if (gMain.inBattle)
        return !sLocal.active || (sLocal.eligible && !sLocal.acquired);
    area = IsStarter() ? SOUL_LINK_AREA_STARTER : SoulLink_GetCurrentArea();
    if (area == SOUL_LINK_AREA_NONE || (!IsStarter() && !FlagGet(FLAG_SYS_POKEDEX_GET)))
        return TRUE;
    return IsEligible(area, identity.species);
}

void SoulLink_FinishAcquisition(struct Pokemon *mon, u8 result)
{
    struct SoulLinkMonIdentity identity;
    u16 area;
    if (!SoulLink_IsEnabled() || (result != MON_GIVEN_TO_PARTY && result != MON_GIVEN_TO_PC))
        return;
    if (gMain.inBattle)
    {
        if (!sLocal.active || !sLocal.eligible || sLocal.acquired)
            return;
        area = sLocal.area;
    }
    else
    {
        area = IsStarter() ? SOUL_LINK_AREA_STARTER : SoulLink_GetCurrentArea();
        if (area == SOUL_LINK_AREA_NONE || (!IsStarter() && !FlagGet(FLAG_SYS_POKEDEX_GET)))
            return;
    }
    GetIdentity(&mon->box, &identity);
    if (SoulLink_RecordLocalEncounter(area, &identity, SOUL_LINK_ENCOUNTER_CAPTURED))
    {
        ConsumeArea(area);
        SetNuzlockeDupeFlags(SpeciesToNationalPokedexNum(identity.species));
        NotifyLatestPair();
        if (gMain.inBattle)
            sLocal.acquired = TRUE;
    }
}

void SoulLink_RecordMonDeath(struct Pokemon *mon)
{
    struct SoulLinkMonIdentity identity;
    struct SoulLinkPair pair;
    u8 player = gSaveBlock2Ptr->soulLink.header.localPlayer;
    if (!SoulLink_IsEnabled() || GetMonData(mon, MON_DATA_IS_EGG, NULL))
        return;
    GetIdentity(&mon->box, &identity);
    if (SoulLink_FindLocalPair(&identity, NULL, &pair) && pair.encounterStates[player] != SOUL_LINK_ENCOUNTER_DEAD
     && SoulLink_MarkLocalDeath(&identity))
    {
        sLocal.notice = TRUE;
        sLocal.fieldTicks = 0;
        sLocal.cleanupCursor = 0;
    }
}

void SoulLink_RecordBattleDeath(u8 partyIndex)
{
    if (sLocal.battleAllowed && partyIndex < PARTY_SIZE)
        SoulLink_RecordMonDeath(&gPlayerParty[partyIndex]);
}

bool8 SoulLink_IsBoxMonDead(struct BoxPokemon *mon)
{
    struct SoulLinkMonIdentity identity;
    struct SoulLinkPair pair;
    if (SoulLink_GetSaveStatus() != SOUL_LINK_SAVE_READY || SoulLink_GetPairCount() == 0)
        return FALSE;
    GetIdentity(mon, &identity);
    // Old bonus clones remain stored, but cannot be healed or used as a pair.
    if (SoulLink_IsLegacyShedinja(&identity))
        return TRUE;
    if (!SoulLink_FindLocalPair(&identity, NULL, &pair))
        return FALSE;
    return pair.encounterStates[gSaveBlock2Ptr->soulLink.header.localPlayer] == SOUL_LINK_ENCOUNTER_DEAD
        || (SoulLink_IsLinkedDeathsEnabled()
         && pair.encounterStates[gSaveBlock2Ptr->soulLink.header.localPlayer] == SOUL_LINK_ENCOUNTER_CAPTURED
         && pair.encounterStates[gSaveBlock2Ptr->soulLink.header.localPlayer ^ 1] == SOUL_LINK_ENCOUNTER_DEAD);
}

bool8 SoulLink_IsGraveBox(u8 box)
{
    return SoulLink_GetSaveStatus() == SOUL_LINK_SAVE_READY
        && gSaveBlock2Ptr->soulLink.header.graveBox == box;
}

static s16 FindGraveSpot(void)
{
    s16 box, pos;
    if (gSaveBlock2Ptr->soulLink.header.graveBox == SOUL_LINK_BOX_NONE)
    {
        for (box = TOTAL_BOXES_COUNT - 1; box >= 0; box--)
        {
            for (pos = 0; pos < IN_BOX_COUNT; pos++)
                if (GetBoxMonData(GetBoxedMonPtr(box, pos), MON_DATA_SPECIES, NULL) != SPECIES_NONE)
                    break;
            if (pos == IN_BOX_COUNT)
            {
                SoulLink_SetGraveBox(box);
                StringCopy(GetBoxNamePtr(box), sGraveName);
                break;
            }
        }
    }
    box = gSaveBlock2Ptr->soulLink.header.graveBox;
    if (box >= TOTAL_BOXES_COUNT)
        return -1;
    for (pos = 0; pos < IN_BOX_COUNT; pos++)
        if (GetBoxMonData(GetBoxedMonPtr(box, pos), MON_DATA_SPECIES, NULL) == SPECIES_NONE)
            return pos;
    return -1;
}

static bool8 Matches(struct BoxPokemon *mon, const struct SoulLinkMonIdentity *identity)
{
    u16 species;
    if (GetBoxMonData(mon, MON_DATA_OT_ID, NULL) != identity->otId
     || GetBoxMonData(mon, MON_DATA_PERSONALITY, NULL) != identity->personality)
        return FALSE;
    species = GetBoxMonData(mon, MON_DATA_SPECIES, NULL);
    return species != SPECIES_NONE && species != SPECIES_SHEDINJA;
}

static bool8 MoveDeadMon(const struct SoulLinkMonIdentity *identity)
{
    struct BoxPokemon *source = NULL;
    s16 party = -1, sourceBox = -1, sourcePos = -1, spot;
    u16 i, j, matches = 0, usable = 0;
    u16 zero = 0;
    u8 grave;
    for (i = 0; i < PARTY_SIZE; i++)
    {
        if (GetMonData(&gPlayerParty[i], MON_DATA_HP, NULL) && !GetMonData(&gPlayerParty[i], MON_DATA_IS_EGG, NULL))
            usable++;
        if (Matches(&gPlayerParty[i].box, identity))
        {
            source = &gPlayerParty[i].box;
            party = i;
            matches++;
        }
    }
    for (i = 0; i < TOTAL_BOXES_COUNT; i++)
        for (j = 0; j < IN_BOX_COUNT; j++)
            if (Matches(GetBoxedMonPtr(i, j), identity))
            {
                source = GetBoxedMonPtr(i, j);
                sourceBox = i;
                sourcePos = j;
                matches++;
            }
    for (i = 0; i < 3; i++)
    {
        struct BoxPokemon *mon = i < 2 ? &gSaveBlock1Ptr->daycare.mons[i].mon : &gSaveBlock1Ptr->route5DayCareMon.mon;
        if (Matches(mon, identity))
        {
            source = mon;
            matches++;
        }
    }
    // Mail, ambiguous identities and an entirely fainted party need manual recovery.
    if (matches != 1 || source == NULL || (party >= 0 && (usable == 0 || GetMonData(&gPlayerParty[party], MON_DATA_MAIL, NULL) != MAIL_NONE))
     || ItemIsMail(GetBoxMonData(source, MON_DATA_HELD_ITEM, NULL)))
        return FALSE;
    SetBoxMonData(source, MON_DATA_BOX_HP, &zero);
    grave = gSaveBlock2Ptr->soulLink.header.graveBox;
    if (sourceBox >= 0 && sourceBox == grave)
        return TRUE;
    spot = FindGraveSpot();
    if (spot < 0)
        return FALSE;
    grave = gSaveBlock2Ptr->soulLink.header.graveBox;
    // Copy before deleting the source; no Pokémon or held item is discarded.
    *GetBoxedMonPtr(grave, spot) = *source;
    if (party >= 0)
    {
        ZeroMonData(&gPlayerParty[party]);
        CompactPartySlots();
        CalculatePlayerPartyCount();
    }
    else if (sourceBox >= 0)
        ZeroBoxMonAt(sourceBox, sourcePos);
    else
        ZeroBoxMonData(source);
    return TRUE;
}

void SoulLink_ProcessField(void)
{
    struct SoulLinkPair pair;
    u16 i;
    u8 player;
    bool8 pending = FALSE;
    if (gMain.inBattle || SoulLink_GetSaveStatus() != SOUL_LINK_SAVE_READY || SoulLink_GetPairCount() == 0)
        return;
    if (SoulLink_ApplyPendingLinkedDeaths())
        SoulLink_NotifyDeathSync();
    if (sLocal.fieldTicks++ != 0)
    {
        sLocal.fieldTicks &= 63;
        return;
    }
    player = gSaveBlock2Ptr->soulLink.header.localPlayer;
    for (i = 0; i < PARTY_SIZE; i++)
    {
        struct SoulLinkMonIdentity identity;
        GetIdentity(&gPlayerParty[i].box, &identity);
        if (SoulLink_FindLocalPair(&identity, NULL, &pair) && pair.mons[player].species != identity.species)
        {
            pair.mons[player].species = identity.species;
            SoulLink_StorePair(&pair);
        }
        if (GetMonData(&gPlayerParty[i], MON_DATA_SPECIES, NULL) != SPECIES_NONE
         && GetMonData(&gPlayerParty[i], MON_DATA_HP, NULL) == 0)
            SoulLink_RecordMonDeath(&gPlayerParty[i]);
    }
    for (i = 0; i < SoulLink_GetPairCount(); i++)
    {
        u16 index = (sLocal.cleanupCursor + i) % SoulLink_GetPairCount();
        SoulLink_ReadPair(index, &pair);
        if (pair.encounterStates[player] == SOUL_LINK_ENCOUNTER_DEAD && (pair.cleanupFlags & (1 << player)))
        {
            if (MoveDeadMon(&pair.mons[player]))
            {
                pair.cleanupFlags &= ~(1 << player);
                SoulLink_StorePair(&pair);
            }
            sLocal.cleanupCursor = (index + 1) % SoulLink_GetPairCount();
            break; // Bound expensive storage scans to one candidate per field tick.
        }
    }
    if (sLocal.notice && IsFieldMessageBoxHidden())
    {
        u8 taskId;
        for (i = 0; i < SoulLink_GetPairCount(); i++)
            if (SoulLink_ReadPair(i, &pair) && (pair.cleanupFlags & (1 << player)))
                pending = TRUE;
        taskId = CreateTask(Task_DeathNotice, 80);
        if (taskId != 0xFF)
        {
            if (ShowFieldMessage(sLocal.notice == 2 ? sPairNotice : sLocal.notice == 3 ? sFailedPairNotice
                : sLocal.tradeConflict ? sTradeNotice : pending ? sPendingNotice : sDeathNotice))
            {
                sLocal.notice = FALSE;
                sLocal.tradeConflict = FALSE;
                LockPlayerFieldControls();
            }
            else
                DestroyTask(taskId);
        }
    }
}

void SoulLink_NotifyPairSync(bool8 failed)
{
    if (!sLocal.notice)
        sLocal.notice = failed ? 3 : 2;
    sLocal.fieldTicks = 0;
}

void SoulLink_NotifyDeathSync(void)
{
    sLocal.notice = TRUE;
    sLocal.fieldTicks = 0;
    sLocal.cleanupCursor = 0;
}

void SoulLink_RequestCleanup(void)
{
    struct SoulLinkPair pair;
    u16 i;
    u8 player = gSaveBlock2Ptr->soulLink.header.localPlayer;
    if (SoulLink_GetSaveStatus() != SOUL_LINK_SAVE_READY || player >= SOUL_LINK_PLAYER_COUNT)
        return;
    for (i = 0; i < SoulLink_GetPairCount(); i++)
        if (SoulLink_ReadPair(i, &pair) && pair.encounterStates[player] == SOUL_LINK_ENCOUNTER_DEAD)
        {
            pair.cleanupFlags |= 1 << player;
            SoulLink_StorePair(&pair);
        }
    sLocal.fieldTicks = 0;
    sLocal.cleanupCursor = 0;
}

bool8 SoulLink_TransferNpcTrade(struct Pokemon *oldMon, struct Pokemon *newMon)
{
    struct SoulLinkMonIdentity oldIdentity, newIdentity;
    if (SoulLink_ArePartyChangesLocked()
     || !SoulLink_CanUseSpecies(&oldMon->box, GetMonData(newMon, MON_DATA_SPECIES, NULL)))
    {
        sLocal.tradeConflict = TRUE; sLocal.notice = TRUE; sLocal.fieldTicks = 0;
        return FALSE;
    }
    GetIdentity(&oldMon->box, &oldIdentity);
    GetIdentity(&newMon->box, &newIdentity);
    if (!SoulLink_FindLocalPair(&oldIdentity, NULL, NULL))
        return TRUE;
    if (SoulLink_ReplaceLocalIdentity(&oldIdentity, &newIdentity))
        return TRUE;
    sLocal.tradeConflict = TRUE;
    sLocal.notice = TRUE;
    sLocal.fieldTicks = 0;
    return FALSE;
}
