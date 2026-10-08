#include <assert.h>
#include <stdio.h>
#include "global.h"
#include "soul_link.h"
#include "soul_link_protocol.h"

struct Endpoint
{
    struct SaveBlock1 block1;
    struct SaveBlock2 block2;
    struct PokemonStorage storage;
    struct SoulLinkProtocol protocol;
};

static struct Endpoint sPeers[2];
bool8 gSoulLinkTestCacheAllocationFailure;
struct SaveBlock1 *gSaveBlock1Ptr;
struct SaveBlock2 *gSaveBlock2Ptr;
struct PokemonStorage *gPokemonStoragePtr;
u16 Random(void) { return 123; }

static void Select(u8 player)
{
    gSaveBlock1Ptr = &sPeers[player].block1;
    gSaveBlock2Ptr = &sPeers[player].block2;
    gPokemonStoragePtr = &sPeers[player].storage;
    assert(SoulLink_LoadSaveData());
}

static void Setup(void)
{
    u8 player;
    const struct SoulLinkRunId run = {{123, 456}};
    const u32 participants[2] = {10, 20};
    gSoulLinkTestCacheAllocationFailure = FALSE;
    memset(sPeers, 0, sizeof(sPeers));
    SoulLink_ProtocolForgetBaseline(0); SoulLink_ProtocolForgetBaseline(1);
    for (player = 0; player < 2; player++)
    {
        struct SoulLinkConfig config;
        Select(player);
        SoulLink_ResetSaveData();
        assert(SoulLink_BeginRun(&run, participants, player));
        assert(SoulLink_GetConfig(&config));
        config.enabled = TRUE;
        assert(SoulLink_SetConfig(&config));
    }
}

static struct SoulLinkPair Bound(u16 id)
{
    struct SoulLinkPair pair = {0};
    pair.pairId = id;
    pair.areaId = id + 7;
    pair.state = SOUL_LINK_PAIR_ALIVE;
    pair.encounterStates[0] = pair.encounterStates[1] = SOUL_LINK_ENCOUNTER_CAPTURED;
    pair.mons[0].otId = 10;
    pair.mons[1].otId = 20;
    pair.mons[0].personality = pair.mons[1].personality = id;
    pair.mons[0].species = pair.mons[1].species = SPECIES_BULBASAUR;
    pair.mons[0].encounterOrdinal = pair.mons[1].encounterOrdinal = id;
    return pair;
}

static void PutBoth(const struct SoulLinkPair *pair)
{
    Select(0); assert(SoulLink_StorePair(pair));
    Select(1); assert(SoulLink_StorePair(pair));
}

static void InitBoth(void)
{
    Select(0); assert(SoulLink_ProtocolInit(&sPeers[0].protocol));
    Select(1); assert(SoulLink_ProtocolInit(&sPeers[1].protocol));
}

static void Round(void)
{
    struct SoulLinkPacket packets[2];
    u8 player;
    for (player = 0; player < 2; player++)
    {
        Select(player);
        assert(SoulLink_ProtocolBuild(&sPeers[player].protocol));
        packets[player] = sPeers[player].protocol.send;
    }
    for (player = 0; player < 2; player++)
    {
        Select(player);
        assert(SoulLink_ProtocolReceive(&sPeers[player].protocol, &packets[player ^ 1], sizeof(packets[0])));
    }
}

static u16 Sync(void)
{
    u16 rounds = 0;
    InitBoth();
    while (sPeers[0].protocol.status == SOUL_LINK_SESSION_SYNCING)
    {
        Round();
        assert(++rounds <= SOUL_LINK_PAIR_CAPACITY * 2 + 5);
    }
    assert(sPeers[0].protocol.status == SOUL_LINK_SESSION_COMPLETE);
    assert(sPeers[1].protocol.status == SOUL_LINK_SESSION_COMPLETE);
    return rounds;
}

static void TestDeathResync(void)
{
    struct SoulLinkPair pair = Bound(1), result;
    Setup(); PutBoth(&pair);
    Select(0);
    assert(SoulLink_MarkLocalDeath(&pair.mons[0]));
    InitBoth(); Round(); Round(); Round(); // Snapshot is staged, not committed.
    Select(1); assert(SoulLink_ReadPair(0, &result));
    assert(result.state == SOUL_LINK_PAIR_ALIVE);
    // Simulate disconnect before READY by discarding both sessions.
    Sync();
    Select(1); assert(SoulLink_ReadPair(0, &result));
    assert(result.state == SOUL_LINK_PAIR_DEAD);
    assert(result.encounterStates[0] == SOUL_LINK_ENCOUNTER_DEAD);
    assert(result.encounterStates[1] == SOUL_LINK_ENCOUNTER_DEAD);
    assert(result.cleanupFlags == SOUL_LINK_CLEANUP_PLAYER_1);
    Select(0); assert(SoulLink_ReadPair(0, &result));
    assert(result.encounterStates[0] == SOUL_LINK_ENCOUNTER_DEAD);
    assert(result.cleanupFlags == SOUL_LINK_CLEANUP_PLAYER_0);
    Sync(); // Repeated snapshots are idempotent.
    Select(1); assert(SoulLink_MarkLocalDeath(&pair.mons[1]));
    Sync();
    Select(0); assert(SoulLink_ReadPair(0, &result));
    assert(result.encounterStates[1] == SOUL_LINK_ENCOUNTER_DEAD);
    assert(result.cleanupFlags == SOUL_LINK_CLEANUP_PLAYER_0);
}

static void TestPendingAndCapacity(void)
{
    struct SoulLinkMonIdentity mon = {10, 99, SPECIES_BULBASAUR, 0};
    struct SoulLinkPair pair;
    u16 i;
    Setup(); Sync(); // Empty snapshots.
    Select(0); assert(SoulLink_RecordLocalEncounter(8, &mon, SOUL_LINK_ENCOUNTER_CAPTURED));
    mon.otId = 20;
    Select(1); assert(SoulLink_RecordLocalEncounter(8, &mon, SOUL_LINK_ENCOUNTER_CAPTURED));
    Sync();
    Select(1); assert(SoulLink_GetPairCount() == 1);
    assert(SoulLink_ReadPair(0, &pair) && pair.pairId == 1);
    assert(pair.state == SOUL_LINK_PAIR_ALIVE);
    Setup();
    for (i = 1; i <= SOUL_LINK_PAIR_CAPACITY; i++)
    {
        pair = Bound(i);
        PutBoth(&pair);
    }
    Sync();
    Select(1); assert(SoulLink_GetPairCount() == SOUL_LINK_PAIR_CAPACITY);
}

static void TestDeltaAndFallback(void)
{
    struct SoulLinkPair pair, result;
    u16 i, rounds;
    Setup();
    for (i = 1; i <= SOUL_LINK_PAIR_CAPACITY; i++)
    {
        pair = Bound(i); PutBoth(&pair);
    }
    Sync();
    // Cleanup belongs to each game and must not make otherwise identical rows dirty.
    rounds = Sync();
    assert(sPeers[0].protocol.delta && sPeers[1].protocol.delta);
    assert(sPeers[0].protocol.stateRows == 0 && sPeers[0].protocol.proposalRows == 0);
    assert(rounds == 5);
    pair = Bound(57); Select(0); assert(SoulLink_MarkLocalDeath(&pair.mons[0]));
    rounds = Sync();
    assert(sPeers[0].protocol.delta && sPeers[0].protocol.stateRows == 1);
    assert(sPeers[0].protocol.proposalRows <= 1 && rounds <= 7);
    Select(1); assert(SoulLink_ReadPair(56, &result));
    assert(result.state == SOUL_LINK_PAIR_DEAD);
    assert(Sync() == 5); // Repeated delivery remains idempotent.
    // One restarted game has no shared baseline. Both use the complete path.
    InitBoth(); sPeers[0].protocol.hasBaseline = FALSE;
    rounds = 0;
    while (sPeers[0].protocol.status == SOUL_LINK_SESSION_SYNCING) { Round(); assert(++rounds < 182); }
    assert(!sPeers[0].protocol.delta && !sPeers[1].protocol.delta);
    assert(sPeers[0].protocol.stateRows == SOUL_LINK_PAIR_CAPACITY);
    Select(1); assert(SoulLink_ReadPair(56, &result));assert(result.state == SOUL_LINK_PAIR_DEAD);
    assert(Sync() == 5);
    // A baseline must agree on both its count and its digest.
    InitBoth(); sPeers[0].protocol.baselineDigest ^= 1;
    Round(); Round();
    assert(!sPeers[0].protocol.delta && !sPeers[1].protocol.delta);
    Sync();
}

static void TestCacheAllocationFailure(void)
{
    struct SoulLinkPair pair = Bound(1), result;
    Setup(); PutBoth(&pair); Select(1); assert(SoulLink_MarkLocalDeath(&pair.mons[1]));
    gSoulLinkTestCacheAllocationFailure = TRUE;
    Sync();
    assert(!sPeers[0].protocol.hasBaseline && !sPeers[1].protocol.hasBaseline);
    Select(0); assert(SoulLink_ReadPair(0, &result)); assert(result.state == SOUL_LINK_PAIR_DEAD);
    gSoulLinkTestCacheAllocationFailure = FALSE;
    Sync(); assert(!sPeers[0].protocol.delta && !sPeers[1].protocol.delta);
    assert(Sync() == 5); // Memory becoming available restores the optional fast path.
}

static void TestDeltaDisconnects(void)
{
    unsigned cut, recipient, i, rounds;
    struct SoulLinkPair pair, result;
    for (cut = 0; cut < 7; cut++)
        for (recipient = 0; recipient < 3; recipient++)
        {
            Setup();
            for (i = 1; i <= SOUL_LINK_PAIR_CAPACITY; i++) { pair = Bound(i); PutBoth(&pair); }
            Sync();
            pair = Bound(57); Select(1); assert(SoulLink_MarkLocalDeath(&pair.mons[1]));
            InitBoth();
            for (rounds = 0; rounds < cut; rounds++) Round();
            struct SoulLinkPacket packets[2];
            for (i = 0; i < 2; i++)
            {
                Select(i); assert(SoulLink_ProtocolBuild(&sPeers[i].protocol)); packets[i] = sPeers[i].protocol.send;
            }
            if (recipient < 2)
            {
                Select(recipient); assert(SoulLink_ProtocolReceive(&sPeers[recipient].protocol, &packets[recipient ^ 1], sizeof(packets[0])));
            }
            // Discard either in-flight exchange, retaining the last completed baseline.
            Sync(); Sync();
            for (i = 0; i < 2; i++)
            {
                Select(i); assert(SoulLink_ReadPair(56, &result)); assert(result.state == SOUL_LINK_PAIR_DEAD);
                assert(SoulLink_GetPairCount() == SOUL_LINK_PAIR_CAPACITY);
            }
        }
}

static void RecheckPacket(struct SoulLinkPacket *packet)
{
    u32 checksum = 2166136261u; unsigned i;
    packet->checksum = 0;
    for (i = 0; i < sizeof(*packet); i++) checksum = (checksum ^ ((u8 *)packet)[i]) * 16777619;
    packet->checksum = checksum;
}
static void TestMalformedDeltaMaps(void)
{
    struct SoulLinkPair pair = Bound(1), result;
    struct SoulLinkPacket packet;
    Setup(); PutBoth(&pair); Sync(); InitBoth(); Round();
    Select(1); assert(SoulLink_ProtocolBuild(&sPeers[1].protocol)); packet = sPeers[1].protocol.send;
    packet.payload.ping.changed[10] = 128; RecheckPacket(&packet);
    Select(0); assert(!SoulLink_ProtocolReceive(&sPeers[0].protocol, &packet, sizeof(packet)));
    assert(SoulLink_ReadPair(0, &result) && result.state == SOUL_LINK_PAIR_ALIVE);
    InitBoth(); Round(); Select(1); assert(SoulLink_ProtocolBuild(&sPeers[1].protocol)); packet = sPeers[1].protocol.send;
    packet.payload.ping.mode = 2; RecheckPacket(&packet);
    Select(0); assert(!SoulLink_ProtocolReceive(&sPeers[0].protocol, &packet, sizeof(packet)));
    Setup();
    struct SoulLinkMonIdentity mon = {20, 99, SPECIES_BULBASAUR, 0};
    Select(1); assert(SoulLink_RecordLocalEncounter(8, &mon, SOUL_LINK_ENCOUNTER_CAPTURED));
    InitBoth();
    for (;;)
    {
        Select(0); assert(SoulLink_ProtocolBuild(&sPeers[0].protocol));
        Select(1); assert(SoulLink_ProtocolBuild(&sPeers[1].protocol));
        if (sPeers[0].protocol.send.type == SOUL_LINK_PACKET_PLAN) break;
        Round();
    }
    packet = sPeers[0].protocol.send;
    assert(packet.payload.ping.count == 1);
    packet.payload.ping.changed[0] = 0; RecheckPacket(&packet);
    Select(1); assert(!SoulLink_ProtocolReceive(&sPeers[1].protocol, &packet, sizeof(packet)));
    assert(SoulLink_ReadPair(0, &result) && result.pairId == 0);
}

static void TestRejections(void)
{
    struct SoulLinkPacket packet;
    struct SoulLinkPair pair = Bound(1), result;
    struct SoulLinkPair workspace[SOUL_LINK_PAIR_CAPACITY];
    u16 count;
    Setup(); PutBoth(&pair); InitBoth();
    Select(1); assert(SoulLink_ProtocolBuild(&sPeers[1].protocol));
    packet = sPeers[1].protocol.send;
    Select(0);
    assert(!SoulLink_ProtocolReceive(&sPeers[0].protocol, &packet, sizeof(packet) - 1));
    assert(sPeers[0].protocol.status == SOUL_LINK_SESSION_INVALID_PACKET);
    InitBoth(); packet.checksum ^= 1;
    Select(0); assert(!SoulLink_ProtocolReceive(&sPeers[0].protocol, &packet, sizeof(packet)));
    InitBoth();
    sPeers[1].protocol.header.runId.words[0]++;
    Select(1); assert(SoulLink_ProtocolBuild(&sPeers[1].protocol)); packet = sPeers[1].protocol.send;
    Select(0); assert(!SoulLink_ProtocolReceive(&sPeers[0].protocol, &packet, sizeof(packet)));
    assert(sPeers[0].protocol.status == SOUL_LINK_SESSION_INCOMPATIBLE);
    InitBoth(); sPeers[1].protocol.header.config.dupesClause = FALSE;
    Select(1); assert(SoulLink_ProtocolBuild(&sPeers[1].protocol)); packet = sPeers[1].protocol.send;
    Select(0); assert(!SoulLink_ProtocolReceive(&sPeers[0].protocol, &packet, sizeof(packet)));
    InitBoth(); sPeers[1].protocol.header.localPlayer = 0;
    Select(1); assert(SoulLink_ProtocolBuild(&sPeers[1].protocol)); packet = sPeers[1].protocol.send;
    Select(0); assert(!SoulLink_ProtocolReceive(&sPeers[0].protocol, &packet, sizeof(packet)));
    InitBoth();
    Select(1); assert(SoulLink_ProtocolBuild(&sPeers[1].protocol)); packet = sPeers[1].protocol.send;
    Round();
    Select(0); assert(!SoulLink_ProtocolReceive(&sPeers[0].protocol, &packet, sizeof(packet))); // Replayed HELLO.
    assert(SoulLink_ReadPair(0, &result) && !memcmp(&result, &pair, sizeof(pair)));
    result = pair; result.areaId++;
    assert(!SoulLink_PrepareRemoteState(&result, 1, 1, workspace, &count));
    result = pair; result.pairId++;
    assert(!SoulLink_PrepareRemoteState(&result, 1, 1, workspace, &count));
    result = pair; result.mons[1].encounterOrdinal++;
    assert(!SoulLink_PrepareRemoteState(&result, 1, 1, workspace, &count));
    assert(!SoulLink_PrepareRemoteState(&result, 89, 1, workspace, &count));
    assert(SoulLink_ReadPair(0, &result) && !memcmp(&result, &pair, sizeof(pair)));
}

static void TestStaleAndOwnership(void)
{
    struct SoulLinkPair pair = Bound(1), result, rows[2];
    struct SoulLinkPair workspace[SOUL_LINK_PAIR_CAPACITY];
    u16 count;
    Setup(); PutBoth(&pair);
    Select(0);
    result = pair;
    result.encounterStates[1] = SOUL_LINK_ENCOUNTER_DEAD;
    result.state = SOUL_LINK_PAIR_DEAD;
    assert(SoulLink_StorePair(&result));
    assert(SoulLink_PrepareRemoteState(&pair, 1, 1, workspace, &count));
    assert(SoulLink_CommitRemoteState(workspace, count));
    assert(SoulLink_ReadPair(0, &result) && result.state == SOUL_LINK_PAIR_DEAD);
    rows[0] = pair; rows[1] = pair;
    assert(!SoulLink_PrepareRemoteState(rows, 2, 1, workspace, &count));
    Setup(); PutBoth(&pair); Select(0);
    result = pair;
    result.encounterStates[0] = SOUL_LINK_ENCOUNTER_DEAD;
    result.state = SOUL_LINK_PAIR_DEAD;
    result.cleanupFlags = 1;
    assert(SoulLink_PrepareRemoteState(&result, 1, 1, workspace, &count));
    assert(workspace[0].state == SOUL_LINK_PAIR_DEAD); // Restore a known tombstone from an older save.
    assert(workspace[0].cleanupFlags == SOUL_LINK_CLEANUP_PLAYER_0);
    workspace[0].mons[0].personality++;
    assert(!SoulLink_CommitRemoteState(workspace, count));
    result = pair;
    result.mons[1].otId = 30; result.mons[1].personality = 100;
    assert(SoulLink_PrepareRemoteState(&result, 1, 1, workspace, &count));
    assert(SoulLink_CommitRemoteState(workspace, count)); // Remote NPC trade.
    InitBoth();
    for (;;)
    {
        Select(0); assert(SoulLink_ProtocolBuild(&sPeers[0].protocol));
        Select(1); assert(SoulLink_ProtocolBuild(&sPeers[1].protocol));
        if (sPeers[0].protocol.send.type == SOUL_LINK_PACKET_READY)
            break;
        Round();
    }
    // Frozen local data changed while the snapshot was in flight: no commit.
    Select(0); assert(SoulLink_ProtocolBuild(&sPeers[0].protocol));
    Select(1); assert(SoulLink_ProtocolBuild(&sPeers[1].protocol));
    Select(0); assert(SoulLink_MarkLocalDeath(&pair.mons[0]));
    assert(!SoulLink_ProtocolReceive(&sPeers[0].protocol, &sPeers[1].protocol.send, sizeof(struct SoulLinkPacket)));
    assert(sPeers[0].protocol.status == SOUL_LINK_SESSION_CONFLICT);
}

static void TestOlderSaveDeathRecovery(void)
{
    struct SoulLinkPair pair = Bound(1), result;
    Setup(); PutBoth(&pair);
    // A remembers B's death; B restored its own older, still-alive save.
    Select(0);
    pair.encounterStates[1] = SOUL_LINK_ENCOUNTER_DEAD;
    pair.state = SOUL_LINK_PAIR_DEAD;
    assert(SoulLink_StorePair(&pair));
    Sync();
    Select(1); assert(SoulLink_ReadPair(0, &result));
    assert(result.encounterStates[1] == SOUL_LINK_ENCOUNTER_DEAD);
    assert(result.encounterStates[0] == SOUL_LINK_ENCOUNTER_DEAD);
    assert(result.cleanupFlags == SOUL_LINK_CLEANUP_PLAYER_1);
    Sync();
    assert(SoulLink_LoadSaveData());
    assert(SoulLink_ReadPair(0, &result) && result.state == SOUL_LINK_PAIR_DEAD);
}


static bool8 sTeamReady[2];
static u8 sTeamCommits[2];
static bool8 PrepareTeam(void *context, const SoulLinkPairId *ids, u8 count)
{
    unsigned player = *(u8 *)context;
    assert(count == 1 && ids[0] == 2); // Requested tombstone #1 is pruned.
    return sTeamReady[player];
}
static void DescribeTeam(void *context, struct SoulLinkTeamDiagnostic *diagnostic)
{
    (void)context;
    memset(diagnostic, 0, sizeof(*diagnostic));
    diagnostic->reason = SOUL_LINK_TEAM_MAIL;
    diagnostic->pairId = 2; diagnostic->species = SPECIES_BULBASAUR;
}
static void CommitTeam(void *context) { sTeamCommits[*(u8 *)context]++; }
static u8 sTeamPlayers[2] = {0, 1};
static void InitTeams(void)
{
    InitBoth();
    for (unsigned p = 0; p < 2; p++)
    {
        sPeers[p].protocol.teamContext = &sTeamPlayers[p];
        sPeers[p].protocol.prepareTeam = PrepareTeam;
        sPeers[p].protocol.commitTeam = CommitTeam;
        sPeers[p].protocol.describeTeam = DescribeTeam;
    }
}
static void TeamPhase(void)
{
    for (;;)
    {
        Select(0); assert(SoulLink_ProtocolBuild(&sPeers[0].protocol));
        Select(1); assert(SoulLink_ProtocolBuild(&sPeers[1].protocol));
        if (sPeers[0].protocol.send.type == SOUL_LINK_PACKET_TEAM_READY) return;
        Round();
    }
}
static void TestTeams(void)
{
    struct SoulLinkPair pair = Bound(1), living = Bound(2), result;
    SoulLinkPairId ids[2] = {1, 2};
    Setup(); PutBoth(&pair); PutBoth(&living);
    for (unsigned p = 0; p < 2; p++)
    {
        struct SoulLinkConfig config;
        Select(p); assert(SoulLink_GetConfig(&config));
        config.enforcePartyPairs = TRUE; assert(SoulLink_SetConfig(&config));
    }
    Select(0); assert(SoulLink_SetTeamSelection(ids, 2));
    assert(SoulLink_MarkLocalDeath(&pair.mons[0]));
    sTeamReady[0] = TRUE; sTeamReady[1] = FALSE;
    memset(sTeamCommits, 0, sizeof(sTeamCommits));
    InitTeams(); TeamPhase();
    for (unsigned p = 0; p < 2; p++)
    {
        Select(p);
        assert(!SoulLink_ProtocolReceive(&sPeers[p].protocol, &sPeers[p ^ 1].protocol.send, sizeof(struct SoulLinkPacket)));
        assert(sPeers[p].protocol.status == SOUL_LINK_SESSION_TEAM_CONFLICT);
        assert(sPeers[p].protocol.teamDiagnostic.reason == SOUL_LINK_TEAM_MAIL);
        assert(sPeers[p].protocol.teamDiagnostic.pairId == 2);
        assert(sPeers[p].protocol.diagnosticPlayer == 1);
        assert(SoulLink_ReadPair(0, &result) && result.state == SOUL_LINK_PAIR_DEAD);
        assert(sTeamCommits[p] == 0);
    }
    for (unsigned malformed = 0; malformed < 4; malformed++)
    {
        InitTeams(); TeamPhase();
        struct SoulLinkPacket packet = sPeers[1].protocol.send;
        if (malformed == 0) packet.payload.team.diagnostic.reason = SOUL_LINK_TEAM_ISSUE_COUNT;
        if (malformed == 1) packet.payload.team.diagnostic.species = NUM_SPECIES;
        if (malformed == 2) packet.payload.team.diagnostic.neededSlots = 7;
        if (malformed == 3) packet.payload.team.diagnostic.reserved = 1;
        packet.checksum = 0; u32 checksum = 2166136261u;
        for (unsigned i = 0; i < sizeof(packet); i++) checksum = (checksum ^ ((u8 *)&packet)[i]) * 16777619;
        packet.checksum = checksum;
        Select(0); assert(!SoulLink_ProtocolReceive(&sPeers[0].protocol, &packet, sizeof(packet)));
        assert(sPeers[0].protocol.status == SOUL_LINK_SESSION_INVALID_PACKET);
        assert(sTeamCommits[0] == 0);
    }
    sTeamReady[1] = TRUE;
    InitTeams(); TeamPhase();
    // B received A's final readiness, but A lost B's block. Reconnect is safe.
    Select(1); assert(SoulLink_ProtocolReceive(&sPeers[1].protocol, &sPeers[0].protocol.send, sizeof(struct SoulLinkPacket)));
    assert(sTeamCommits[0] == 0 && sTeamCommits[1] == 1);
    InitTeams(); TeamPhase(); Round();
    assert(sTeamCommits[0] == 1 && sTeamCommits[1] == 2);
    assert(sPeers[0].protocol.status == SOUL_LINK_SESSION_COMPLETE);
    assert(sPeers[1].protocol.status == SOUL_LINK_SESSION_COMPLETE);
}

int main(void)
{
    // Identical rules can have different editing histories on the two consoles.
    Setup();
    sPeers[1].block2.soulLink.header.configRevision += 2;
    Sync();
    TestTeams();
    TestDeathResync();
    TestPendingAndCapacity();
    TestDeltaAndFallback();
    TestDeltaDisconnects();
    TestCacheAllocationFailure();
    TestMalformedDeltaMaps();
    TestRejections();
    TestStaleAndOwnership();
    TestOlderSaveDeathRecovery();
    puts("Soul Link protocol: handshake, full snapshots, terminal merge and rejection tests passed");
    return 0;
}
