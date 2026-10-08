#ifndef GUARD_SOUL_LINK_H
#define GUARD_SOUL_LINK_H

#include "soul_link_types.h"

void SoulLink_ResetSaveData(void);
void SoulLink_InvalidateRuntime(void);
bool8 SoulLink_LoadSaveData(void);
bool8 SoulLink_IsLegacySaveBlock2(const void *data, u16 size);
u8 SoulLink_GetSaveStatus(void);
bool8 SoulLink_IsEnabled(void);
bool8 SoulLink_IsLinkedDeathsEnabled(void);
bool8 SoulLink_IsSameAreaRequired(void);
bool8 SoulLink_IsTypeClauseEnabled(void);
bool8 SoulLink_ArePartyChangesLocked(void);
bool8 SoulLink_SetTeamSelection(const SoulLinkPairId *ids, u8 count);
bool8 SoulLink_GetConfig(struct SoulLinkConfig *config);
bool8 SoulLink_SetConfig(const struct SoulLinkConfig *config);
void SoulLink_GenerateRunId(struct SoulLinkRunId *runId);
bool8 SoulLink_BeginRun(const struct SoulLinkRunId *runId, const u32 *participantIds, u8 localPlayer);
bool8 SoulLink_GetRunId(struct SoulLinkRunId *runId);
u16 SoulLink_GetPairCount(void);
bool8 SoulLink_ReadPair(u16 index, struct SoulLinkPair *pair);
bool8 SoulLink_CreatePair(const struct SoulLinkPair *pair, SoulLinkPairId *pairId);
bool8 SoulLink_StorePair(const struct SoulLinkPair *pair);
bool8 SoulLink_SetGraveBox(u8 boxId);
// Player 1 keeps provisional candidates with pairId NONE until synchronization.
bool8 SoulLink_RecordLocalEncounter(u16 areaId, const struct SoulLinkMonIdentity *mon, u8 result);
bool8 SoulLink_CanRecordLocalEncounter(u16 areaId);
u16 SoulLink_ApplyPendingLinkedDeaths(void);
bool8 SoulLink_FindLocalPair(const struct SoulLinkMonIdentity *mon, u16 *index, struct SoulLinkPair *pair);
bool8 SoulLink_IsLegacyShedinja(const struct SoulLinkMonIdentity *mon);
bool8 SoulLink_MarkLocalDeath(const struct SoulLinkMonIdentity *mon);
bool8 SoulLink_ReplaceLocalIdentity(const struct SoulLinkMonIdentity *oldMon, const struct SoulLinkMonIdentity *newMon);
// Caller supplies capacity-sized workspace. Preparation never changes the save.
bool8 SoulLink_PrepareRemoteState(const struct SoulLinkPair *remote, u16 count, u8 player, struct SoulLinkPair *merged, u16 *mergedCount);
bool8 SoulLink_CommitRemoteState(const struct SoulLinkPair *merged, u16 count);
bool8 SoulLink_ApplyRemoteState(const struct SoulLinkPair *remote, u16 count, u8 player,
                               struct SoulLinkPair *scratch);
struct SoulLinkApplyJob;
u32 SoulLink_ApplyJobSize(void);
bool8 SoulLink_BeginApplyJob(struct SoulLinkApplyJob *job, const struct SoulLinkPair *remote, u16 count, u8 player);
// Pending, committed, rejected, or changed local baseline (retry).
u8 SoulLink_StepApplyJob(struct SoulLinkApplyJob *job);

#endif
