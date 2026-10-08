#define main ExistingAuditMain
#include "test_rom.c"
#undef main
#include "constants/flags.h"
#include "constants/region_map_sections.h"
static void Setup(void) {
 sCore->busWrite32(sCore,Symbol("gSaveBlock1Ptr"),Symbol("gSaveBlock1"));
 sCore->busWrite32(sCore,Symbol("gSaveBlock2Ptr"),Symbol("gSaveBlock2"));
 sCore->busWrite32(sCore,Symbol("gPokemonStoragePtr"),Symbol("gPokemonStorage"));
 CALL0("ResetPokemonStorageSystem"); CALL0("ZeroPlayerPartyMons"); CALL0("SoulLink_ResetSaveData");
 struct SoulLinkRunId run={{123,456}}; uint32_t players[2]={10,20};
 Write(SCRATCH,&run,sizeof(run));Write(SCRATCH+16,players,sizeof(players));
 assert(CALL("SoulLink_BeginRun",SCRATCH,SCRATCH+16,0));
 struct SoulLinkConfig config={1,1,1,1,1,0,0,0};Write(SCRATCH+32,&config,sizeof(config));assert(CALL("SoulLink_SetConfig",SCRATCH+32));
 CALL("SetTrainerId",10,Symbol("gSaveBlock2")+0xA);
 CALL("FlagSet",FLAG_SYS_POKEDEX_GET);
 sCore->busWrite8(sCore,Symbol("gSaveBlock1")+4,3); sCore->busWrite8(sCore,Symbol("gSaveBlock1")+5,20);
 sCore->busWrite8(sCore,Symbol("gMain")+0x439,0);
}
int main(int argc,char **argv) {
 assert(argc==3);sSymbols=argv[2];mLogSetDefaultLogger(&sLogger);
 sCore=mCoreFind(argv[1]);assert(sCore&&sCore->init(sCore));mCoreInitConfig(sCore,"soul-link-readonly-audit");
 sCore->setVideoBuffer(sCore,sPixels,240);assert(mCoreLoadFile(sCore,argv[1]));sCore->reset(sCore);
 for(unsigned f=0;f<720;f++)sCore->runFrame(sCore);
 CALL("InitHeap",Symbol("gHeap"),0x1C000);sScratch=CALL("AllocZeroed",2048);assert(sScratch);
 Setup();
 uint32_t party=Symbol("gPlayerParty"), sb1=Symbol("gSaveBlock1");
 sCore->busWrite16(sCore,sb1+0x634,sCore->busRead16(sCore,sb1+0x634)|8);
 CALL("CreateEgg",SCRATCH+256,SPECIES_BULBASAUR,0);
 assert(CALL("GiveMonToPlayer",SCRATCH+256)==MON_GIVEN_TO_PARTY);
 assert(CALL0("SoulLink_GetPairCount")==1);
 printf("egg received: hp=%u, area used=%u, metloc=%u\n",CALL("GetMonData",party,MON_DATA_HP,0),CALL("NuzlockeFlagGet",MAPSEC_ROUTE_2),CALL("GetMonData",party,MON_DATA_MET_LOCATION,0));
 CALL("AddHatchedMonToParty",0);
 printf("egg hatched: hp=%u, isEgg=%u\n",CALL("GetMonData",party,MON_DATA_HP,0),CALL("GetMonData",party,MON_DATA_IS_EGG,0));
 assert(CALL("GetMonData",party,MON_DATA_HP,0)>0);
 CALL0("SoulLink_ProcessField");
 struct SoulLinkPair row;assert(CALL("SoulLink_ReadPair",0,SCRATCH+512));Read(SCRATCH+512,&row,sizeof(row));
 printf("field cleanup: local state=%u (3=DEAD), pair state=%u\n",row.encounterStates[0],row.state);
 assert(row.encounterStates[0]==SOUL_LINK_ENCOUNTER_CAPTURED);
 puts("PASS legal recorded egg survives hatching and remains captured");
 Setup();
 sCore->busWrite16(sCore,sb1+0x634,0);CALL("FlagClear",NUZLOCKE_FLAGS_START+8);
 CALL("CreateMon",party,SPECIES_NINJASK,20,31,1,101,OT_ID_PRESET,10);CALL0("CalculatePlayerPartyCount");
 struct SoulLinkMonIdentity identity={10,101,SPECIES_NINJASK,0};Write(SCRATCH+64,&identity,sizeof(identity));assert(CALL("SoulLink_RecordLocalEncounter",9,SCRATCH+64,SOUL_LINK_ENCOUNTER_CAPTURED));
 CALL("CreateShedinja",SPECIES_NINCADA,party);
 assert(CALL("GetMonData",party+100,MON_DATA_SPECIES,0)==SPECIES_NONE);
 puts("PASS Soul Link evolution creates no unpaired bonus");
 struct SoulLinkConfig off={0,1,1,1,1,0,0,0};Write(SCRATCH+32,&off,sizeof(off));assert(CALL("SoulLink_SetConfig",SCRATCH+32));
 CALL("CreateShedinja",SPECIES_NINCADA,party);
 assert(CALL("GetMonData",party+100,MON_DATA_SPECIES,0)==SPECIES_SHEDINJA);
 puts("PASS standalone evolution bonus remains available");
 off.enabled=1;Write(SCRATCH+32,&off,sizeof(off));assert(CALL("SoulLink_SetConfig",SCRATCH+32));
 uint16_t zero=0;Write(SCRATCH+96,&zero,2);CALL("SetMonData",party+100,MON_DATA_HP,SCRATCH+96);
 CALL("SoulLink_RecordMonDeath",party+100);CALL0("HealPlayerPartyOak");
 printf("Shedinja after faint+heal: hp=%u, isDead=%u\n",CALL("GetMonData",party+100,MON_DATA_HP,0),CALL("SoulLink_IsBoxMonDead",party+100));
 assert(CALL("GetMonData",party+100,MON_DATA_HP,0)==0);
 assert(CALL("SoulLink_IsBoxMonDead",party+100));
 assert(CALL("GetMonData",party,MON_DATA_HP,0)>0);
 puts("PASS old bonus is unusable without killing its parent");
  assert(CALL("SoulLink_ReadPair",0,SCRATCH+512));Read(SCRATCH+512,&row,sizeof(row));
 row.mons[1]=(struct SoulLinkMonIdentity){20,201,SPECIES_SQUIRTLE,1};row.encounterStates[1]=SOUL_LINK_ENCOUNTER_CAPTURED;row.state=SOUL_LINK_PAIR_ALIVE;
 Write(SCRATCH+512,&row,sizeof(row));assert(CALL("SoulLink_StorePair",SCRATCH+512));
 uint32_t plan=CALL("AllocZeroed",1600);assert(plan);uint16_t id=1;Write(SCRATCH+96,&id,2);
 unsigned ready=CALL("SoulLink_PrepareTeamChange",plan,SCRATCH+96,1);CALL("SoulLink_DescribeTeamChange",plan,SCRATCH+1024);
 struct SoulLinkTeamDiagnostic diag;Read(SCRATCH+1024,&diag,sizeof(diag));
 printf("Ninjask team preflight with generated Shedinja: ready=%u, reason=%u, pair=%u\n",ready,diag.reason,diag.pairId);
 assert(ready);
 CALL("ZeroMonData",party+100);CALL0("CalculatePlayerPartyCount");
 assert(CALL("SoulLink_PrepareTeamChange",plan,SCRATCH+96,1));
  puts("PASS old bonus does not block Ninjask team selection");
 CALL("CopyMon",party+100,party,100);CALL0("CalculatePlayerPartyCount");
 assert(!CALL("SoulLink_PrepareTeamChange",plan,SCRATCH+96,1));CALL("SoulLink_DescribeTeamChange",plan,SCRATCH+1024);Read(SCRATCH+1024,&diag,sizeof(diag));
 assert(diag.reason==SOUL_LINK_TEAM_DUPLICATE);puts("PASS actual duplicate parent is still refused");
 CALL("CreateMon",party+100,SPECIES_SHEDINJA,20,31,1,102,OT_ID_PRESET,10);CALL0("CalculatePlayerPartyCount");
 assert(!CALL("SoulLink_IsBoxMonDead",party+100));puts("PASS unrelated Shedinja is not treated as an old clone");
 Setup();off.enabled=0;Write(SCRATCH+32,&off,sizeof(off));assert(CALL("SoulLink_SetConfig",SCRATCH+32));
 sCore->busWrite16(sCore,sb1+0x634,sCore->busRead16(sCore,sb1+0x634)|8);
 CALL("CreateEgg",SCRATCH+256,SPECIES_BULBASAUR,0);assert(CALL("GiveMonToPlayer",SCRATCH+256)==MON_GIVEN_TO_PARTY);
 CALL("NuzlockeFlagSet",MAPSEC_ROUTE_2);CALL("AddHatchedMonToParty",0);
 assert(CALL("GetMonData",party,MON_DATA_HP,0)==0);puts("PASS standalone Nuzlocke quota still rejects a hatch in a consumed area");
 mCoreConfigDeinit(&sCore->config);sCore->deinit(sCore);return 0;
}
