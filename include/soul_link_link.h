#ifndef GUARD_SOUL_LINK_LINK_H
#define GUARD_SOUL_LINK_LINK_H

#include "soul_link_protocol.h"

#define LINKTYPE_SOUL_LINK 0x534C

// Optional mGBA companion transport. All packets still pass through the same
// ROM protocol validation and commit path. Buffers live in the session heap.
#define SOUL_LINK_REMOTE_MAGIC 0x31524C53
#define SOUL_LINK_REMOTE_WINDOW 32
struct SoulLinkRemoteMailbox
{
    u32 magic;
    u32 epoch;
    u8 txReady;
    u8 rxReady;
    u8 connected;
    u8 abort;
    struct SoulLinkPacket tx;
    struct SoulLinkPacket rx;
    u16 txCount;
    u16 rxCount;
    struct SoulLinkPacket txExtra[SOUL_LINK_REMOTE_WINDOW - 1];
    struct SoulLinkPacket rxExtra[SOUL_LINK_REMOTE_WINDOW - 1];
};
extern u32 gSoulLinkRemoteEnabled;
extern u32 gSoulLinkRemoteEpoch;
extern struct SoulLinkRemoteMailbox *gSoulLinkRemoteMailbox;

// Call from an idle overworld. Owns the cable channel until completion/cancel.
bool8 SoulLink_StartCableSession(void);
bool8 SoulLink_StartCompanionSession(void);
void SoulLink_CancelCableSession(void);
u8 SoulLink_GetSessionStatus(void);
bool8 SoulLink_GetTeamDiagnostic(struct SoulLinkTeamDiagnostic *diagnostic, u8 *player);

#endif
