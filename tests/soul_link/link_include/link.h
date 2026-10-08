#include "global.h"
#define EXCHANGE_COMPLETE 1
struct LinkPlayer { u32 linkType; };
extern struct LinkPlayer gLinkPlayers[5];
extern u16 gLinkType, gBlockRecvBuffer[5][128];
extern u8 gWirelessCommType;
extern u8 gShouldAdvanceLinkState;
void Task_TriggerHandshake(u8);
extern bool8 gReceivedRemoteLinkPlayers;
bool8 IsLinkTaskFinished(void);
void ClearLinkCallback_2(void);
void CloseLink(void);
void ResetBlockReceivedFlags(void);
void SetSuppressLinkErrorMessage(bool8);
void OpenLinkTimed(void);
void ResetLinkPlayerCount(void);
void ResetLinkPlayers(void);
bool8 HasLinkErrorOccurred(void);
u8 GetLinkPlayerCount_2(void);
bool8 IsLinkMaster(void);
void CheckShouldAdvanceLinkState(void);
u8 GetLinkPlayerDataExchangeStatusTimed(int, int);
u8 GetMultiplayerId(void);
bool8 SendBlock(u8, const void *, u16);
u8 GetBlockReceivedStatus(void);
u16 GetCableBlockReceivedSize(u8);
