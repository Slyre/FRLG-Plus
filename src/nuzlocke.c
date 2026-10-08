#include "global.h"
#include "nuzlocke.h"
#include "soul_link.h"

bool8 Nuzlocke_IsActive(void)
{
    return gSaveBlock1Ptr->keyFlags.nuzlocke == 1 || SoulLink_IsEnabled();
}

bool8 Nuzlocke_IsFirstEncounterOnly(void)
{
    struct SoulLinkConfig config;
    if (SoulLink_IsEnabled() && SoulLink_GetConfig(&config))
        return config.firstEncounterOnly;
    return gSaveBlock1Ptr->keyFlags.nuzlocke == 1;
}

bool8 Nuzlocke_IsDupesClauseEnabled(void)
{
    struct SoulLinkConfig config;
    if (SoulLink_IsEnabled() && SoulLink_GetConfig(&config))
        return config.dupesClause;
    return TRUE;
}

bool8 Nuzlocke_HasPermanentDeaths(void)
{
    struct SoulLinkPair pair;
    u16 i;
    u8 player = gSaveBlock2Ptr->soulLink.header.localPlayer;
    if (Nuzlocke_IsActive())
        return TRUE;
    if (player >= SOUL_LINK_PLAYER_COUNT)
        return FALSE;
    for (i = 0; i < SoulLink_GetPairCount(); i++)
        if (SoulLink_ReadPair(i, &pair) && pair.encounterStates[player] == SOUL_LINK_ENCOUNTER_DEAD)
            return TRUE;
    return FALSE;
}
