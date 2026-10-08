#include "global.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "item.h"
#include "mail_data.h"
#include "constants/items.h"
#include "soul_link.h"
#include "soul_link_local.h"
#include "soul_link_rules.h"
#include "soul_link_team.h"

static bool8 Matches(struct BoxPokemon *mon, const struct SoulLinkMonIdentity *identity)
{
    u16 species;
    // Reject actual duplicates; an old unpaired evolution bonus is not its parent.
    if (mon->otId != identity->otId || mon->personality != identity->personality)
        return FALSE;
    species = GetBoxMonData(mon, MON_DATA_SPECIES, NULL);
    return species != SPECIES_NONE
        && !(species == SPECIES_SHEDINJA
          && (identity->species == SPECIES_NINCADA || identity->species == SPECIES_NINJASK));
}

static bool8 RefuseTeam(struct SoulLinkTeamPlan *plan, u8 reason)
{
    plan->diagnostic.reason = reason;
    return FALSE;
}

void SoulLink_DescribeTeamChange(void *context, struct SoulLinkTeamDiagnostic *diagnostic)
{
    *diagnostic = ((struct SoulLinkTeamPlan *)context)->diagnostic;
}

bool8 SoulLink_PrepareTeamChange(void *context, const SoulLinkPairId *ids, u8 count)
{
    struct SoulLinkTeamPlan *plan = context;
    struct SoulLinkPair pair;
    struct SoulLinkPair chosen[PARTY_SIZE];
    bool8 found[PARTY_SIZE] = {FALSE};
    u8 i, j, box, slot, local = gSaveBlock2Ptr->soulLink.header.localPlayer;
    bool8 selected[PARTY_SIZE] = {FALSE};
    u16 row = 0, rows, matches;
    if (plan == NULL) return FALSE;
    memset(plan, 0, sizeof(*plan));
    if (count == 0 || count > PARTY_SIZE || ids == NULL)
        return RefuseTeam(plan, SOUL_LINK_TEAM_SELECTION);
    plan->count = count;
    rows = SoulLink_GetPairCount();
    for (i = 0; i < count; i++)
    {
        plan->diagnostic.pairId = ids[i];
        if (!ids[i] || (i && ids[i] <= ids[i - 1])) return RefuseTeam(plan, SOUL_LINK_TEAM_INVALID_PAIR);
        // Read the registry once for this synchronous preparation. Retain the
        // first matching row and check each selection in the original order.
        while (!found[i] && row < rows)
        {
                if (SoulLink_ReadPair(row++, &pair))
                    for (j = i; j < count; j++)
                        if (!found[j] && pair.pairId == ids[j])
                        {
                            chosen[j] = pair;
                            found[j] = TRUE;
                        }
        }
        if (!found[i] || chosen[i].state != SOUL_LINK_PAIR_ALIVE) return RefuseTeam(plan, SOUL_LINK_TEAM_INVALID_PAIR);
        pair = chosen[i];
        plan->diagnostic.species = pair.mons[local].species;
        matches = 0;
        for (j = 0; j < PARTY_SIZE; j++)
            if (Matches(&gPlayerParty[j].box, &pair.mons[local]))
            {
                matches++;
                selected[j] = TRUE;
                plan->party[i] = gPlayerParty[j];
            }
        for (box = 0; box < TOTAL_BOXES_COUNT; box++)
        {
            struct BoxPokemon *boxMons = GetBoxedMonPtr(box, 0);
            for (slot = 0; slot < IN_BOX_COUNT; slot++)
            {
                struct BoxPokemon *mon = &boxMons[slot];
                if (Matches(mon, &pair.mons[local]))
                {
                    matches++;
                    if (SoulLink_IsGraveBox(box)) return RefuseTeam(plan, SOUL_LINK_TEAM_GRAVE);
                    plan->sources[i] = mon;
                    BoxMonToMon(mon, &plan->party[i]);
                    PopulateBoxHpAndStatusToPartyMon(&plan->party[i]);
                }
            }
        }
        {
            bool8 inDaycare = FALSE;
            for (j = 0; j < 2; j++)
                if (Matches(&gSaveBlock1Ptr->daycare.mons[j].mon, &pair.mons[local])) { matches++; inDaycare = TRUE; }
            if (Matches(&gSaveBlock1Ptr->route5DayCareMon.mon, &pair.mons[local])) { matches++; inDaycare = TRUE; }
            if (matches == 0) return RefuseTeam(plan, SOUL_LINK_TEAM_MISSING);
            if (matches > 1) return RefuseTeam(plan, SOUL_LINK_TEAM_DUPLICATE);
            if (inDaycare) return RefuseTeam(plan, SOUL_LINK_TEAM_DAYCARE);
        }
        plan->diagnostic.species = GetMonData(&plan->party[i], MON_DATA_SPECIES, NULL);
        if (GetMonData(&plan->party[i], MON_DATA_IS_EGG, NULL)) return RefuseTeam(plan, SOUL_LINK_TEAM_EGG);
        if (GetMonData(&plan->party[i], MON_DATA_HP, NULL) == 0) return RefuseTeam(plan, SOUL_LINK_TEAM_FAINTED);
        if (GetMonData(&plan->party[i], MON_DATA_MAIL, NULL) != MAIL_NONE
         || ItemIsMail(GetMonData(&plan->party[i], MON_DATA_HELD_ITEM, NULL))) return RefuseTeam(plan, SOUL_LINK_TEAM_MAIL);
        if (!SoulLink_CanUseSpecies(&plan->party[i].box, plan->diagnostic.species)) return RefuseTeam(plan, SOUL_LINK_TEAM_TYPES);
        plan->ids[i] = ids[i];
    }
    for (j = 0; j < PARTY_SIZE; j++)
        if (!selected[j] && GetMonData(&gPlayerParty[j], MON_DATA_SPECIES, NULL) != SPECIES_NONE)
        {
            if (GetMonData(&gPlayerParty[j], MON_DATA_MAIL, NULL) != MAIL_NONE
             || ItemIsMail(GetMonData(&gPlayerParty[j], MON_DATA_HELD_ITEM, NULL)))
            {
                struct SoulLinkMonIdentity identity;
                plan->diagnostic.species = GetMonData(&gPlayerParty[j], MON_DATA_SPECIES, NULL);
                identity.otId = GetMonData(&gPlayerParty[j], MON_DATA_OT_ID, NULL);
                identity.personality = GetMonData(&gPlayerParty[j], MON_DATA_PERSONALITY, NULL);
                identity.species = plan->diagnostic.species; identity.encounterOrdinal = 0;
                plan->diagnostic.pairId = SoulLink_FindLocalPair(&identity, NULL, &pair) ? pair.pairId : 0;
                return RefuseTeam(plan, SOUL_LINK_TEAM_MAIL);
            }
            plan->deposits[plan->depositCount++] = gPlayerParty[j];
        }
    // Boxed selections free valid, non-grave slots. Reuse those first so a
    // full-PC swap does not search every occupied body for an empty slot.
    j = 0;
    for (i = 0; i < count && j < plan->depositCount; i++)
        if (plan->sources[i]) plan->destinations[j++] = plan->sources[i];
    for (box = 0; box < TOTAL_BOXES_COUNT && j < plan->depositCount; box++)
    {
        if (SoulLink_IsGraveBox(box)) continue;
        for (slot = 0; slot < IN_BOX_COUNT && j < plan->depositCount; slot++)
        {
            struct BoxPokemon *mon = GetBoxedMonPtr(box, slot);
            bool8 source = FALSE;
            for (i = 0; i < count; i++) if (plan->sources[i] == mon) source = TRUE;
            if (!source && GetBoxMonData(mon, MON_DATA_SPECIES, NULL) == SPECIES_NONE)
                plan->destinations[j++] = mon;
        }
    }
    plan->diagnostic.pairId = 0; plan->diagnostic.species = 0;
    plan->diagnostic.neededSlots = plan->depositCount;
    plan->diagnostic.availableSlots = j;
    if (j != plan->depositCount) return RefuseTeam(plan, SOUL_LINK_TEAM_PC_SPACE);
    if (gSaveBlock2Ptr->soulLink.header.team.revision == 0xFFFFFFFF) return RefuseTeam(plan, SOUL_LINK_TEAM_REVISION);
    return TRUE;
}

void SoulLink_CommitTeamChange(void *context)
{
    struct SoulLinkTeamPlan *plan = context;
    u8 i;
    // All sources were copied before any writes, including destinations reusing them.
    for (i = 0; i < plan->count; i++)
        if (plan->sources[i]) memset(plan->sources[i], 0, sizeof(struct BoxPokemon));
    for (i = 0; i < plan->depositCount; i++)
    {
        StoreHPAndStatusInBoxMon(&plan->deposits[i]);
        *plan->destinations[i] = plan->deposits[i].box;
    }
    memset(gPlayerParty, 0, sizeof(struct Pokemon) * PARTY_SIZE);
    for (i = 0; i < plan->count; i++) gPlayerParty[i] = plan->party[i];
    CalculatePlayerPartyCount();
    SoulLink_SetTeamSelection(plan->ids, plan->count);
    gSaveBlock2Ptr->soulLink.header.team.locked = TRUE;
}

void SoulLink_SelectCurrentParty(void)
{
    SoulLinkPairId ids[PARTY_SIZE];
    struct SoulLinkPair pair;
    struct SoulLinkMonIdentity identity;
    u8 i, j, count = 0;
    if (gSaveBlock2Ptr->soulLink.header.localPlayer != 0 || gSaveBlock2Ptr->soulLink.header.team.count) return;
    for (i = 0; i < PARTY_SIZE; i++)
    {
        identity.otId = GetMonData(&gPlayerParty[i], MON_DATA_OT_ID, NULL);
        identity.personality = GetMonData(&gPlayerParty[i], MON_DATA_PERSONALITY, NULL);
        identity.species = GetMonData(&gPlayerParty[i], MON_DATA_SPECIES, NULL);
        identity.encounterOrdinal = 0;
        if (!SoulLink_FindLocalPair(&identity, NULL, &pair) || pair.state != SOUL_LINK_PAIR_ALIVE) continue;
        for (j = count; j && ids[j - 1] > pair.pairId; j--) ids[j] = ids[j - 1];
        ids[j] = pair.pairId;
        count++;
    }
    SoulLink_SetTeamSelection(ids, count);
}
