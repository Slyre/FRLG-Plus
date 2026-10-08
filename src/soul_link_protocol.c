#include "global.h"
#include "soul_link.h"
#include "malloc.h"
#include "soul_link_protocol.h"

struct SoulLinkBaseline
{
    struct SoulLinkRunId runId;
    u32 participantIds[2];
    struct SoulLinkConfig config;
    struct SoulLinkPair rows[SOUL_LINK_PAIR_CAPACITY];
    u32 digest;
    u16 count;
    bool8 valid;
};
static EWRAM_DATA struct SoulLinkBaseline *sBaselines[2] = {NULL, NULL};
static u32 CanonicalDigest(const struct SoulLinkPair *rows, u16 count);
void SoulLink_ProtocolForgetBaseline(u8 player)
{
    if (player < 2 && sBaselines[player] != NULL)
    {
        Free(sBaselines[player]); sBaselines[player] = NULL;
    }
}
void SoulLink_ProtocolResetHeapCache(void)
{
    sBaselines[0] = sBaselines[1] = NULL;
}
static bool8 CanonicalEqual(const struct SoulLinkPair *a, const struct SoulLinkPair *b)
{
    struct SoulLinkPair left = *a, right = *b;
    left.cleanupFlags = right.cleanupFlags = 0;
    return memcmp(&left, &right, sizeof(left)) == 0;
}
static void SetChanged(u8 *map, u16 index) { map[index / 8] |= 1 << (index % 8); }
static bool8 IsChanged(const u8 *map, u16 index) { return (map[index / 8] >> (index % 8)) & 1; }
static u16 ChangedCount(const u8 *map)
{
    u16 i, count = 0;
    for (i = 0; i < SOUL_LINK_PAIR_CAPACITY; i++) if (IsChanged(map, i)) count++;
    return count;
}
static u16 ChangedIndex(const u8 *map, u16 ordinal)
{
    u16 i;
    for (i = 0; i < SOUL_LINK_PAIR_CAPACITY; i++)
        if (IsChanged(map, i) && ordinal-- == 0) return i;
    return SOUL_LINK_PAIR_CAPACITY;
}
static bool8 ValidMap(const u8 *map, u16 count)
{
    u16 i;
    for (i = count; i < SOUL_LINK_PAIR_CAPACITY; i++) if (IsChanged(map, i)) return FALSE;
    return TRUE;
}
static bool8 CoversNewRows(const u8 *map, u16 baselineCount, u16 count)
{
    u16 i;
    for (i = baselineCount; i < count; i++) if (!IsChanged(map, i)) return FALSE;
    return TRUE;
}
static void RememberBaseline(const struct SoulLinkProtocol *session)
{
    u16 i;
    struct SoulLinkBaseline *base = sBaselines[session->header.localPlayer];
    if (base == NULL)
    {
        base = Alloc(sizeof(*base));
        if (base == NULL) return;
        memset(base, 0, sizeof(*base));
        sBaselines[session->header.localPlayer] = base;
    }
    base->runId = session->header.runId;
    memcpy(base->participantIds, session->header.participantIds, sizeof(base->participantIds));
    base->config = session->header.config;
    base->count = session->mergedCount;
    for (i = 0; i < base->count; i++)
    {
        base->rows[i] = session->merged[i];
        base->rows[i].cleanupFlags = 0;
    }
    base->digest = CanonicalDigest(base->rows, base->count);
    base->valid = TRUE;
}

STATIC_ASSERT(sizeof(struct SoulLinkPacket) == 64, SoulLinkPacketSize);

static u32 Hash(const void *data, u32 size, u32 hash)
{
    const u8 *bytes = data;
    u32 i;
    for (i = 0; i < size; i++)
        hash = (hash ^ bytes[i]) * 16777619;
    return hash;
}

u32 SoulLink_StateDigest(const struct SoulLinkPair *pairs, u16 count)
{
    return Hash(pairs, count * sizeof(*pairs), 2166136261u);
}

static u32 PacketChecksum(const struct SoulLinkPacket *packet)
{
    struct SoulLinkPacket copy = *packet;
    copy.checksum = 0;
    return Hash(&copy, sizeof(copy), 2166136261u);
}

static bool8 Fail(struct SoulLinkProtocol *session, u8 status)
{
    session->status = status;
    if (session->header.localPlayer < 2 && (status == SOUL_LINK_SESSION_INVALID_PACKET || status == SOUL_LINK_SESSION_CONFLICT))
        SoulLink_ProtocolForgetBaseline(session->header.localPlayer);
    return FALSE;
}

bool8 SoulLink_ProtocolInit(struct SoulLinkProtocol *session)
{
    u16 i;
    struct SoulLinkBaseline *base;
    if (session == NULL || !SoulLink_IsEnabled())
        return FALSE;
    memset(session, 0, sizeof(*session));
    session->header = gSaveBlock2Ptr->soulLink.header;
    for (i = 0; i < session->header.pairCount; i++)
        if (!SoulLink_ReadPair(i, &session->outgoing[i]))
            return FALSE;
    base = sBaselines[session->header.localPlayer];
    if (base != NULL && base->valid && !memcmp(&base->runId, &session->header.runId, sizeof(base->runId))
     && !memcmp(base->participantIds, session->header.participantIds, sizeof(base->participantIds))
     && !memcmp(&base->config, &session->header.config, sizeof(base->config)))
    {
        session->hasBaseline = TRUE;
        session->baselineCount = base->count;
        session->baselineDigest = base->digest;
        memcpy(session->incoming, base->rows, base->count * sizeof(base->rows[0]));
        for (i = 0; i < session->header.pairCount || i < base->count; i++)
            if (i >= session->header.pairCount || i >= base->count
             || !CanonicalEqual(&session->outgoing[i], &base->rows[i])) SetChanged(session->changed, i);
    }
    session->status = SOUL_LINK_SESSION_SYNCING;
    return TRUE;
}

static u16 RowCount(const struct SoulLinkProtocol *session)
{
    return session->header.pairCount > session->remoteCount ? session->header.pairCount : session->remoteCount;
}

u16 SoulLink_ProtocolBatchEnd(const struct SoulLinkProtocol *session, u8 type)
{
    return type == SOUL_LINK_PACKET_STATE ? session->stateRows + 2
        : session->stateRows + 3 + session->proposalRows;
}

static u32 CanonicalDigest(const struct SoulLinkPair *rows, u16 count)
{
    u16 i;
    u32 hash = 2166136261u;
    for (i = 0; i < count; i++)
    {
        struct SoulLinkPair pair = rows[i];
        pair.cleanupFlags = 0;
        hash = Hash(&pair, sizeof(pair), hash);
    }
    return hash;
}

static bool8 SaveUnchanged(const struct SoulLinkProtocol *session)
{
    u16 i;
    struct SoulLinkPair current;
    if (memcmp(&session->header, &gSaveBlock2Ptr->soulLink.header, sizeof(session->header)))
        return FALSE;
    for (i = 0; i < session->header.pairCount; i++)
        if (!SoulLink_ReadPair(i, &current) || memcmp(&current, &session->outgoing[i], sizeof(current)))
            return FALSE;
    return TRUE;
}

bool8 SoulLink_ProtocolBuild(struct SoulLinkProtocol *session)
{
    struct SoulLinkPacket *packet;
    u16 sequence, rows;
    if (session == NULL || session->status != SOUL_LINK_SESSION_SYNCING)
        return FALSE;
    packet = &session->send;
    sequence = session->sequence;
    rows = session->stateRows;
    memset(packet, 0, sizeof(*packet));
    packet->magic = SOUL_LINK_PACKET_MAGIC;
    packet->runId = session->header.runId;
    packet->version = SOUL_LINK_PROTOCOL_VERSION;
    packet->areaVersion = SOUL_LINK_AREA_VERSION;
    packet->sender = session->header.localPlayer;
    packet->sequence = sequence;
    if (sequence == 0)
    {
        packet->type = SOUL_LINK_PACKET_HELLO;
        packet->payloadSize = sizeof(packet->payload.hello);
        memcpy(packet->payload.hello.participantIds, session->header.participantIds, sizeof(session->header.participantIds));
        packet->payload.hello.config = session->header.config;
        packet->payload.hello.configRevision = session->header.configRevision;
        packet->payload.hello.count = session->header.pairCount;
        if (session->header.config.enforcePartyPairs)
            memcpy(packet->payload.hello.team, session->header.team.pairIds, sizeof(packet->payload.hello.team));
    }
    else if (sequence == 1)
    {
        packet->type = SOUL_LINK_PACKET_PING;
        packet->payloadSize = sizeof(packet->payload.ping);
        packet->payload.ping.digest = CanonicalDigest(session->outgoing, session->header.pairCount);
        packet->payload.ping.count = session->header.pairCount;
        if (session->hasBaseline)
        {
            packet->payload.ping.mode = 1;
            packet->payload.ping.baselineDigest = session->baselineDigest;
            packet->payload.ping.baselineCount = session->baselineCount;
            memcpy(packet->payload.ping.changed, session->changed, sizeof(session->changed));
        }
    }
    else if (sequence < rows + 2)
    {
        u16 index = session->delta ? ChangedIndex(session->changed, sequence - 2) : sequence - 2;
        packet->type = SOUL_LINK_PACKET_STATE;
        packet->payloadSize = sizeof(packet->payload.state);
        packet->payload.state.index = index;
        packet->payload.state.count = session->header.pairCount;
        if (index < session->header.pairCount)
            packet->payload.state.pair = session->outgoing[index];
    }
    else if (sequence == rows + 2)
    {
        if (CanonicalDigest(session->incoming, session->remoteCount) != session->remoteDigest)
            return Fail(session, SOUL_LINK_SESSION_INVALID_PACKET);
        if (!SaveUnchanged(session))
            return Fail(session, SOUL_LINK_SESSION_CONFLICT);
        packet->type = SOUL_LINK_PACKET_PLAN;
        packet->payloadSize = sizeof(packet->payload.ping);
        if (session->header.localPlayer == 0)
        {
            if (!SoulLink_PrepareRemoteState(session->incoming, session->remoteCount, 1,
                                             session->merged, &session->mergedCount))
                return Fail(session, SOUL_LINK_SESSION_CONFLICT);
            session->proposalCount = session->mergedCount;
            session->proposalDigest = CanonicalDigest(session->merged, session->mergedCount);
            {
                u16 i;
                for (i = 0; i < session->mergedCount; i++)
                    if (i >= session->header.pairCount || !CanonicalEqual(&session->merged[i], &session->outgoing[i]))
                        SetChanged(session->proposalChanged, i);
                session->proposalRows = ChangedCount(session->proposalChanged);
            }
            packet->payload.ping.mode = 1;
            memcpy(packet->payload.ping.changed, session->proposalChanged, sizeof(session->proposalChanged));
            packet->payload.ping.count = session->proposalCount;
            packet->payload.ping.digest = session->proposalDigest;
        }
    }
    else if (sequence < rows + 3 + session->proposalRows)
    {
        u16 index = ChangedIndex(session->proposalChanged, sequence - rows - 3);
        packet->type = SOUL_LINK_PACKET_PROPOSAL;
        packet->payloadSize = sizeof(packet->payload.state);
        packet->payload.state.index = index;
        if (session->header.localPlayer == 0)
        {
            packet->payload.state.count = session->proposalCount;
            packet->payload.state.pair = session->merged[index];
        }
    }
    else if (sequence == rows + 3 + session->proposalRows)
    {
        if (!SaveUnchanged(session))
            return Fail(session, SOUL_LINK_SESSION_CONFLICT);
        if (session->header.localPlayer == 1)
        {
            if (CanonicalDigest(session->incoming, session->proposalCount) != session->proposalDigest)
                return Fail(session, SOUL_LINK_SESSION_INVALID_PACKET);
            if (!SoulLink_PrepareRemoteState(session->incoming, session->proposalCount, 0,
                                             session->merged, &session->mergedCount))
                return Fail(session, SOUL_LINK_SESSION_CONFLICT);
        }
        packet->type = SOUL_LINK_PACKET_READY;
        packet->payloadSize = sizeof(packet->payload.ping);
        packet->payload.ping.count = session->mergedCount;
        packet->payload.ping.digest = CanonicalDigest(session->merged, session->mergedCount);
    }
    else if (sequence == rows + 4 + session->proposalRows)
    {
        packet->type = SOUL_LINK_PACKET_ACK;
        packet->payloadSize = sizeof(packet->payload.ping);
        packet->payload.ping.count = session->mergedCount;
        packet->payload.ping.digest = CanonicalDigest(session->merged, session->mergedCount);
    }
    else if (sequence == rows + 5 + session->proposalRows && session->header.config.enforcePartyPairs)
    {
        const SoulLinkPairId *ids = session->header.localPlayer == 0 ? session->header.team.pairIds : session->remoteTeam;
        u8 i;
        u16 j;
        packet->type = SOUL_LINK_PACKET_TEAM_READY;
        packet->payloadSize = sizeof(packet->payload.team);
        for (i = 0; i < 6; i++)
            for (j = 0; ids[i] && j < session->mergedCount; j++)
                if (session->merged[j].pairId == ids[i] && session->merged[j].state == SOUL_LINK_PAIR_ALIVE)
                {
                    packet->payload.team.ids[packet->payload.team.count++] = ids[i];
                    break;
                }
        packet->payload.team.ready = session->prepareTeam != NULL && session->commitTeam != NULL
            && session->prepareTeam(session->teamContext, packet->payload.team.ids, packet->payload.team.count);
        if (!packet->payload.team.ready)
        {
            if (session->describeTeam != NULL) session->describeTeam(session->teamContext, &packet->payload.team.diagnostic);
            if (packet->payload.team.diagnostic.reason == SOUL_LINK_TEAM_OK)
                packet->payload.team.diagnostic.reason = SOUL_LINK_TEAM_UNKNOWN;
        }
    }
    else
        return Fail(session, SOUL_LINK_SESSION_INVALID_PACKET);
    packet->checksum = PacketChecksum(packet);
    return TRUE;
}

bool8 SoulLink_ProtocolReceive(struct SoulLinkProtocol *session, const void *data, u16 size)
{
    struct SoulLinkPacket packet;
    struct SoulLinkPacket empty;
    u16 sequence, rows;
    if (session == NULL || session->status != SOUL_LINK_SESSION_SYNCING)
        return FALSE;
    if (data == NULL || size != sizeof(packet))
        return Fail(session, SOUL_LINK_SESSION_INVALID_PACKET);
    memcpy(&packet, data, sizeof(packet));
    if (packet.magic != SOUL_LINK_PACKET_MAGIC || packet.reserved != 0
     || packet.sequence != session->sequence || packet.checksum != PacketChecksum(&packet))
        return Fail(session, SOUL_LINK_SESSION_INVALID_PACKET);
    if (packet.version != SOUL_LINK_PROTOCOL_VERSION || packet.areaVersion != SOUL_LINK_AREA_VERSION
     || packet.sender != (session->header.localPlayer ^ 1)
     || memcmp(&packet.runId, &session->header.runId, sizeof(packet.runId)))
        return Fail(session, SOUL_LINK_SESSION_INCOMPATIBLE);
    sequence = session->sequence;
    rows = session->stateRows;
    // Require zero unused payload bytes as well as the exact type and size.
    memset(&empty, 0, sizeof(empty));
    if (sequence == 0)
    {
        if (packet.type != SOUL_LINK_PACKET_HELLO || packet.payloadSize != sizeof(packet.payload.hello)
         || packet.payload.hello.reserved != 0 || packet.payload.hello.count > SOUL_LINK_PAIR_CAPACITY)
            return Fail(session, SOUL_LINK_SESSION_INVALID_PACKET);
        if (memcmp(packet.payload.hello.participantIds, session->header.participantIds, sizeof(session->header.participantIds))
         || memcmp(&packet.payload.hello.config, &session->header.config, sizeof(session->header.config)))
            return Fail(session, SOUL_LINK_SESSION_INCOMPATIBLE);
        session->remoteCount = packet.payload.hello.count;
        memcpy(session->remoteTeam, packet.payload.hello.team, sizeof(session->remoteTeam));
        {
            u8 i;
            bool8 ended = FALSE;
            for (i = 0; i < 6; i++)
            {
                if (packet.payload.hello.team[i] == 0)
                    ended = TRUE;
                else if (!session->header.config.enforcePartyPairs || ended
                      || (i && packet.payload.hello.team[i] <= packet.payload.hello.team[i - 1]))
                    return Fail(session, SOUL_LINK_SESSION_INVALID_PACKET);
            }
        }
        empty.payload.hello = packet.payload.hello;
    }
    else if (sequence == 1)
    {
        if (packet.type != SOUL_LINK_PACKET_PING || packet.payloadSize != sizeof(packet.payload.ping)
         || packet.payload.ping.reserved != 0 || packet.payload.ping.count != session->remoteCount
         || packet.payload.ping.reserved2 || packet.payload.ping.reserved3 || packet.payload.ping.mode > 1
         || packet.payload.ping.baselineCount > SOUL_LINK_PAIR_CAPACITY
         || (packet.payload.ping.mode && !CoversNewRows(packet.payload.ping.changed, packet.payload.ping.baselineCount, session->remoteCount))
         || !ValidMap(packet.payload.ping.changed, packet.payload.ping.baselineCount > session->remoteCount
            ? packet.payload.ping.baselineCount : session->remoteCount)
         || (!packet.payload.ping.mode && (packet.payload.ping.baselineCount || packet.payload.ping.baselineDigest
            || ChangedCount(packet.payload.ping.changed))))
            return Fail(session, SOUL_LINK_SESSION_INVALID_PACKET);
        session->remoteDigest = packet.payload.ping.digest;
        session->delta = session->hasBaseline && packet.payload.ping.mode
            && session->baselineCount == packet.payload.ping.baselineCount
            && session->baselineDigest == packet.payload.ping.baselineDigest;
        if (session->delta)
        {
            u16 i;
            for (i = 0; i < sizeof(session->changed); i++) session->changed[i] |= packet.payload.ping.changed[i];
            // Removed rows beyond both current counts need no wire slot.
            for (i = RowCount(session); i < SOUL_LINK_PAIR_CAPACITY; i++) session->changed[i / 8] &= ~(1 << (i % 8));
            session->stateRows = ChangedCount(session->changed);
        }
        else
        {
            memset(session->incoming, 0, sizeof(session->incoming));
            session->stateRows = RowCount(session);
        }
        empty.payload.ping = packet.payload.ping;
    }
    else if (sequence < rows + 2)
    {
        u16 index = session->delta ? ChangedIndex(session->changed, sequence - 2) : sequence - 2;
        if (packet.type != SOUL_LINK_PACKET_STATE || packet.payloadSize != sizeof(packet.payload.state)
         || packet.payload.state.index != index || packet.payload.state.count != session->remoteCount)
            return Fail(session, SOUL_LINK_SESSION_INVALID_PACKET);
        if (index < session->remoteCount)
            session->incoming[index] = packet.payload.state.pair;
        else if (memcmp(&packet.payload.state.pair, &empty.payload.state.pair, sizeof(struct SoulLinkPair)))
            return Fail(session, SOUL_LINK_SESSION_INVALID_PACKET);
        empty.payload.state = packet.payload.state;
    }
    else if (sequence == rows + 2)
    {
        if (packet.type != SOUL_LINK_PACKET_PLAN || packet.payloadSize != sizeof(packet.payload.ping)
         || packet.payload.ping.reserved != 0 || packet.payload.ping.count > SOUL_LINK_PAIR_CAPACITY
         || packet.payload.ping.reserved2 || packet.payload.ping.reserved3
         || packet.payload.ping.baselineCount || packet.payload.ping.baselineDigest
         || packet.payload.ping.mode != (session->header.localPlayer == 1 ? 1 : 0)
         || !ValidMap(packet.payload.ping.changed, packet.payload.ping.count)
         || (session->header.localPlayer == 1 && !CoversNewRows(packet.payload.ping.changed, session->remoteCount, packet.payload.ping.count)))
            return Fail(session, SOUL_LINK_SESSION_INVALID_PACKET);
        if (session->header.localPlayer == 1)
        {
            session->proposalCount = packet.payload.ping.count;
            session->proposalDigest = packet.payload.ping.digest;
            memcpy(session->proposalChanged, packet.payload.ping.changed, sizeof(session->proposalChanged));
            session->proposalRows = ChangedCount(session->proposalChanged);
        }
        else if (packet.payload.ping.count != 0 || packet.payload.ping.digest != 0)
            return Fail(session, SOUL_LINK_SESSION_INVALID_PACKET);
        empty.payload.ping = packet.payload.ping;
    }
    else if (sequence < rows + 3 + session->proposalRows)
    {
        u16 index = ChangedIndex(session->proposalChanged, sequence - rows - 3);
        if (packet.type != SOUL_LINK_PACKET_PROPOSAL || packet.payloadSize != sizeof(packet.payload.state)
         || packet.payload.state.index != index)
            return Fail(session, SOUL_LINK_SESSION_INVALID_PACKET);
        if (session->header.localPlayer == 1)
        {
            if (packet.payload.state.count != session->proposalCount)
                return Fail(session, SOUL_LINK_SESSION_INVALID_PACKET);
            session->incoming[index] = packet.payload.state.pair;
        }
        else if (packet.payload.state.count != 0
         || memcmp(&packet.payload.state.pair, &empty.payload.state.pair, sizeof(struct SoulLinkPair)))
            return Fail(session, SOUL_LINK_SESSION_INVALID_PACKET);
        empty.payload.state = packet.payload.state;
    }
    else if (sequence == rows + 3 + session->proposalRows || sequence == rows + 4 + session->proposalRows)
    {
        u8 type = sequence == rows + 3 + session->proposalRows ? SOUL_LINK_PACKET_READY : SOUL_LINK_PACKET_ACK;
        if (packet.type != type || packet.payloadSize != sizeof(packet.payload.ping) || packet.payload.ping.reserved != 0
         || packet.payload.ping.count != session->mergedCount
         || packet.payload.ping.digest != CanonicalDigest(session->merged, session->mergedCount)
         || packet.payload.ping.baselineCount || packet.payload.ping.baselineDigest || packet.payload.ping.mode
         || packet.payload.ping.reserved2 || packet.payload.ping.reserved3 || ChangedCount(packet.payload.ping.changed)
         || session->send.sequence != sequence || session->send.type != type)
            return Fail(session, SOUL_LINK_SESSION_CONFLICT);
        empty.payload.ping = packet.payload.ping;
    }
    else if (sequence == rows + 5 + session->proposalRows && session->header.config.enforcePartyPairs)
    {
        if (packet.type != SOUL_LINK_PACKET_TEAM_READY || packet.payloadSize != sizeof(packet.payload.team)
         || packet.payload.team.count > 6 || packet.payload.team.ready > TRUE || packet.payload.team.reserved != 0
         || packet.payload.team.diagnostic.reason >= SOUL_LINK_TEAM_ISSUE_COUNT
         || packet.payload.team.diagnostic.reserved != 0
         || packet.payload.team.diagnostic.species >= NUM_SPECIES
         || packet.payload.team.diagnostic.neededSlots > 6 || packet.payload.team.diagnostic.availableSlots > 6
         || (packet.payload.team.ready && memcmp(&packet.payload.team.diagnostic, &empty.payload.team.diagnostic, sizeof(packet.payload.team.diagnostic)))
         || (!packet.payload.team.ready && packet.payload.team.diagnostic.reason == SOUL_LINK_TEAM_OK))
            return Fail(session, SOUL_LINK_SESSION_INVALID_PACKET);
        if (!packet.payload.team.ready || !session->send.payload.team.ready
         || session->send.type != SOUL_LINK_PACKET_TEAM_READY || session->send.sequence != sequence
         || packet.payload.team.count != session->send.payload.team.count
         || memcmp(packet.payload.team.ids, session->send.payload.team.ids, sizeof(packet.payload.team.ids)))
        {
            if (!session->send.payload.team.ready)
            {
                session->teamDiagnostic = session->send.payload.team.diagnostic;
                session->diagnosticPlayer = session->header.localPlayer;
            }
            else if (!packet.payload.team.ready)
            {
                session->teamDiagnostic = packet.payload.team.diagnostic;
                session->diagnosticPlayer = packet.sender;
            }
            else session->teamDiagnostic.reason = SOUL_LINK_TEAM_UNKNOWN;
            return Fail(session, SOUL_LINK_SESSION_TEAM_CONFLICT);
        }
        empty.payload.team = packet.payload.team;
    }
    else
        return Fail(session, SOUL_LINK_SESSION_INVALID_PACKET);
    if (memcmp(&empty.payload, &packet.payload, sizeof(packet.payload)))
        return Fail(session, SOUL_LINK_SESSION_INVALID_PACKET);
    if (sequence == rows + 3 + session->proposalRows)
    {
        if (!SaveUnchanged(session))
            return Fail(session, SOUL_LINK_SESSION_CONFLICT);
        if (!SoulLink_CommitRemoteState(session->merged, session->mergedCount))
            return Fail(session, SOUL_LINK_SESSION_CONFLICT);
    }
    session->sequence++;
    if (sequence == rows + 5 + session->proposalRows && session->header.config.enforcePartyPairs)
    {
        session->commitTeam(session->teamContext);
        session->status = SOUL_LINK_SESSION_COMPLETE;
    }
    else if (sequence == rows + 4 + session->proposalRows && !session->header.config.enforcePartyPairs)
        session->status = SOUL_LINK_SESSION_COMPLETE;
    if (session->status == SOUL_LINK_SESSION_COMPLETE) RememberBaseline(session);
    return TRUE;
}
