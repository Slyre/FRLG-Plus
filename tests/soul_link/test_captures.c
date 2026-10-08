#define main ProtocolTestMain
#include "test_protocol.c"
#undef main

static void Capture(u8 player, u16 area, u32 personality)
{
    struct SoulLinkMonIdentity mon = {10 + player * 10, personality, SPECIES_BULBASAUR, 0};
    Select(player);
    assert(SoulLink_CanRecordLocalEncounter(area));
    assert(SoulLink_RecordLocalEncounter(area, &mon, SOUL_LINK_ENCOUNTER_CAPTURED));
}

static struct SoulLinkPair ReadId(u8 player, u16 id)
{
    struct SoulLinkPair pair;
    u16 i;
    Select(player);
    for (i = 0; i < SoulLink_GetPairCount(); i++)
        if (SoulLink_ReadPair(i, &pair) && pair.pairId == id)
            return pair;
    assert(!"Missing pair ID");
    return pair;
}

static void Mirrors(void)
{
    struct SoulLinkPair a, b;
    u16 i, count;
    Select(0); count = SoulLink_GetPairCount();
    Select(1); assert(SoulLink_GetPairCount() == count);
    for (i = 0; i < count; i++)
    {
        Select(0); assert(SoulLink_ReadPair(i, &a));
        Select(1); assert(SoulLink_ReadPair(i, &b));
        a.cleanupFlags = b.cleanupFlags = 0;
        assert(!memcmp(&a, &b, sizeof(a)));
        assert(a.pairId != 0);
    }
}

static void TestAreasAndOrder(void)
{
    struct SoulLinkPair pair;
    Setup(); Capture(0, 8, 101); Capture(1, 8, 201); Sync(); Mirrors();
    pair = ReadId(0, 1);
    assert(pair.state == SOUL_LINK_PAIR_ALIVE && pair.mons[1].personality == 201);
    Sync(); Mirrors(); Select(0); assert(SoulLink_GetPairCount() == 1);

    Setup(); Capture(0, 9, 101); Capture(1, 10, 201); Sync(); Mirrors();
    Select(0); assert(SoulLink_GetPairCount() == 2);
    pair = ReadId(0, 1); assert(pair.state == SOUL_LINK_PAIR_PENDING && pair.encounterStates[1] == 0);
    pair = ReadId(0, 2); assert(pair.state == SOUL_LINK_PAIR_PENDING && pair.encounterStates[0] == 0);
    Capture(0, 10, 102); Capture(1, 9, 202); Sync(); Mirrors();
    assert(ReadId(0, 1).state == SOUL_LINK_PAIR_ALIVE);
    assert(ReadId(0, 2).state == SOUL_LINK_PAIR_ALIVE);
    Select(0); assert(SoulLink_GetPairCount() == 2 && gSaveBlock2Ptr->soulLink.header.nextPairId == 3);

    Setup(); Capture(0, 8, 101); Capture(0, 9, 102); Capture(1, 9, 201); Capture(1, 8, 202);
    Sync(); Mirrors();
    assert(ReadId(0, 1).mons[1].personality == 202);
    assert(ReadId(0, 2).mons[1].personality == 201);
}

static void TestPartnerFirstAndFifo(void)
{
    struct SoulLinkPair pair, swap;
    Setup(); Capture(1, 8, 201); Sync(); Mirrors();
    pair = ReadId(0, 1); assert(pair.encounterStates[0] == 0);
    Capture(0, 8, 101); Sync(); Mirrors();
    assert(ReadId(1, 1).state == SOUL_LINK_PAIR_ALIVE);
    Select(0); assert(SoulLink_GetPairCount() == 1);

    Setup(); Capture(0, 8, 101); Sync(); Capture(1, 8, 201);
    Select(1); assert(SoulLink_GetPairCount() == 1);
    pair = ReadId(1, 1); assert(pair.state == SOUL_LINK_PAIR_ALIVE);
    Sync(); Mirrors();

    Setup(); Capture(0, 8, 101); Capture(0, 8, 102); Capture(1, 8, 201); Capture(1, 8, 202);
    swap = sPeers[1].block2.soulLink.pairs[0];
    sPeers[1].block2.soulLink.pairs[0] = sPeers[1].block2.soulLink.pairs[1];
    sPeers[1].block2.soulLink.pairs[1] = swap;
    Sync(); Mirrors();
    assert(ReadId(0, 1).mons[1].personality == 201);
    assert(ReadId(0, 2).mons[1].personality == 202);
}

static void TestFailuresAndDeaths(void)
{
    struct SoulLinkPair pair;
    Setup(); Select(0); assert(SoulLink_RecordLocalEncounter(8, NULL, SOUL_LINK_ENCOUNTER_FAILED));
    Capture(1, 8, 201); Sync(); Mirrors(); pair = ReadId(1, 1);
    assert(pair.state == SOUL_LINK_PAIR_FAILED && pair.mons[1].personality == 201);
    assert(pair.encounterStates[1] == SOUL_LINK_ENCOUNTER_CAPTURED && pair.cleanupFlags == 0);
    Setup(); Capture(0, 8, 101); Select(1);
    assert(SoulLink_RecordLocalEncounter(8, NULL, SOUL_LINK_ENCOUNTER_FAILED)); Sync(); Mirrors();
    assert(ReadId(0, 1).state == SOUL_LINK_PAIR_FAILED);
    Setup(); Capture(0, 8, 101); pair = ReadId(0, 1);
    Select(0); assert(SoulLink_MarkLocalDeath(&pair.mons[0]));
    Capture(1, 8, 201); Sync(); Mirrors(); pair = ReadId(1, 1);
    assert(pair.state == SOUL_LINK_PAIR_DEAD && pair.encounterStates[1] == SOUL_LINK_ENCOUNTER_DEAD);
    Setup(); Capture(1, 8, 201); Select(1); assert(SoulLink_ReadPair(0, &pair));
    assert(SoulLink_MarkLocalDeath(&pair.mons[1])); Capture(0, 8, 101); Sync(); Mirrors();
    pair = ReadId(1, 1);
    assert(pair.state == SOUL_LINK_PAIR_DEAD && pair.cleanupFlags == SOUL_LINK_CLEANUP_PLAYER_1);
}

static void TestOptionalAreaAndStarter(void)
{
    u8 player;
    struct SoulLinkConfig config;
    Setup();
    for (player = 0; player < 2; player++)
    {
        Select(player); assert(SoulLink_GetConfig(&config)); config.sameAreaPairing = FALSE;
        assert(SoulLink_SetConfig(&config));
    }
    Capture(0, 9, 101); Capture(1, 10, 201); Sync(); Mirrors();
    assert(ReadId(0, 1).state == SOUL_LINK_PAIR_ALIVE && ReadId(1, 1).areaId == 9);
    Setup();
    for (player = 0; player < 2; player++)
    {
        Select(player); assert(SoulLink_GetConfig(&config)); config.sameAreaPairing = FALSE;
        assert(SoulLink_SetConfig(&config));
    }
    Capture(0, SOUL_LINK_AREA_STARTER, 101); Capture(1, 8, 201); Sync(); Mirrors();
    assert(ReadId(0, 1).state == SOUL_LINK_PAIR_PENDING);
    assert(ReadId(0, 2).state == SOUL_LINK_PAIR_PENDING);
}

static void TestCapacityAndIdExhaustion(void)
{
    struct SoulLinkPair pair;
    u16 i;
    Setup();
    for (i = 1; i <= SOUL_LINK_PAIR_CAPACITY; i++) Capture(0, i + 7, i + 100);
    Capture(1, 8, 201); Sync(); Mirrors();
    Select(1); assert(SoulLink_GetPairCount() == SOUL_LINK_PAIR_CAPACITY);
    assert(SoulLink_CanRecordLocalEncounter(9));
    assert(!SoulLink_CanRecordLocalEncounter(1000));
    Capture(1, 9, 202); Sync(); Mirrors();
    assert(ReadId(0, 2).state == SOUL_LINK_PAIR_ALIVE);

    Setup(); Capture(1, 8, 201); Sync();
    Select(0); gSaveBlock2Ptr->soulLink.header.nextPairId = 0;
    assert(SoulLink_CanRecordLocalEncounter(8));
    assert(!SoulLink_CanRecordLocalEncounter(9));
    Capture(0, 8, 101); Sync(); Mirrors();
    assert(ReadId(0, 1).state == SOUL_LINK_PAIR_ALIVE);

    Setup();
    for (i = 1; i <= SOUL_LINK_PAIR_CAPACITY; i++) Capture(0, i + 7, i + 100);
    Capture(1, 1000, 201); InitBoth();
    for (i = 0; i < SOUL_LINK_PAIR_CAPACITY + 2; i++) Round();
    Select(0); assert(!SoulLink_ProtocolBuild(&sPeers[0].protocol));
    assert(sPeers[0].protocol.status == SOUL_LINK_SESSION_CONFLICT);
    assert(SoulLink_GetPairCount() == SOUL_LINK_PAIR_CAPACITY);
    Select(1); assert(SoulLink_GetPairCount() == 1);
    assert(SoulLink_ReadPair(0, &pair) && pair.pairId == 0 && pair.mons[1].personality == 201);
}

static void TestInterruptedPromotion(void)
{
    struct SoulLinkPacket packet;
    struct SoulLinkPair pair;
    Setup(); Capture(0, 8, 101); Capture(1, 8, 201); InitBoth();
    for (unsigned i = 0; i < 5; i++) Round(); // Proposal received, no READY yet.
    Select(1); assert(SoulLink_ReadPair(0, &pair) && pair.pairId == 0);
    Sync(); Mirrors(); assert(ReadId(1, 1).state == SOUL_LINK_PAIR_ALIVE);

    Setup(); Capture(0, 8, 101); Capture(1, 8, 201); InitBoth();
    for (unsigned i = 0; i < 5; i++) Round();
    Select(0); assert(SoulLink_ProtocolBuild(&sPeers[0].protocol)); packet = sPeers[0].protocol.send;
    Select(1); assert(SoulLink_ProtocolBuild(&sPeers[1].protocol));
    Select(0); assert(SoulLink_ProtocolReceive(&sPeers[0].protocol, &sPeers[1].protocol.send, sizeof(packet)));
    Select(1); assert(SoulLink_ReadPair(0, &pair) && pair.pairId == 0);
    Sync(); Mirrors();
    Select(0); assert(SoulLink_GetPairCount() == 1 && gSaveBlock2Ptr->soulLink.header.nextPairId == 2);

    // The follower alone committed an ID for its capture; the coordinator restores it.
    Setup(); Capture(1, 8, 201); InitBoth();
    for (unsigned i = 0; i < 5; i++) Round();
    Select(0); assert(SoulLink_ProtocolBuild(&sPeers[0].protocol)); packet = sPeers[0].protocol.send;
    Select(1); assert(SoulLink_ProtocolBuild(&sPeers[1].protocol));
    assert(SoulLink_ProtocolReceive(&sPeers[1].protocol, &packet, sizeof(packet)));
    Select(0); assert(SoulLink_GetPairCount() == 0);
    Sync(); Mirrors();
    assert(ReadId(0, 1).mons[1].personality == 201);
}

static void TestConflictingProposal(void)
{
    struct SoulLinkPair pair = Bound(1), proposed, workspace[SOUL_LINK_PAIR_CAPACITY];
    u16 count;
    Setup(); PutBoth(&pair); Select(1);
    proposed = pair; proposed.mons[0].encounterOrdinal++;
    assert(!SoulLink_PrepareRemoteState(&proposed, 1, 0, workspace, &count));
    proposed = pair; proposed.encounterStates[0] = SOUL_LINK_ENCOUNTER_NONE;
    memset(&proposed.mons[0], 0, sizeof(proposed.mons[0])); proposed.state = SOUL_LINK_PAIR_PENDING;
    assert(!SoulLink_PrepareRemoteState(&proposed, 1, 0, workspace, &count));
    assert(SoulLink_ReadPair(0, &proposed) && !memcmp(&pair, &proposed, sizeof(pair)));
}

#ifdef RECONCILE_HELPERS
static int CaptureRegressionMain(void)
#else
int main(void)
#endif
{
    TestAreasAndOrder(); TestPartnerFirstAndFifo(); TestFailuresAndDeaths();
    TestOptionalAreaAndStarter(); TestCapacityAndIdExhaustion(); TestInterruptedPromotion();
    TestConflictingProposal();
    puts("Soul Link captures: area pairing, FIFO, failures, capacity, exhausted IDs and interrupted promotion passed");
    return 0;
}
