#define RECONCILE_HELPERS
#include "test_captures.c"

static struct Endpoint sPersisted[2];
static unsigned sCuts;

static void RestartBoth(void)
{
    unsigned player;
    // Preserve only save blocks/storage: discard all transport runtime state.
    SoulLink_ProtocolForgetBaseline(0); SoulLink_ProtocolForgetBaseline(1);
    memcpy(sPersisted, sPeers, sizeof(sPeers));
    memset(sPeers, 0, sizeof(sPeers));
    for (player = 0; player < 2; player++)
    {
        sPeers[player].block1 = sPersisted[player].block1;
        sPeers[player].block2 = sPersisted[player].block2;
        sPeers[player].storage = sPersisted[player].storage;
        Select(player);
    }
}

static void Fixture(unsigned scenario)
{
    struct SoulLinkPair pair;
    Setup();
    if (scenario == 0)
    {
        Capture(0, 8, 101); Capture(1, 8, 201);
        Capture(0, 9, 102); Capture(1, 10, 202);
    }
    else
    {
        pair = Bound(5); PutBoth(&pair);
        Select(scenario - 1); assert(SoulLink_MarkLocalDeath(&pair.mons[scenario - 1]));
    }
}

static void CheckResult(unsigned scenario)
{
    unsigned player;
    Mirrors();
    for (player = 0; player < 2; player++)
    {
        struct SoulLinkPair pair = ReadId(player, scenario == 0 ? 1 : 5);
        if (scenario == 0)
        {
            assert(pair.state == SOUL_LINK_PAIR_ALIVE);
            assert(pair.mons[0].personality == 101 && pair.mons[1].personality == 201);
            assert(SoulLink_GetPairCount() == 3);
            assert(gSaveBlock2Ptr->soulLink.header.nextPairId == 4);
            assert(ReadId(player, 2).state == SOUL_LINK_PAIR_PENDING);
            assert(ReadId(player, 3).state == SOUL_LINK_PAIR_PENDING);
        }
        else
        {
            assert(pair.encounterStates[0] == SOUL_LINK_ENCOUNTER_DEAD);
            assert(pair.encounterStates[1] == SOUL_LINK_ENCOUNTER_DEAD);
            assert(pair.cleanupFlags == (1u << player));
        }
    }
}

static void TestEveryBoundary(void)
{
    unsigned scenario, cut, recipient, round, player;
    for (scenario = 0; scenario < 3; scenario++)
    {
        unsigned rounds = 0;
        Fixture(scenario); InitBoth();
        while (sPeers[0].protocol.status == SOUL_LINK_SESSION_SYNCING) { Round(); assert(++rounds < 182); }
        for (cut = 0; cut < rounds; cut++)
            for (recipient = 0; recipient < 3; recipient++)
            {
                struct SoulLinkPacket packets[2];
                Fixture(scenario); InitBoth();
                for (round = 0; round < cut; round++) Round();
                // Cut before delivery, or after delivery to exactly one side.
                for (player = 0; player < 2; player++)
                {
                    Select(player); assert(SoulLink_ProtocolBuild(&sPeers[player].protocol));
                    packets[player] = sPeers[player].protocol.send;
                }
                if (recipient < 2)
                {
                    Select(recipient);
                    assert(SoulLink_ProtocolReceive(&sPeers[recipient].protocol,
                                                   &packets[recipient ^ 1], sizeof(packets[0])));
                }
                RestartBoth(); Sync(); CheckResult(scenario);
                RestartBoth(); Sync(); CheckResult(scenario);
                sCuts++;
            }
    }
}

static void TestOfflineDeathsAndCompletedCleanup(void)
{
    struct SoulLinkPair pair;
    unsigned player;
    Setup(); pair = Bound(6); PutBoth(&pair);
    // Neither endpoint is connected while the coordinator loses this pair.
    Select(0); assert(SoulLink_MarkLocalDeath(&pair.mons[0]));
    assert(SoulLink_ReadPair(0, &pair)); pair.cleanupFlags = 0;
    assert(SoulLink_StorePair(&pair)); // Local body already transferred.
    RestartBoth(); Sync(); Mirrors();
    assert(ReadId(0, 6).cleanupFlags == 0);
    assert(ReadId(1, 6).cleanupFlags == SOUL_LINK_CLEANUP_PLAYER_1);
    Select(1); assert(SoulLink_ReadPair(0, &pair)); pair.cleanupFlags = 0;
    assert(SoulLink_StorePair(&pair));
    RestartBoth(); Sync();
    for (player = 0; player < 2; player++)
    {
        pair = ReadId(player, 6);
        assert(pair.state == SOUL_LINK_PAIR_DEAD && pair.cleanupFlags == 0);
    }
}

static void TestCapacityReconnect(void)
{
    unsigned i;
    struct SoulLinkPair pair;
    Setup();
    for (i = 1; i <= SOUL_LINK_PAIR_CAPACITY; i++)
    {
        pair = Bound(i); PutBoth(&pair);
    }
    Select(1); assert(SoulLink_MarkLocalDeath(&pair.mons[1]));
    InitBoth();
    for (i = 0; i < SOUL_LINK_PAIR_CAPACITY + 3; i++) Round();
    RestartBoth(); Sync(); Mirrors();
    assert(ReadId(0, 88).state == SOUL_LINK_PAIR_DEAD);
    assert(ReadId(0, 87).state == SOUL_LINK_PAIR_ALIVE);
    assert(SoulLink_GetPairCount() == SOUL_LINK_PAIR_CAPACITY);
}

int main(void)
{
    CaptureRegressionMain();
    TestEveryBoundary(); TestOfflineDeathsAndCompletedCleanup(); TestCapacityReconnect();
    printf("Soul Link reconciliation: %u asymmetric cuts, save-state reloads, offline deaths, completed cleanup and 88-row reconnect passed\n", sCuts);
    return 0;
}
