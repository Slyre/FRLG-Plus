#ifndef GUARD_SOUL_LINK_PROTOCOL_H
#define GUARD_SOUL_LINK_PROTOCOL_H

#include "soul_link_types.h"
#include "soul_link_diagnostics.h"

#define SOUL_LINK_PROTOCOL_VERSION 7
#define SOUL_LINK_PACKET_MAGIC 0x504B4C53

enum SoulLinkPacketType
{
    SOUL_LINK_PACKET_HELLO,
    SOUL_LINK_PACKET_PING,
    SOUL_LINK_PACKET_STATE,
    SOUL_LINK_PACKET_PLAN,
    SOUL_LINK_PACKET_PROPOSAL,
    SOUL_LINK_PACKET_READY,
    SOUL_LINK_PACKET_ACK,
    SOUL_LINK_PACKET_TEAM_READY,
};

enum SoulLinkSessionStatus
{
    SOUL_LINK_SESSION_IDLE,
    SOUL_LINK_SESSION_CONNECTING,
    SOUL_LINK_SESSION_SYNCING,
    SOUL_LINK_SESSION_COMPLETE,
    SOUL_LINK_SESSION_CANCELED,
    SOUL_LINK_SESSION_TIMEOUT,
    SOUL_LINK_SESSION_DISCONNECTED,
    SOUL_LINK_SESSION_INCOMPATIBLE,
    SOUL_LINK_SESSION_INVALID_PACKET,
    SOUL_LINK_SESSION_CONFLICT,
    SOUL_LINK_SESSION_TEAM_CONFLICT,
};

struct SoulLinkPacket
{
    u32 magic;
    struct SoulLinkRunId runId;
    u16 sequence;
    u8 version;
    u8 areaVersion;
    u8 sender;
    u8 type;
    u16 payloadSize;
    u32 checksum;
    u32 reserved;
    union
    {
        struct
        {
            u32 participantIds[2];
            struct SoulLinkConfig config;
            u32 configRevision;
            u16 count;
            u16 reserved;
            SoulLinkPairId team[6];
        } hello;
        struct
        {
            SoulLinkPairId ids[6];
            u8 count;
            bool8 ready;
            u16 reserved;
            struct SoulLinkTeamDiagnostic diagnostic;
        } team;
        struct
        {
            u32 digest;
            u16 count;
            u16 reserved;
            u32 baselineDigest;
            u16 baselineCount;
            u8 mode;
            u8 reserved2;
            u8 changed[11];
            u8 reserved3;
        } ping;
        struct
        {
            u16 index;
            u16 count;
            struct SoulLinkPair pair;
        } state;
    } payload;
};

// Heap-owned, short-lived snapshot. No persistent layout changes.
struct SoulLinkProtocol
{
    struct SoulLinkSaveHeader header;
    struct SoulLinkPair outgoing[SOUL_LINK_PAIR_CAPACITY];
    struct SoulLinkPair incoming[SOUL_LINK_PAIR_CAPACITY];
    struct SoulLinkPair merged[SOUL_LINK_PAIR_CAPACITY];
    struct SoulLinkPacket send;
    u32 remoteDigest;
    u32 proposalDigest;
    u16 remoteCount;
    u16 mergedCount;
    u16 proposalCount;
    u16 sequence;
    u16 stateRows;
    u16 proposalRows;
    u16 baselineCount;
    u32 baselineDigest;
    u8 changed[11];
    u8 proposalChanged[11];
    bool8 hasBaseline;
    bool8 delta;
    u8 status;
    SoulLinkPairId remoteTeam[6];
    void *teamContext;
    bool8 (*prepareTeam)(void *, const SoulLinkPairId *, u8);
    void (*commitTeam)(void *);
    void (*describeTeam)(void *, struct SoulLinkTeamDiagnostic *);
    struct SoulLinkTeamDiagnostic teamDiagnostic;
    u8 diagnosticPlayer;
};

void SoulLink_ProtocolResetHeapCache(void);
void SoulLink_ProtocolForgetBaseline(u8 player);
bool8 SoulLink_ProtocolInit(struct SoulLinkProtocol *session);
bool8 SoulLink_ProtocolBuild(struct SoulLinkProtocol *session);
bool8 SoulLink_ProtocolReceive(struct SoulLinkProtocol *session, const void *data, u16 size);
u16 SoulLink_ProtocolBatchEnd(const struct SoulLinkProtocol *session, u8 type);
u32 SoulLink_StateDigest(const struct SoulLinkPair *pairs, u16 count);

#endif
