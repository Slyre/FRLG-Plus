#define main ProtocolRegressionMain
#include "test_protocol.c"
#undef main

static void LinkedRule(bool8 enabled)
{
    unsigned player;
    for (player = 0; player < 2; player++)
    {
        struct SoulLinkConfig config;
        Select(player); assert(SoulLink_GetConfig(&config));
        config.linkedDeaths = enabled; assert(SoulLink_SetConfig(&config));
    }
}

int main(void)
{
    unsigned victim, player;
    struct SoulLinkPair pair, result;
    for (victim = 0; victim < 2; victim++)
    {
        Setup(); pair = Bound(1); PutBoth(&pair);
        Select(victim); assert(SoulLink_MarkLocalDeath(&pair.mons[victim]));
        InitBoth(); Round(); Round();
        Select(victim ^ 1); assert(SoulLink_ReadPair(0, &result));
        assert(result.state == SOUL_LINK_PAIR_ALIVE); // Interrupted snapshot has not committed.
        Sync(); Sync();
        for (player = 0; player < 2; player++)
        {
            Select(player); assert(SoulLink_ReadPair(0, &result));
            assert(result.encounterStates[0] == SOUL_LINK_ENCOUNTER_DEAD);
            assert(result.encounterStates[1] == SOUL_LINK_ENCOUNTER_DEAD);
            assert(result.cleanupFlags == (1u << player));
            assert(SoulLink_ApplyPendingLinkedDeaths() == 0);
        }
    }
    Setup(); pair = Bound(1); PutBoth(&pair); LinkedRule(FALSE);
    Select(0); assert(SoulLink_MarkLocalDeath(&pair.mons[0])); Sync();
    Select(1); assert(SoulLink_ReadPair(0, &result));
    assert(result.encounterStates[1] == SOUL_LINK_ENCOUNTER_CAPTURED && result.cleanupFlags == 0);
    assert(SoulLink_ApplyPendingLinkedDeaths() == 0);
    LinkedRule(TRUE);
    Select(1); assert(SoulLink_ApplyPendingLinkedDeaths() == 1);
    assert(SoulLink_ApplyPendingLinkedDeaths() == 0);
    Sync(); LinkedRule(FALSE); Sync();
    Select(1); assert(SoulLink_ReadPair(0, &result));
    assert(result.encounterStates[1] == SOUL_LINK_ENCOUNTER_DEAD); // Rule changes cannot resurrect.
    Setup(); pair = Bound(1);
    memset(&pair.mons[1], 0, sizeof(pair.mons[1]));
    pair.mons[1].encounterOrdinal = 1;
    pair.encounterStates[1] = SOUL_LINK_ENCOUNTER_FAILED;
    pair.state = SOUL_LINK_PAIR_FAILED; PutBoth(&pair);
    Select(0); assert(SoulLink_MarkLocalDeath(&pair.mons[0])); Sync();
    Select(1); assert(SoulLink_ReadPair(0, &result));
    assert(result.encounterStates[1] == SOUL_LINK_ENCOUNTER_FAILED && result.cleanupFlags == 0);
    puts("Soul Link linked deaths: both directions, interrupted snapshots, rule toggle, idempotence and failed encounter passed");
    return 0;
}
