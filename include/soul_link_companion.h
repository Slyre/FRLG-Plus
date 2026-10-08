#ifndef GUARD_SOUL_LINK_COMPANION_H
#define GUARD_SOUL_LINK_COMPANION_H
#include "soul_link_types.h"
#define SOUL_LINK_COMPANION_MAGIC 0x31434C53
#define SOUL_LINK_COMMAND_SYNC 1
#define SOUL_LINK_COMMAND_INIT 2
#define SOUL_LINK_COMMAND_TEAM 3
#define SOUL_LINK_COMMAND_DEATHS 4
#define SOUL_LINK_COMMAND_APPLY_VIEW 5
struct SoulLinkCompanionMon { u16 species, pairId, hp, maxHp; u8 level; u8 reserved[3]; };
struct SoulLinkCompanionView
{
    u32 magic, generation;
    u8 saveStatus, safe, inBattle, enabled, sessionStatus, player, graveBox, pairCount;
    struct SoulLinkRunId run;
    u32 participants[2];
    SoulLinkPairId team[6];
    u8 teamCount, locked; u16 reserved;
    struct SoulLinkConfig config;
    u32 configRevision, pairDigest;
    struct SoulLinkCompanionMon party[6];
    struct SoulLinkPair pairs[SOUL_LINK_PAIR_CAPACITY];
};
struct SoulLinkCompanionControl
{
    u32 magic, commandId, appliedId;
    u8 command, result, role, count;
    struct SoulLinkRunId run;
    u32 participants[2];
    SoulLinkPairId team[6];
    struct SoulLinkCompanionView view;
    struct SoulLinkCompanionView remoteView;
    u32 remoteCommandId;
};
extern struct SoulLinkCompanionControl *gSoulLinkCompanionControl;
bool8 SoulLink_CompanionStartApplyView(const struct SoulLinkCompanionView *remote);
u8 SoulLink_CompanionStepApplyView(void);
bool8 SoulLink_CompanionApplyView(const struct SoulLinkCompanionView *remote);
void SoulLink_CompanionUpdate(void);
void SoulLink_CompanionReset(void);
bool8 SoulLink_CompanionRecoverStarter(void);
bool8 SoulLink_CompanionRecoverEarlyCaptures(void);
#endif
