#include "global.h"
#include "field_message_box.h"
#include "link.h"
#include "main.h"
#include "malloc.h"
#include "overworld.h"
#include "script.h"
#include "soul_link.h"
#include "soul_link_link.h"
#include "soul_link_local.h"
#include "soul_link_team.h"
#include "task.h"

struct SoulLinkCableSession
{
    struct SoulLinkProtocol protocol;
    struct SoulLinkTeamPlan team;
    u16 timer;
    u16 remoteRemaining;
    u8 taskId;
    u8 phase;
    u8 physicalId;
    bool8 oldSoftReset;
    bool8 remote;
    bool8 companion;
    struct SoulLinkRemoteMailbox mailbox;
};

static EWRAM_DATA struct SoulLinkCableSession *sSession = NULL;
static EWRAM_DATA u8 sSessionStatus = SOUL_LINK_SESSION_IDLE;
static EWRAM_DATA struct SoulLinkTeamDiagnostic sTeamDiagnostic = {0};
static EWRAM_DATA u8 sDiagnosticPlayer = 0;
EWRAM_DATA u32 gSoulLinkRemoteEnabled = 0;
EWRAM_DATA u32 gSoulLinkRemoteEpoch = 0;
EWRAM_DATA struct SoulLinkRemoteMailbox *gSoulLinkRemoteMailbox = NULL;

static void Task_SoulLinkCable(u8 taskId);
static void Task_SoulLinkRemote(void);

STATIC_ASSERT(sizeof(struct SoulLinkRemoteMailbox) == 4112, SoulLinkRemoteMailboxSize);

static void NotifySynchronizedDeaths(void)
{
    u16 i, j;
    u8 local = sSession->protocol.header.localPlayer;
    struct SoulLinkPair row;
    for (i = 0; i < SoulLink_GetPairCount(); i++)
    {
        if (!SoulLink_ReadPair(i, &row) || row.encounterStates[local] != SOUL_LINK_ENCOUNTER_DEAD
         || !(row.cleanupFlags & (1 << local)))
            continue;
        for (j = 0; j < sSession->protocol.header.pairCount; j++)
        {
            const struct SoulLinkPair *old = &sSession->protocol.outgoing[j];
            if ((old->pairId == row.pairId || (old->pairId == 0
              && old->mons[local].encounterOrdinal == row.mons[local].encounterOrdinal))
             && old->encounterStates[local] == SOUL_LINK_ENCOUNTER_DEAD)
                break;
        }
        if (j == sSession->protocol.header.pairCount)
        {
            SoulLink_NotifyDeathSync();
            break;
        }
    }
}

static void Finish(u8 status)
{
    u8 handshakeTask = sSession->remote ? TASK_NONE : FindTaskIdByFunc(Task_TriggerHandshake);
    // Automatic exchanges report their changes in the companion. Modal field
    // notices would require a key press and prevent the next automatic update.
    if (!sSession->companion)
        NotifySynchronizedDeaths(); // Includes a committed death followed by a lost ACK.
    if (!sSession->companion && (status == SOUL_LINK_SESSION_COMPLETE || status == SOUL_LINK_SESSION_TEAM_CONFLICT))
    {
        u16 i, j;
        bool8 paired = FALSE, failed = FALSE;
        for (i = 0; i < sSession->protocol.mergedCount; i++)
        {
            const struct SoulLinkPair *row = &sSession->protocol.merged[i];
            if (row->encounterStates[0] == SOUL_LINK_ENCOUNTER_NONE || row->encounterStates[1] == SOUL_LINK_ENCOUNTER_NONE)
                continue;
            for (j = 0; j < sSession->protocol.header.pairCount; j++)
                if (sSession->protocol.outgoing[j].pairId == row->pairId
                 && sSession->protocol.outgoing[j].encounterStates[0] != SOUL_LINK_ENCOUNTER_NONE
                 && sSession->protocol.outgoing[j].encounterStates[1] != SOUL_LINK_ENCOUNTER_NONE)
                    break;
            if (j == sSession->protocol.header.pairCount)
            {
                paired = TRUE;
                if (row->encounterStates[0] == SOUL_LINK_ENCOUNTER_FAILED || row->encounterStates[1] == SOUL_LINK_ENCOUNTER_FAILED)
                    failed = TRUE;
            }
        }
        if (paired)
            SoulLink_NotifyPairSync(failed);
    }
    if (handshakeTask != TASK_NONE)
        DestroyTask(handshakeTask);
    gShouldAdvanceLinkState = 0;
    sTeamDiagnostic = sSession->protocol.teamDiagnostic;
    sDiagnosticPlayer = sSession->protocol.diagnosticPlayer;
    sSessionStatus = status;
    if (!sSession->remote)
    {
        ClearLinkCallback_2();
        CloseLink();
        ResetBlockReceivedFlags();
        gLinkType = 0;
        SetSuppressLinkErrorMessage(FALSE);
    }
    gSoulLinkRemoteMailbox = NULL;
    gSoftResetDisabled = sSession->oldSoftReset;
    DestroyTask(sSession->taskId);
    Free(sSession);
    sSession = NULL;
    UnlockPlayerFieldControls();
}

bool8 SoulLink_StartCableSession(void)
{
    if (sSession != NULL || !SoulLink_IsEnabled() || gWirelessCommType
     || gReceivedRemoteLinkPlayers || gLinkType != 0 || !IsLinkTaskFinished()
     || !IsOverworldIdle() || GetTaskCount() > NUM_TASKS - 2
     || FindTaskIdByFunc(Task_TriggerHandshake) != TASK_NONE)
        return FALSE;
    sSession = AllocZeroed(sizeof(*sSession));
    if (sSession == NULL)
        return FALSE;
    if (!SoulLink_ProtocolInit(&sSession->protocol))
    {
        Free(sSession);
        sSession = NULL;
        return FALSE;
    }
    sSession->protocol.teamContext = &sSession->team;
    sSession->protocol.prepareTeam = SoulLink_PrepareTeamChange;
    sSession->protocol.commitTeam = SoulLink_CommitTeamChange;
    sSession->protocol.describeTeam = SoulLink_DescribeTeamChange;
    sSession->taskId = CreateTask(Task_SoulLinkCable, 80);
    if (sSession->taskId == TASK_NONE)
    {
        Free(sSession);
        sSession = NULL;
        return FALSE;
    }
    sSession->oldSoftReset = gSoftResetDisabled;
    gSoftResetDisabled = TRUE;
    LockPlayerFieldControls();
    sSession->remote = gSoulLinkRemoteEnabled == SOUL_LINK_REMOTE_MAGIC;
    if (sSession->remote)
    {
        sSession->mailbox.magic = SOUL_LINK_REMOTE_MAGIC;
        if (++gSoulLinkRemoteEpoch == 0) ++gSoulLinkRemoteEpoch;
        sSession->mailbox.epoch = gSoulLinkRemoteEpoch;
        gSoulLinkRemoteMailbox = &sSession->mailbox;
    }
    else
    {
        gLinkType = LINKTYPE_SOUL_LINK;
        OpenLinkTimed();
        ResetLinkPlayerCount();
        ResetLinkPlayers();
        SetSuppressLinkErrorMessage(TRUE);
    }
    memset(&sTeamDiagnostic, 0, sizeof(sTeamDiagnostic));
    sSessionStatus = SOUL_LINK_SESSION_CONNECTING;
    return TRUE;
}

bool8 SoulLink_StartCompanionSession(void)
{
    if (gSoulLinkRemoteEnabled != SOUL_LINK_REMOTE_MAGIC || !SoulLink_StartCableSession()) return FALSE;
    sSession->companion = TRUE;
    return TRUE;
}

void SoulLink_CancelCableSession(void)
{
    if (sSession != NULL)
        Finish(SOUL_LINK_SESSION_CANCELED);
}

u8 SoulLink_GetSessionStatus(void)
{
    return sSessionStatus;
}

bool8 SoulLink_GetTeamDiagnostic(struct SoulLinkTeamDiagnostic *diagnostic, u8 *player)
{
    if (sSessionStatus != SOUL_LINK_SESSION_TEAM_CONFLICT || diagnostic == NULL || player == NULL) return FALSE;
    *diagnostic = sTeamDiagnostic; *player = sDiagnosticPlayer;
    return TRUE;
}

static void Task_SoulLinkCable(u8 taskId)
{
    u8 status, playerCount;
    (void)taskId;
    if (sSession->remote)
    {
        Task_SoulLinkRemote();
        return;
    }
    // Keep the channel alive long enough for the other endpoint to consume the
    // final block. Its completion may lag ours by an interrupt or a game frame.
    if (sSession->phase == 3)
    {
        if (++sSession->timer >= 60 || HasLinkErrorOccurred() || !gReceivedRemoteLinkPlayers)
            Finish(sSession->protocol.status);
        return;
    }
    if (gMain.newKeys & B_BUTTON)
    {
        Finish(SOUL_LINK_SESSION_CANCELED);
        return;
    }
    if (HasLinkErrorOccurred())
    {
        Finish(SOUL_LINK_SESSION_DISCONNECTED);
        return;
    }
    if (++sSession->timer > (sSession->phase == 0 ? 1800 : 600))
    {
        Finish(SOUL_LINK_SESSION_TIMEOUT);
        return;
    }
    playerCount = GetLinkPlayerCount_2();
    if (playerCount > SOUL_LINK_PLAYER_COUNT || gLinkType != LINKTYPE_SOUL_LINK)
    {
        Finish(SOUL_LINK_SESSION_INCOMPATIBLE);
        return;
    }
    if (sSession->phase == 0)
    {
        if (playerCount != SOUL_LINK_PLAYER_COUNT)
            return;
        if (IsLinkMaster())
            CheckShouldAdvanceLinkState();
        if (!gReceivedRemoteLinkPlayers)
            return;
        status = GetLinkPlayerDataExchangeStatusTimed(2, 2);
        if (status != EXCHANGE_COMPLETE)
        {
            Finish(SOUL_LINK_SESSION_INCOMPATIBLE);
            return;
        }
        if (!IsLinkTaskFinished())
            return;
        sSession->physicalId = GetMultiplayerId();
        if (sSession->physicalId >= SOUL_LINK_PLAYER_COUNT
         || gLinkPlayers[0].linkType != LINKTYPE_SOUL_LINK || gLinkPlayers[1].linkType != LINKTYPE_SOUL_LINK)
        {
            Finish(SOUL_LINK_SESSION_INCOMPATIBLE);
            return;
        }
        ResetBlockReceivedFlags();
        sSession->phase = 1;
        sSession->timer = 0;
        sSessionStatus = SOUL_LINK_SESSION_SYNCING;
    }
    if (!gReceivedRemoteLinkPlayers || playerCount != SOUL_LINK_PLAYER_COUNT
     || GetMultiplayerId() != sSession->physicalId)
    {
        Finish(SOUL_LINK_SESSION_DISCONNECTED);
        return;
    }
    if (sSession->phase == 1)
    {
        if (!IsLinkTaskFinished())
            return;
        if (!SoulLink_ProtocolBuild(&sSession->protocol))
        {
            Finish(sSession->protocol.status);
            return;
        }
        if (SendBlock(0, &sSession->protocol.send, sizeof(struct SoulLinkPacket)))
            sSession->phase = 2;
        return;
    }
    status = GetBlockReceivedStatus();
    if (status & ~3)
    {
        Finish(SOUL_LINK_SESSION_INVALID_PACKET);
        return;
    }
    if (status != 3 || !IsLinkTaskFinished())
        return;
    if (!SoulLink_ProtocolReceive(&sSession->protocol, gBlockRecvBuffer[sSession->physicalId ^ 1],
                                  GetCableBlockReceivedSize(sSession->physicalId ^ 1)))
    {
        if (sSession->protocol.status == SOUL_LINK_SESSION_TEAM_CONFLICT)
        {
            sSession->phase = 3;
            sSession->timer = 0;
        }
        else Finish(sSession->protocol.status);
        return;
    }
    ResetBlockReceivedFlags();
    if (sSession->protocol.status == SOUL_LINK_SESSION_COMPLETE)
    {
        sSession->phase = 3;
        sSession->timer = 0;
        return;
    }
    sSession->phase = 1;
    sSession->timer = 0;
}

static void Task_SoulLinkRemote(void)
{
    struct SoulLinkRemoteMailbox *mailbox = &sSession->mailbox;
    u16 i;
    if (!sSession->companion && (gMain.newKeys & B_BUTTON))
    {
        Finish(SOUL_LINK_SESSION_CANCELED);
        return;
    }
    if (mailbox->abort)
    {
        Finish(SOUL_LINK_SESSION_DISCONNECTED);
        return;
    }
    // Frame-based safety net; the companion additionally uses wall-clock
    // deadlines, including when an emulator is paused or accelerated.
    if (++sSession->timer > (sSession->companion ? (sSession->phase == 0 ? 180 : 600) : (sSession->phase == 0 ? 18000 : 9000)))
    {
        Finish(SOUL_LINK_SESSION_TIMEOUT);
        return;
    }
    if (sSession->phase == 0)
    {
        if (!mailbox->connected) return;
        sSession->phase = 1;
        sSession->timer = 0;
        sSessionStatus = SOUL_LINK_SESSION_SYNCING;
    }
    if (sSession->phase == 1)
    {
        if (!SoulLink_ProtocolBuild(&sSession->protocol))
        {
            Finish(sSession->protocol.status);
            return;
        }
        mailbox->tx = sSession->protocol.send;
        mailbox->txCount = 1;
        // Only immutable row-transfer phases can be sent ahead. HELLO, digest,
        // plan, commit and team barriers retain their delivery acknowledgements.
        if (mailbox->tx.type == SOUL_LINK_PACKET_STATE || mailbox->tx.type == SOUL_LINK_PACKET_PROPOSAL)
        {
            u16 sequence = sSession->protocol.sequence;
            u16 end = SoulLink_ProtocolBatchEnd(&sSession->protocol, mailbox->tx.type);
            while (mailbox->txCount < SOUL_LINK_REMOTE_WINDOW && sequence + mailbox->txCount < end)
            {
                sSession->protocol.sequence = sequence + mailbox->txCount;
                if (!SoulLink_ProtocolBuild(&sSession->protocol))
                {
                    Finish(sSession->protocol.status);
                    return;
                }
                mailbox->txExtra[mailbox->txCount - 1] = sSession->protocol.send;
                mailbox->txCount++;
            }
            sSession->protocol.sequence = sequence;
            sSession->protocol.send = mailbox->tx;
        }
        sSession->remoteRemaining = mailbox->txCount;
        mailbox->txReady = TRUE;
        sSession->phase = 2;
        return;
    }
    // Drain row inboxes while the batch is in flight, otherwise both players
    // would wait forever for delivery into a full inbox. Rows cannot commit.
    // Every batch must be fully acknowledged before crossing its next barrier.
    if (sSession->remoteRemaining == 0)
    {
        if (mailbox->txReady) return;
        sSession->phase = 1;
        sSession->timer = 0;
        return;
    }
    if (!mailbox->rxReady || (mailbox->txReady
     && mailbox->tx.type != SOUL_LINK_PACKET_STATE && mailbox->tx.type != SOUL_LINK_PACKET_PROPOSAL)) return;
    if (mailbox->rxCount == 0 || mailbox->rxCount > SOUL_LINK_REMOTE_WINDOW
     || mailbox->rxCount > sSession->remoteRemaining)
    {
        Finish(SOUL_LINK_SESSION_INVALID_PACKET);
        return;
    }
    mailbox->rxReady = FALSE;
    for (i = 0; i < mailbox->rxCount; i++)
    {
        const struct SoulLinkPacket *packet = i ? &mailbox->rxExtra[i - 1] : &mailbox->rx;
        if (!SoulLink_ProtocolReceive(&sSession->protocol, packet, sizeof(*packet)))
        {
            Finish(sSession->protocol.status);
            return;
        }
        if (sSession->protocol.status == SOUL_LINK_SESSION_COMPLETE)
        {
            Finish(SOUL_LINK_SESSION_COMPLETE);
            return;
        }
        sSession->remoteRemaining--;
    }
    sSession->timer = 0;
}
