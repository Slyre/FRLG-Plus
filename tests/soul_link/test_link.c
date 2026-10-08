// Reuse the two independent saves and real protocol from the protocol tests.
#define main ProtocolTestMain
#include "test_protocol.c"
#undef main
#include <stdlib.h>
#include "link.h"
#include "soul_link_link.h"

struct Main gMain;
bool8 gSoftResetDisabled, gReceivedRemoteLinkPlayers;
u8 gWirelessCommType;
u8 gShouldAdvanceLinkState;
u16 gLinkType, gBlockRecvBuffer[5][128];
struct LinkPlayer gLinkPlayers[5];
static void (*sTask)(u8);
static bool8 sLocked, sIdle, sError, sAllocationFailure, sSendBlocked, sTaskFailure;
static bool8 sBusy;
static u8 sCount, sFlags, sPhysicalId;
static u16 sSize, sSends;
static unsigned sAllocations;
static u8 sOtherTasks;
static bool8 sHandshake;
static u8 sPairNotices;
static bool8 sFailedNotice;
static u8 sDeathNotices;

bool8 SoulLink_PrepareTeamChange(void *context, const SoulLinkPairId *ids, u8 count)
{
    (void)context; (void)ids; (void)count;
    return FALSE; // Storage behavior is exercised separately in the native ROM.
}
void SoulLink_CommitTeamChange(void *context) { (void)context; assert(FALSE); }
void SoulLink_DescribeTeamChange(void *context, struct SoulLinkTeamDiagnostic *diagnostic) { (void)context; memset(diagnostic, 0, sizeof(*diagnostic)); }

void SoulLink_NotifyPairSync(bool8 failed) { sPairNotices++; sFailedNotice = failed; }
void SoulLink_NotifyDeathSync(void) { sDeathNotices++; }

static void *sCacheAllocations[2];
void *Alloc(u32 size)
{
    if (sAllocationFailure || gSoulLinkTestCacheAllocationFailure) return NULL;
    for (unsigned i = 0; i < 2; i++) if (!sCacheAllocations[i])
        return sCacheAllocations[i] = malloc(size);
    assert(!"Unexpected persistent cache allocation"); return NULL;
}
void *AllocZeroed(u32 size)
{
    if (sAllocationFailure) return NULL;
    sAllocations++;
    return calloc(1, size);
}
void Free(void *pointer)
{
    assert(pointer != NULL);
    for (unsigned i = 0; i < 2; i++) if (sCacheAllocations[i] == pointer)
    { sCacheAllocations[i] = NULL; free(pointer); return; }
    sAllocations--; free(pointer);
}
u8 CreateTask(void (*callback)(u8), u8 priority)
{
    (void)priority;
    if (sTaskFailure) return 0xFF;
    assert(sTask == NULL); sTask = callback; return 0;
}
void DestroyTask(u8 id)
{
    if (id == 1) { assert(sHandshake); sHandshake = FALSE; return; }
    assert(id == 0 && sTask != NULL); sTask = NULL;
}
u8 GetTaskCount(void) { return sOtherTasks + (sTask != NULL) + sHandshake; }
void Task_TriggerHandshake(u8 id) { (void)id; }
u8 FindTaskIdByFunc(void (*callback)(u8))
{
    assert(callback == Task_TriggerHandshake);
    return sHandshake ? 1 : 0xFF;
}
bool8 IsOverworldIdle(void) { return sIdle && !sLocked; }
void LockPlayerFieldControls(void) { assert(!sLocked); sLocked = TRUE; }
void UnlockPlayerFieldControls(void) { assert(sLocked); sLocked = FALSE; }
bool8 IsLinkTaskFinished(void) { return !sBusy; }
void ClearLinkCallback_2(void) {}
void CloseLink(void) { gReceivedRemoteLinkPlayers = FALSE; }
void ResetBlockReceivedFlags(void) { sFlags = 0; }
void SetSuppressLinkErrorMessage(bool8 value) { (void)value; }
void OpenLinkTimed(void) { gReceivedRemoteLinkPlayers = FALSE; sError = FALSE; sHandshake = TRUE; }
void ResetLinkPlayerCount(void) { sCount = 0; }
void ResetLinkPlayers(void) { memset(gLinkPlayers, 0, sizeof(gLinkPlayers)); }
bool8 HasLinkErrorOccurred(void) { return sError; }
u8 GetLinkPlayerCount_2(void) { return sCount; }
bool8 IsLinkMaster(void) { return sPhysicalId == 0; }
void CheckShouldAdvanceLinkState(void) {}
u8 GetLinkPlayerDataExchangeStatusTimed(int min, int max) { assert(min == 2 && max == 2); return 1; }
u8 GetMultiplayerId(void) { return sPhysicalId; }
u8 GetBlockReceivedStatus(void) { return sFlags; }
u16 GetCableBlockReceivedSize(u8 player) { assert(player == (sPhysicalId ^ 1)); return sSize; }
bool8 SendBlock(u8 unused, const void *packet, u16 size)
{
    (void)unused;
    if (sSendBlocked) return FALSE;
    assert(size == sizeof(struct SoulLinkPacket));
    Select(1);
    assert(SoulLink_ProtocolBuild(&sPeers[1].protocol));
    memcpy(gBlockRecvBuffer[sPhysicalId ^ 1], &sPeers[1].protocol.send, size);
    assert(SoulLink_ProtocolReceive(&sPeers[1].protocol, packet, size));
    memcpy(gBlockRecvBuffer[sPhysicalId], packet, size);
    Select(0);
    sFlags = 3; sSize = size; sSends++;
    return TRUE;
}

static void ResetAdapter(void)
{
    assert(sTask == NULL && sAllocations == 0 && !sLocked);
    Setup();
    Select(1); assert(SoulLink_ProtocolInit(&sPeers[1].protocol));
    Select(0);
    gSoftResetDisabled = FALSE; gWirelessCommType = 0; gLinkType = 0;
    gReceivedRemoteLinkPlayers = FALSE; gMain.newKeys = 0;
    sIdle = TRUE; sError = sAllocationFailure = sSendBlocked = sTaskFailure = FALSE;
    sCount = sFlags = sSends = 0; sPhysicalId = 0;
    sOtherTasks = 0; sHandshake = FALSE; gShouldAdvanceLinkState = 0;
    sBusy = FALSE;
    sPairNotices = 0; sFailedNotice = FALSE;
    sDeathNotices = 0;
    gSoulLinkRemoteEnabled = 0;
}

static void Connected(void)
{
    sCount = 2; gReceivedRemoteLinkPlayers = TRUE;
    gLinkPlayers[0].linkType = gLinkPlayers[1].linkType = LINKTYPE_SOUL_LINK;
}

static void Tick(void) { assert(sTask != NULL); Select(0); sTask(0); }
static void Finished(u8 status)
{
    assert(SoulLink_GetSessionStatus() == status);
    assert(sTask == NULL && sAllocations == 0 && !sLocked && gLinkType == 0);
    assert(!sHandshake && gShouldAdvanceLinkState == 0);
    assert(!gSoftResetDisabled);
}

int main(void)
{
    struct SoulLinkPair pair = Bound(1), result;
    ResetAdapter();
    sIdle = FALSE; assert(!SoulLink_StartCableSession()); sIdle = TRUE;
    gWirelessCommType = 1; assert(!SoulLink_StartCableSession()); gWirelessCommType = 0;
    gLinkType = 0x1111; assert(!SoulLink_StartCableSession()); gLinkType = 0;
    sAllocationFailure = TRUE; assert(!SoulLink_StartCableSession()); sAllocationFailure = FALSE;
    sTaskFailure = TRUE; assert(!SoulLink_StartCableSession()); sTaskFailure = FALSE;
    sBusy = TRUE; assert(!SoulLink_StartCableSession()); sBusy = FALSE;
    sOtherTasks = 15; assert(!SoulLink_StartCableSession()); sOtherTasks = 0;
    sHandshake = TRUE; assert(!SoulLink_StartCableSession()); sHandshake = FALSE;
    assert(sAllocations == 0 && !sLocked);
    assert(SoulLink_StartCableSession()); assert(!SoulLink_StartCableSession());
    assert(sLocked && gSoftResetDisabled);
    gShouldAdvanceLinkState = 1;
    SoulLink_CancelCableSession(); Finished(SOUL_LINK_SESSION_CANCELED);
    SoulLink_CancelCableSession();

    ResetAdapter(); assert(SoulLink_StartCableSession());
    gMain.newKeys = B_BUTTON; Tick(); Finished(SOUL_LINK_SESSION_CANCELED);
    ResetAdapter(); assert(SoulLink_StartCableSession());
    for (unsigned i = 0; i < 1801; i++) Tick();
    Finished(SOUL_LINK_SESSION_TIMEOUT);
    ResetAdapter(); assert(SoulLink_StartCableSession()); sError = TRUE; Tick();
    Finished(SOUL_LINK_SESSION_DISCONNECTED);
    ResetAdapter(); assert(SoulLink_StartCableSession()); sCount = 3; Tick();
    Finished(SOUL_LINK_SESSION_INCOMPATIBLE);

    ResetAdapter();
    PutBoth(&pair); Select(0); assert(SoulLink_MarkLocalDeath(&pair.mons[0]));
    Select(1); assert(SoulLink_ProtocolInit(&sPeers[1].protocol)); Select(0);
    sPhysicalId = 1; // Logical coordinator is the physical cable slave.
    assert(SoulLink_StartCableSession()); Connected();
    sSendBlocked = TRUE; Tick(); assert(sSends == 0 && sLocked);
    sSendBlocked = FALSE;
    sBusy = TRUE; Tick(); assert(sSends == 0 && sLocked); sBusy = FALSE;
    Tick(); assert(sSends == 1);
    sBusy = TRUE; Tick(); assert(sSends == 1 && sLocked); sBusy = FALSE;
    for (unsigned i = 0; sTask != NULL && i < 20; i++) Tick();
    assert(sPeers[1].protocol.status == SOUL_LINK_SESSION_COMPLETE);
    assert(sTask != NULL && sLocked && gLinkType == LINKTYPE_SOUL_LINK);
    for (unsigned i = 0; sTask != NULL && i < 60; i++) Tick();
    Finished(SOUL_LINK_SESSION_COMPLETE); assert(sSends == 6);
    Select(1); assert(SoulLink_ReadPair(0, &result));
    assert(result.state == SOUL_LINK_PAIR_DEAD && result.encounterStates[1] == SOUL_LINK_ENCOUNTER_DEAD);
    assert(sPairNotices == 0); // Updating an existing pair does not announce a new binding.

    ResetAdapter();
    {
        struct SoulLinkMonIdentity mon = {10, 101, SPECIES_BULBASAUR, 0};
        Select(0); assert(SoulLink_RecordLocalEncounter(8, &mon, SOUL_LINK_ENCOUNTER_CAPTURED));
        mon.otId = 20; mon.personality = 201;
        Select(1); assert(SoulLink_RecordLocalEncounter(8, &mon, SOUL_LINK_ENCOUNTER_CAPTURED));
        assert(SoulLink_ProtocolInit(&sPeers[1].protocol)); Select(0);
    }
    assert(SoulLink_StartCableSession()); Connected();
    for (unsigned i = 0; sTask != NULL && i < 20; i++) Tick();
    assert(sPeers[1].protocol.status == SOUL_LINK_SESSION_COMPLETE);
    assert(sTask != NULL && sLocked && gLinkType == LINKTYPE_SOUL_LINK);
    for (unsigned i = 0; sTask != NULL && i < 60; i++) Tick();
    Finished(SOUL_LINK_SESSION_COMPLETE);
    assert(sPairNotices == 1 && !sFailedNotice);

    ResetAdapter();
    {
        struct SoulLinkMonIdentity mon = {10, 101, SPECIES_BULBASAUR, 0};
        Select(0); assert(SoulLink_RecordLocalEncounter(8, &mon, SOUL_LINK_ENCOUNTER_CAPTURED));
        Select(1); assert(SoulLink_RecordLocalEncounter(8, NULL, SOUL_LINK_ENCOUNTER_FAILED));
        assert(SoulLink_ProtocolInit(&sPeers[1].protocol)); Select(0);
    }
    assert(SoulLink_StartCableSession()); Connected();
    for (unsigned i = 0; sTask != NULL && i < 20; i++) Tick();
    assert(sPeers[1].protocol.status == SOUL_LINK_SESSION_COMPLETE);
    assert(sTask != NULL && sLocked && gLinkType == LINKTYPE_SOUL_LINK);
    for (unsigned i = 0; sTask != NULL && i < 60; i++) Tick();
    Finished(SOUL_LINK_SESSION_COMPLETE);
    assert(sPairNotices == 1 && sFailedNotice);

    ResetAdapter(); assert(SoulLink_StartCableSession()); Connected(); Tick();
    sCount = 1; Tick(); Finished(SOUL_LINK_SESSION_DISCONNECTED);
    ResetAdapter(); assert(SoulLink_StartCableSession()); Connected(); Tick();
    sSize = 63; Tick(); Finished(SOUL_LINK_SESSION_INVALID_PACKET);
    ResetAdapter(); assert(SoulLink_StartCableSession()); Connected(); Tick();
    sFlags = 1;
    for (unsigned i = 0; i < 601 && sTask != NULL; i++) Tick();
    Finished(SOUL_LINK_SESSION_TIMEOUT);

    for (unsigned lostAck = 0; lostAck < 2; lostAck++)
    {
        ResetAdapter(); PutBoth(&pair);
        Select(1); assert(SoulLink_MarkLocalDeath(&pair.mons[1]));
        assert(SoulLink_ProtocolInit(&sPeers[1].protocol)); Select(0);
        assert(SoulLink_StartCableSession()); Connected();
        for (unsigned i = 0; sTask != NULL && i < 20; i++)
        {
            Tick(); Select(0); assert(SoulLink_ReadPair(0, &result));
            if (lostAck && result.encounterStates[0] == SOUL_LINK_ENCOUNTER_DEAD && sTask != NULL)
                sCount = 1;
        }
        if (!lostAck) for (unsigned i = 0; sTask != NULL && i < 60; i++) Tick();
        Finished(lostAck ? SOUL_LINK_SESSION_DISCONNECTED : SOUL_LINK_SESSION_COMPLETE);
        assert(sDeathNotices == 1 && sPairNotices == 0);
        assert(result.encounterStates[0] == SOUL_LINK_ENCOUNTER_DEAD);
        // Reuse the adapter after teardown without resetting either save.
        Select(1); assert(SoulLink_ProtocolInit(&sPeers[1].protocol)); Select(0);
        assert(SoulLink_StartCableSession()); Connected();
        for (unsigned i = 0; sTask != NULL && i < 20; i++) Tick();
        assert(sPeers[1].protocol.status == SOUL_LINK_SESSION_COMPLETE);
        assert(sTask != NULL && sLocked && gLinkType == LINKTYPE_SOUL_LINK);
        for (unsigned i = 0; sTask != NULL && i < 60; i++) Tick();
        Finished(SOUL_LINK_SESSION_COMPLETE);
        assert(sDeathNotices == 1 && sPairNotices == 0);
    }

    ResetAdapter(); gSoftResetDisabled = TRUE;
    assert(SoulLink_StartCableSession()); SoulLink_CancelCableSession();
    assert(gSoftResetDisabled && sAllocations == 0 && !sLocked);
    // WAN sessions never open a hardware link. Delivery is acknowledged by
    // the other emulator, not by a local socket send.
    ResetAdapter(); PutBoth(&pair);
    Select(1); assert(SoulLink_MarkLocalDeath(&pair.mons[1]));
    assert(SoulLink_ProtocolInit(&sPeers[1].protocol)); Select(0);
    gSoulLinkRemoteEnabled = SOUL_LINK_REMOTE_MAGIC;
    assert(SoulLink_StartCableSession());
    assert(gSoulLinkRemoteMailbox && !sHandshake && gLinkType == 0);
    assert(gSoulLinkRemoteMailbox->epoch != 0);
    for (unsigned i = 0; i < 2200; i++) Tick(); // Wait for a distant player.
    gSoulLinkRemoteMailbox->connected = TRUE;
    unsigned rounds = 0;
    while (sTask != NULL && rounds++ < 20)
    {
        Tick();
        if (!gSoulLinkRemoteMailbox->txReady) continue;
        struct SoulLinkPacket packet = gSoulLinkRemoteMailbox->tx;
        Select(1); assert(SoulLink_ProtocolBuild(&sPeers[1].protocol));
        gSoulLinkRemoteMailbox->rx = sPeers[1].protocol.send;
        assert(SoulLink_ProtocolReceive(&sPeers[1].protocol, &packet, sizeof(packet)));
        Select(0); gSoulLinkRemoteMailbox->rxCount = 1; gSoulLinkRemoteMailbox->rxReady = TRUE;
        for (unsigned i = 0; i < 700; i++) Tick(); // Delayed delivery ACK.
        assert(gSoulLinkRemoteMailbox->txReady && sLocked);
        gSoulLinkRemoteMailbox->txReady = FALSE;
        Tick();
    }
    Finished(SOUL_LINK_SESSION_COMPLETE);
    assert(gSoulLinkRemoteMailbox == NULL && sDeathNotices == 1);
    assert(sPeers[1].protocol.status == SOUL_LINK_SESSION_COMPLETE);
    // Exercise the adapter's full window independently of Lua: receiving rows
    // must free the inbox before sender ACK, but cannot cross the commit barrier.
    for (unsigned cancel = 0; cancel < 3; cancel++)
    {
        ResetAdapter();
        for (u16 i = 1; i <= SOUL_LINK_PAIR_CAPACITY; i++)
        {
            struct SoulLinkPair row = Bound(i);
            PutBoth(&row);
        }
        struct SoulLinkPair first = Bound(1), result;
        Select(1); assert(SoulLink_MarkLocalDeath(&first.mons[1]));
        assert(SoulLink_ProtocolInit(&sPeers[1].protocol)); Select(0);
        gSoulLinkRemoteEnabled = SOUL_LINK_REMOTE_MAGIC;
        assert(SoulLink_StartCableSession());
        gSoulLinkRemoteMailbox->connected = TRUE;
        unsigned batches = 0, packets = 0, attempts = 0;
        while (sTask && attempts++ < 40)
        {
            Tick();
            if (!gSoulLinkRemoteMailbox->txReady) continue;
            u16 count = gSoulLinkRemoteMailbox->txCount;
            struct SoulLinkPacket head = gSoulLinkRemoteMailbox->tx;
            assert(count >= 1 && count <= SOUL_LINK_REMOTE_WINDOW);
            if (count > 1) batches++;
            for (u16 i = 0; i < count; i++)
            {
                struct SoulLinkPacket current = i ? gSoulLinkRemoteMailbox->txExtra[i - 1] : head;
                assert(current.sequence == head.sequence + i);
                Select(1); assert(SoulLink_ProtocolBuild(&sPeers[1].protocol));
                if (i) gSoulLinkRemoteMailbox->rxExtra[i - 1] = sPeers[1].protocol.send;
                else gSoulLinkRemoteMailbox->rx = sPeers[1].protocol.send;
                assert(SoulLink_ProtocolReceive(&sPeers[1].protocol, &current, sizeof(current)));
                Select(0); packets++;
            }
            gSoulLinkRemoteMailbox->rxCount = count;
            gSoulLinkRemoteMailbox->rxReady = TRUE;
            if (cancel == 2 && count > 1)
            {
                gSoulLinkRemoteMailbox->rxExtra[1].checksum ^= 1;
                Tick(); Finished(SOUL_LINK_SESSION_INVALID_PACKET);
                assert(SoulLink_ReadPair(0, &result) && result.state == SOUL_LINK_PAIR_ALIVE);
                break;
            }
            Tick();
            if (count > 1)
            {
                assert(!gSoulLinkRemoteMailbox->rxReady && gSoulLinkRemoteMailbox->txReady);
                assert(SoulLink_ReadPair(0, &result) && result.state == SOUL_LINK_PAIR_ALIVE);
                if (cancel)
                {
                    gSoulLinkRemoteMailbox->abort = TRUE; Tick();
                    Finished(SOUL_LINK_SESSION_DISCONNECTED);
                    assert(SoulLink_ReadPair(0, &result) && result.state == SOUL_LINK_PAIR_ALIVE);
                    break;
                }
            }
            if (!sTask) break;
            for (unsigned i = 0; i < 30; i++) Tick();
            assert(gSoulLinkRemoteMailbox->txReady && sLocked);
            assert(gSoulLinkRemoteMailbox->tx.sequence == head.sequence);
            gSoulLinkRemoteMailbox->txReady = FALSE; Tick();
        }
        if (!cancel)
        {
            Finished(SOUL_LINK_SESSION_COMPLETE);
            assert(batches == 3 && packets == 94);
            assert(SoulLink_ReadPair(0, &result) && result.state == SOUL_LINK_PAIR_DEAD);
        }
    }
    for (u16 invalid = 0; invalid < 3; invalid++)
    {
        ResetAdapter(); gSoulLinkRemoteEnabled = SOUL_LINK_REMOTE_MAGIC;
        assert(SoulLink_StartCableSession());
        gSoulLinkRemoteMailbox->connected = TRUE; Tick();
        gSoulLinkRemoteMailbox->txReady = FALSE;
        gSoulLinkRemoteMailbox->rxCount = invalid == 0 ? 0 : invalid == 1 ? 33 : 2;
        gSoulLinkRemoteMailbox->rxReady = TRUE; Tick();
        Finished(SOUL_LINK_SESSION_INVALID_PACKET);
    }
    ResetAdapter(); gSoulLinkRemoteEnabled = SOUL_LINK_REMOTE_MAGIC;
    assert(SoulLink_StartCableSession());
    u32 oldEpoch = gSoulLinkRemoteMailbox->epoch;
    gSoulLinkRemoteMailbox->abort = TRUE; Tick();
    Finished(SOUL_LINK_SESSION_DISCONNECTED);
    assert(gSoulLinkRemoteMailbox == NULL);
    assert(SoulLink_StartCableSession());
    assert(gSoulLinkRemoteMailbox->epoch != oldEpoch);
    gMain.newKeys = B_BUTTON; Tick(); Finished(SOUL_LINK_SESSION_CANCELED);
    ResetAdapter(); gSoulLinkRemoteEnabled = SOUL_LINK_REMOTE_MAGIC;
    assert(SoulLink_StartCableSession());
    for (unsigned i = 0; i < 18001 && sTask; i++) Tick();
    Finished(SOUL_LINK_SESSION_TIMEOUT);
    puts("Soul Link cable and WAN adapters: ownership, delivery backpressure, delayed ACK, epoch, cancel, timeouts and teardown passed");
    return 0;
}
