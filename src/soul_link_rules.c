#include "global.h"
#include "pokemon.h"
#include "soul_link.h"
#include "soul_link_rules.h"

bool8 SoulLink_ArePairTypesAllowed(u16 speciesA, u16 speciesB, u8 mode)
{
    u8 i, j;
    if (mode == SOUL_LINK_TYPE_NONE)
        return TRUE;
    if (mode >= SOUL_LINK_TYPE_COUNT || speciesA == SPECIES_NONE || speciesA >= NUM_SPECIES
     || speciesB == SPECIES_NONE || speciesB >= NUM_SPECIES)
        return FALSE;
    for (i = 0; i < (mode == SOUL_LINK_TYPE_PRIMARY ? 1 : 2); i++)
        for (j = 0; j < (mode == SOUL_LINK_TYPE_PRIMARY ? 1 : 2); j++)
            if (gSpeciesInfo[speciesA].types[i] == gSpeciesInfo[speciesB].types[j])
                return FALSE;
    return TRUE;
}

static bool8 CanUseWithConfig(struct BoxPokemon *mon, u16 species, const struct SoulLinkConfig *config)
{
    struct SoulLinkMonIdentity identity;
    struct SoulLinkPair pair;
    u8 local = gSaveBlock2Ptr->soulLink.header.localPlayer;
    if (!config->enabled || config->typeClause == SOUL_LINK_TYPE_NONE)
        return TRUE;
    identity.otId = GetBoxMonData(mon, MON_DATA_OT_ID, NULL);
    identity.personality = GetBoxMonData(mon, MON_DATA_PERSONALITY, NULL);
    identity.species = GetBoxMonData(mon, MON_DATA_SPECIES, NULL);
    identity.encounterOrdinal = 0;
    if (!SoulLink_FindLocalPair(&identity, NULL, &pair) || pair.state != SOUL_LINK_PAIR_ALIVE)
        return TRUE;
    return SoulLink_ArePairTypesAllowed(species, pair.mons[local ^ 1].species, config->typeClause);
}

bool8 SoulLink_CanUseSpecies(struct BoxPokemon *mon, u16 species)
{
    struct SoulLinkConfig config;
    return !SoulLink_GetConfig(&config) || CanUseWithConfig(mon, species, &config);
}

bool8 SoulLink_ValidatePartyRules(const struct SoulLinkConfig *config)
{
    u8 i, j;
    struct SoulLinkPair pair;
    struct SoulLinkMonIdentity identity;
    bool8 seen[6] = {FALSE};
    struct SoulLinkTeamSelection *team = &gSaveBlock2Ptr->soulLink.header.team;
    if (config == NULL)
        return FALSE;
    for (i = 0; i < PARTY_SIZE; i++)
    {
        u16 species = GetMonData(&gPlayerParty[i], MON_DATA_SPECIES, NULL);
        if (species != SPECIES_NONE && config->enabled && config->enforcePartyPairs && team->locked
         && GetMonData(&gPlayerParty[i], MON_DATA_HP, NULL) != 0)
        {
            identity.otId = GetMonData(&gPlayerParty[i], MON_DATA_OT_ID, NULL);
            identity.personality = GetMonData(&gPlayerParty[i], MON_DATA_PERSONALITY, NULL);
            identity.species = species; identity.encounterOrdinal = 0;
            if (!SoulLink_FindLocalPair(&identity, NULL, &pair) || pair.state != SOUL_LINK_PAIR_ALIVE) return FALSE;
            for (j = 0; j < team->count; j++) if (team->pairIds[j] == pair.pairId) break;
            if (j == team->count || seen[j]) return FALSE;
            seen[j] = TRUE;
        }
        if (species != SPECIES_NONE && !GetMonData(&gPlayerParty[i], MON_DATA_IS_EGG, NULL)
         && !CanUseWithConfig(&gPlayerParty[i].box, species, config))
            return FALSE;
    }
    if (config->enabled && config->enforcePartyPairs && team->locked)
        for (j = 0; j < team->count; j++)
        {
            u16 row;
            for (row = 0; row < SoulLink_GetPairCount(); row++)
                if (SoulLink_ReadPair(row, &pair) && pair.pairId == team->pairIds[j]) break;
            if (row == SoulLink_GetPairCount() || (pair.state == SOUL_LINK_PAIR_ALIVE && !seen[j])) return FALSE;
        }
    return TRUE;
}

bool8 SoulLink_IsPartyMon(struct Pokemon *mon)
{
    u8 i;
    u32 ot = GetMonData(mon, MON_DATA_OT_ID, NULL);
    u32 pid = GetMonData(mon, MON_DATA_PERSONALITY, NULL);
    for (i = 0; i < PARTY_SIZE; i++)
        if (GetMonData(&gPlayerParty[i], MON_DATA_SPECIES, NULL) != SPECIES_NONE
         && GetMonData(&gPlayerParty[i], MON_DATA_OT_ID, NULL) == ot
         && GetMonData(&gPlayerParty[i], MON_DATA_PERSONALITY, NULL) == pid) return TRUE;
    return FALSE;
}
