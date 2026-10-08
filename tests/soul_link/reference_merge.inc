// Frozen linear-search reference from the previous local-apply release.
static bool8 ReferenceCanCommit(const struct SoulLinkPair *merged, u16 count)
{
    struct SoulLinkSaveHeader *header = GetHeader();
    u16 i;
    if (!SoulLink_IsEnabled()
     || !Reference(merged, count, header->localPlayer))
        return FALSE;
    for (i = 0; i < header->pairCount; i++)
    {
        u16 j;
        const struct SoulLinkPair *old = GetPair(i);
        const struct SoulLinkPair *updated;
        u8 local = header->localPlayer;
        bool8 restoredDeath;
        struct SoulLinkPair compatible;
        for (j = 0; j < count; j++)
            if ((old->pairId != 0 && old->pairId == merged[j].pairId)
             || (old->pairId == 0 && old->mons[local].encounterOrdinal == merged[j].mons[local].encounterOrdinal))
                break;
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


static bool8 ReferenceCoordinator(const struct SoulLinkPair *remote, u16 count,
                                    struct SoulLinkPair *merged, u16 *mergedCount)
{
    const struct SoulLinkPair *localRows[SOUL_LINK_PAIR_CAPACITY];
    u16 i, j;
    if (!Reference(remote, count, 0))
        return FALSE;
    if (GetHeader()->pairCount > SOUL_LINK_PAIR_CAPACITY) return FALSE;
    for (j = 0; j < GetHeader()->pairCount; j++) localRows[j] = GetPair(j);
    for (i = 0; i < count; i++)
    {
        const struct SoulLinkPair *incoming = &remote[i];
        const struct SoulLinkPair *old = NULL;
        struct SoulLinkPair *row = &merged[i];
        *row = *incoming;
        row->cleanupFlags = 0;
        // Prefer the local encounter over a possibly empty replica of this ID.
        for (j = 0; j < GetHeader()->pairCount; j++)
        {
            const struct SoulLinkPair *candidate = localRows[j];
            if (incoming->encounterStates[1] != SOUL_LINK_ENCOUNTER_NONE
             && candidate->encounterStates[1] != SOUL_LINK_ENCOUNTER_NONE
             && candidate->mons[1].encounterOrdinal == incoming->mons[1].encounterOrdinal)
            {
                if ((candidate->pairId != 0 && candidate->pairId != incoming->pairId)
                 || !AreasMatch(candidate->areaId, incoming->areaId))
                    return FALSE;
                old = candidate;
                break;
            }
            if (candidate->pairId == incoming->pairId)
                old = candidate;
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
    if (!Reference(merged, count, 1) || !ReferenceCanCommit(merged, count))
        return FALSE;
    *mergedCount = count;
    return TRUE;
}
