#ifndef GUARD_SOUL_LINK_LOCAL_H
#define GUARD_SOUL_LINK_LOCAL_H

#include "global.h"

u16 SoulLink_GetCanonicalArea(u8 section, u8 mapGroup, u8 mapNum);
u16 SoulLink_GetCurrentArea(void);
void SoulLink_BeginBattle(void);
void SoulLink_EndBattle(void);
bool8 SoulLink_PrepareAcquisition(struct Pokemon *mon);
void SoulLink_FinishAcquisition(struct Pokemon *mon, u8 result);
void SoulLink_RecordMonDeath(struct Pokemon *mon);
void SoulLink_RecordBattleDeath(u8 partyIndex);
bool8 SoulLink_IsBoxMonDead(struct BoxPokemon *mon);
bool8 SoulLink_IsGraveBox(u8 box);
void SoulLink_ProcessField(void);
void SoulLink_RequestCleanup(void);
void SoulLink_NotifyPairSync(bool8 failed);
void SoulLink_NotifyDeathSync(void);
bool8 SoulLink_TransferNpcTrade(struct Pokemon *oldMon, struct Pokemon *newMon);

#endif
