// Frame-sliced reconciliation. No save write occurs before the final step.
struct SoulLinkApplyJob
{
    struct SoulLinkSaveHeader header;
    struct SoulLinkPair remote[SOUL_LINK_PAIR_CAPACITY];
    struct SoulLinkPair local[SOUL_LINK_PAIR_CAPACITY];
    struct SoulLinkPair merged[SOUL_LINK_PAIR_CAPACITY];
    u32 nextId;
    u16 count, total, cursor, ordinal;
    u8 player, phase;
    u8 validationSlots[5][256];
};

u32 SoulLink_ApplyJobSize(void) { return sizeof(struct SoulLinkApplyJob); }

bool8 SoulLink_BeginApplyJob(struct SoulLinkApplyJob *job, const struct SoulLinkPair *remote, u16 count, u8 player)
{
    u16 i;
    if (job == NULL || remote == NULL || count > SOUL_LINK_PAIR_CAPACITY
     || !SoulLink_IsEnabled() || GetHeader()->pairCount > SOUL_LINK_PAIR_CAPACITY
     || player >= SOUL_LINK_PLAYER_COUNT || player == GetHeader()->localPlayer) return FALSE;
    job->nextId = 0;
    job->cursor = job->ordinal = 0;
    job->phase = 0;
    job->header = *GetHeader();
    job->count = count; job->player = player;
    memcpy(job->remote, remote, count * sizeof(*remote));
    for (i = 0; i < job->header.pairCount; i++) job->local[i] = *GetPair(i);
    if (player == 1)
    {
        job->total = job->header.pairCount;
        memcpy(job->merged, job->local, job->total * sizeof(*remote));
    }
    else job->total = count;
    return TRUE;
}

static bool8 ApplyJobUnchanged(const struct SoulLinkApplyJob *job)
{
    static const u8 bankSizes[] = {SOUL_LINK_PRIMARY_PAIR_COUNT, 2, 2, 1, 5, 1,
        SOUL_LINK_PAIR_CAPACITY - SOUL_LINK_PRIMARY_PAIR_COUNT - 11};
    u16 i = 0;
    u8 bank;
    if (!SoulLink_IsEnabled() || memcmp(GetHeader(), &job->header, sizeof(job->header))) return FALSE;
    // GetPair's seven storage banks are contiguous internally. Compare every
    // byte, using one memcmp per bank instead of one per row.
    for (bank = 0; bank < ARRAY_COUNT(bankSizes) && i < job->header.pairCount; bank++)
    {
        u16 count = job->header.pairCount - i;
        if (count > bankSizes[bank]) count = bankSizes[bank];
        if (memcmp(GetPair(i), &job->local[i], count * sizeof(job->local[i]))) return FALSE;
        i += count;
    }
    return TRUE;
}

static bool8 ApplyJobValidRow(struct SoulLinkApplyJob *job, const struct SoulLinkPair *rows, u16 i, u8 player)
{
    u8 field;
    if (i == 0) memset(job->validationSlots, 0, sizeof(job->validationSlots));
    if (!IsPairValidForPlayer(&rows[i], player)) return FALSE;
    for (field = 0; field < 5; field++)
    {
        u8 side = field > 2;
        const struct SoulLinkMonIdentity *mon = &rows[i].mons[side];
        u32 key;
        u8 slot;
        u8 *slots = job->validationSlots[field];
        if (field == 0)
        {
            key = rows[i].pairId;
            if (key == 0) continue;
        }
        else
        {
            if (rows[i].encounterStates[side] == SOUL_LINK_ENCOUNTER_NONE) continue;
            if (field == 1 || field == 3) key = mon->encounterOrdinal;
            else
            {
                if (mon->species == SPECIES_NONE) continue;
                key = mon->otId ^ (mon->personality * 33u);
            }
        }
        key ^= key >> 16; key *= 0x9E3779B1u; key ^= key >> 16;
        slot = key;
        while (slots[slot])
        {
            const struct SoulLinkPair *other = &rows[slots[slot] - 1];
            if ((field == 0 && rows[i].pairId == other->pairId)
             || ((field == 1 || field == 3) && mon->encounterOrdinal == other->mons[side].encounterOrdinal)
             || ((field == 2 || field == 4) && SameMon(mon, &other->mons[side]))) return FALSE;
            slot++;
        }
        slots[slot] = i + 1;
    }
    return TRUE;
}

static bool8 ApplyJobExisting(struct SoulLinkApplyJob *job, u16 i)
{
    const struct SoulLinkPair *remote = job->remote;
    struct SoulLinkPair *merged = job->merged;
    u16 j, total = job->total;
    u8 player = job->player, local = job->header.localPlayer;
    const struct SoulLinkPair *incoming = &remote[i];
    struct SoulLinkPair *row;
    u8 before, after;
    if (incoming->pairId == SOUL_LINK_PAIR_NONE)
        return TRUE;
    for (j = 0; j < total; j++)
        if (merged[j].pairId == incoming->pairId)
            break;
    if (j == total)
    {
        if (total == SOUL_LINK_PAIR_CAPACITY)
            return FALSE;
        merged[total++] = *incoming;
        merged[j].cleanupFlags = incoming->encounterStates[local] == SOUL_LINK_ENCOUNTER_DEAD ? 1 << local : 0;

        job->total = total;
        return TRUE;
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
        return TRUE; // A stale snapshot cannot undo a terminal observation.
    if (before == SOUL_LINK_ENCOUNTER_CAPTURED && after == SOUL_LINK_ENCOUNTER_FAILED)
        return FALSE;
    row->mons[player] = incoming->mons[player];
    row->encounterStates[player] = after;
    row->cleanupFlags &= ~(1 << player);
    row->state = GetPairState(row);

    job->total = total;
    return TRUE;
}

static bool8 ApplyJobCoordinator(struct SoulLinkApplyJob *job, u16 i)
{
    const struct SoulLinkPair *remote = job->remote, *localRows = job->local;
    struct SoulLinkPair *merged = job->merged;
    u16 j;
    const struct SoulLinkPair *incoming = &remote[i];
    const struct SoulLinkPair *old = NULL;
    struct SoulLinkPair *row = &merged[i];
    *row = *incoming;
    row->cleanupFlags = 0;
    // Prefer the local encounter over a possibly empty replica of this ID.
    for (j = 0; j < job->header.pairCount; j++)
        if (incoming->encounterStates[1] != SOUL_LINK_ENCOUNTER_NONE
         && localRows[j].mons[1].encounterOrdinal == incoming->mons[1].encounterOrdinal)
        {
            const struct SoulLinkPair *candidate = &localRows[j];
            if ((candidate->pairId != 0 && candidate->pairId != incoming->pairId)
             || !AreasMatch(candidate->areaId, incoming->areaId)) return FALSE;
            old = candidate;
            break;
        }
    if (old == NULL && incoming->pairId != 0)
        for (j = 0; j < job->header.pairCount; j++)
            if (localRows[j].pairId == incoming->pairId) { old = &localRows[j]; break; }
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

    return TRUE;
}

static bool8 ApplyJobCandidate(struct SoulLinkApplyJob *job)
{
    const struct SoulLinkPair *remote = job->remote;
    struct SoulLinkPair *merged = job->merged;
    u16 i, j, count = job->count, total = job->total, ordinal = job->ordinal;
    u32 nextId = job->nextId;
    const struct SoulLinkPair *candidate = NULL;
    u16 best = SOUL_LINK_PAIR_CAPACITY;
    // Match candidates by their encounter order, independently of table order.
    for (i = 0; i < count; i++)
        if (remote[i].pairId == 0 && remote[i].mons[1].encounterOrdinal > ordinal
         && (candidate == NULL || remote[i].mons[1].encounterOrdinal < candidate->mons[1].encounterOrdinal))
            candidate = &remote[i];
    if (candidate == NULL)
        return TRUE;
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

    job->ordinal = ordinal; job->total = total; job->nextId = nextId;
    return TRUE;
}

static bool8 ApplyJobCanCommitRow(struct SoulLinkApplyJob *job, u16 i)
{
    const struct SoulLinkPair *merged = job->merged;
    const struct SoulLinkSaveHeader *header = &job->header;
    u16 count = job->total;
    u16 j;
    const struct SoulLinkPair *old = &job->local[i];
    const struct SoulLinkPair *updated;
    u8 local = header->localPlayer;
    bool8 restoredDeath;
    struct SoulLinkPair compatible;
    for (j = 0; j < count; j++)
        if (old->pairId ? old->pairId == merged[j].pairId
            : old->mons[local].encounterOrdinal == merged[j].mons[local].encounterOrdinal) break;
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

    return TRUE;
}

// 0: pending, 1: committed, 2: rejected, 3: local state changed; retry.
u8 SoulLink_StepApplyJob(struct SoulLinkApplyJob *job)
{
    u8 budget = 16;
    if (job == NULL || job->phase > 7) return 2;
    // Exact comparison includes cleanup, evolution, rules, team, participants,
    // IDs and the run. Never rely on a hash to authorize a delayed save write.
    if (!ApplyJobUnchanged(job)) return 3;
    while (budget--)
    {
        u16 i = job->cursor;
        switch (job->phase)
        {
        case 0:
            if (i < job->count || (job->player == 0 && i < job->header.pairCount))
            {
                u16 j;
                if (i < job->count && !ApplyJobValidRow(job, job->remote, i, job->player)) return 2;
                if (job->player == 0 && i < job->header.pairCount)
                    for (j = 0; j < i; j++)
                        if ((job->local[i].pairId != 0 && job->local[i].pairId == job->local[j].pairId)
                         || (job->local[i].mons[1].encounterOrdinal != 0
                          && job->local[i].mons[1].encounterOrdinal == job->local[j].mons[1].encounterOrdinal)) return 2;
                job->cursor++; break;
            }
            job->phase++; job->cursor = 0; break;
        case 1:
            if (i < job->count)
            {
                if (!(job->player == 1 ? ApplyJobExisting(job, i) : ApplyJobCoordinator(job, i))) return 2;
                job->cursor++; break;
            }
            job->phase++; job->cursor = 0; break;
        case 2:
            if (job->player == 1)
            {
                if (i < job->total)
                {
                    if (!ApplyJobValidRow(job, job->merged, i, job->header.localPlayer)) return 2;
                    job->cursor++; break;
                }
                job->nextId = job->header.nextPairId;
                for (i = 0; i < job->total; i++)
                    if (job->nextId != 0 && job->merged[i].pairId >= job->nextId)
                        job->nextId = (u32)job->merged[i].pairId + 1;
            }
            else if (i < job->header.pairCount)
            {
                u16 j;
                const struct SoulLinkPair *old = &job->local[i];
                for (j = 0; j < job->count; j++)
                    if ((old->pairId != 0 && old->pairId == job->merged[j].pairId)
                     || (old->pairId == 0 && old->mons[1].encounterOrdinal == job->merged[j].mons[1].encounterOrdinal)) break;
                if (j == job->count) return 2;
                job->cursor++; break;
            }
            job->phase++; job->cursor = 0; break;
        case 3:
            if (job->player == 1)
            {
                u16 ordinal = job->ordinal;
                if (!ApplyJobCandidate(job)) return 2;
                if (ordinal != job->ordinal)
                {
                    // Candidate matching scans two tables; charge four rows.
                    budget = budget >= 3 ? budget - 3 : 0;
                    break;
                }
            }
            job->phase++; job->cursor = 0; break;
        case 4:
            if (i < job->total)
            {
                PropagateLinkedDeaths(&job->merged[i], 1);
                job->cursor++; break;
            }
            job->phase++; job->cursor = 0; break;
        case 5:
            if (i < job->total)
            {
                if (!ApplyJobValidRow(job, job->merged, i, job->header.localPlayer)) return 2;
                job->cursor++; break;
            }
            job->phase++; job->cursor = 0; break;
        case 6:
            if (i < job->header.pairCount)
            {
                if (!ApplyJobCanCommitRow(job, i)) return 2;
                job->cursor++; break;
            }
            job->phase++; break;
        case 7:
            // No yield between the exact baseline check and this atomic write.
            CommitValidatedRemoteState(job->merged, job->total);
            job->phase = 8;
            return 1;
        }
    }
    return 0;
}
