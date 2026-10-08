// Native starter recovery with a partial partner row, provenance and death checks.
#define SOUL_LINK_CABLE_NO_MAIN
#include "test_cable_rom.c"
#include "soul_link_companion.h"
#include "soul_link_link.h"
#include "constants/region_map_sections.h"
#include "pokedex.h"
int main(int argc,char **argv) {
    assert(argc==3);sSymbols=argv[2];mLogSetDefaultLogger(&sLogger);
    for(unsigned p=0;p<2;p++) {
        games[p]=mCoreFind(argv[1]);assert(games[p]&&games[p]->init(games[p]));
        mCoreInitConfig(games[p],"soul-link-WAN-tests");
        games[p]->setVideoBuffer(games[p],pixels[p],240);
        assert(mCoreLoadFile(games[p],argv[1]));games[p]->reset(games[p]);
        for(unsigned f=0;f<720;f++)games[p]->runFrame(games[p]);
        Select(p);
        sCore->busWrite32(sCore,Symbol("gSaveBlock1Ptr"),Symbol("gSaveBlock1"));
        sCore->busWrite32(sCore,Symbol("gSaveBlock2Ptr"),Symbol("gSaveBlock2"));
        sCore->busWrite32(sCore,Symbol("gPokemonStoragePtr"),Symbol("gPokemonStorage"));
        sCore->busWrite8(sCore,Symbol("gSaveBlock1")+0x3A4C,0xFF);
        sCore->busWrite8(sCore,Symbol("gSaveBlock2"),0xBB+p);
        sCore->busWrite8(sCore,Symbol("gSaveBlock2")+1,0xFF);
        sCore->busWrite32(sCore,Symbol("gMain")+4,Symbol("CB2_NewGame")|1);
        sCore->busWrite8(sCore,Symbol("gMain")+0x438,0);
    }
    Frames(120);
    for(unsigned p=0;p<2;p++) {
        Select(p);scratch[p]=GAME("AllocZeroed",512);assert(scratch[p]);
        GAME("SetTrainerId",p?20:10,sCore->busRead32(sCore,Symbol("gSaveBlock2Ptr"))+0xA);
        GAME("FlagSet",FLAG_SYS_POKEMON_GET);GAME("FlagSet",FLAG_SYS_POKEDEX_GET);
    }



    // Recovered acquisitions used to leave the duplicate-history bits empty.
    Gift(0,MAP_ROUTE1,SPECIES_RATTATA,501);
    CreateRun(0);Select(0);
    uint32_t save=sCore->busRead32(sCore,Symbol("gSaveBlock1Ptr"));
    sCore->busWrite16(sCore,save+0x634,sCore->busRead16(sCore,save+0x634)|8);
    GAME("FlagSet",NUZLOCKE_FLAGS_START+7);
    assert(GAME0("SoulLink_CompanionRecoverEarlyCaptures"));
    assert(GAME("CheckNuzlockeDupeFlags",19));
    // A saved local capture is still a duplicate if its separate history bit
    // is missing, as in a save made with the older recovery implementation.
    sCore->busWrite8(sCore,save+0x353C+2,sCore->busRead8(sCore,save+0x353C+2)&~8);
    assert(!GAME("CheckNuzlockeDupeFlags",19));
    assert(GAME("IsWildMonNuzlockeDupe",SPECIES_RATTATA));
    assert(GAME("IsWildMonNuzlockeDupe",SPECIES_RATICATE));
    assert(!GAME("IsWildMonNuzlockeDupe",SPECIES_MANKEY));
    GAME("GetSetPokedexFlag",21,FLAG_SET_SEEN);
    assert(!GAME("IsWildMonNuzlockeDupe",SPECIES_SPEAROW));
    GAME("GetSetPokedexFlag",21,FLAG_SET_CAUGHT);
    assert(GAME("IsWildMonNuzlockeDupe",SPECIES_SPEAROW));
    GAME("SetWarpDestination",MAP_ROUTE22>>8,MAP_ROUTE22&255,(uint32_t)-1,10,10);
    GAME0("WarpIntoMap");GAME("SetMainCallback2",Symbol("CB2_LoadMap")|1);Frames(120);Select(0);
    assert(GAME0("IsOverworldIdle"));
    GAME("CreateMon",Symbol("gEnemyParty"),SPECIES_RATTATA,3,31,1,601,OT_ID_PRESET,10);
    GAME0("StartWildBattle");Frames(180);Select(0);
    assert(sCore->busRead8(sCore,Symbol("gMain")+0x439));
    assert(!GAME("SoulLink_PrepareAcquisition",Symbol("gEnemyParty")));
    uint16_t zero=0;Write(SCRATCH+128,&zero,2);
    GAME("SetMonData",Symbol("gEnemyParty"),MON_DATA_HP,SCRATCH+128);
    sCore->busWrite8(sCore,Symbol("gBattleOutcome"),1);
    GAME0("ReturnFromBattleToOverworld");Frames(180);Select(0);
    assert(GAME0("SoulLink_GetPairCount")==1);
    assert(!GAME("FlagGet",NUZLOCKE_FLAGS_START+28));
    assert(GAME0("IsOverworldIdle"));
    GAME("CreateMon",Symbol("gEnemyParty"),SPECIES_MANKEY,3,31,1,602,OT_ID_PRESET,10);
    GAME0("StartWildBattle");Frames(180);Select(0);
    assert(GAME("SoulLink_PrepareAcquisition",Symbol("gEnemyParty")));
    assert(GAME("GiveMonToPlayer",Symbol("gEnemyParty"))==MON_GIVEN_TO_PARTY);
    sCore->busWrite8(sCore,Symbol("gBattleOutcome"),7);
    GAME0("ReturnFromBattleToOverworld");Frames(180);Select(0);
    assert(GAME0("SoulLink_GetPairCount")==2);
    struct SoulLinkPair row=Pair(0,1);
    assert(row.areaId==29&&row.encounterStates[0]==SOUL_LINK_ENCOUNTER_CAPTURED&&row.mons[0].species==SPECIES_MANKEY);
    puts("PASS native real battle: recovered Rattata/Raticate duplicate blocked, duplicate KO preserves Route 22, following Mankey capture succeeds");
    Write(SCRATCH+128,&row.mons[0],sizeof(row.mons[0]));assert(GAME("SoulLink_MarkLocalDeath",SCRATCH+128));
    save=sCore->busRead32(sCore,Symbol("gSaveBlock1Ptr"));
    sCore->busWrite8(sCore,save+0x353C+7,sCore->busRead8(sCore,save+0x353C+7)&~1);
    assert(!GAME("CheckNuzlockeDupeFlags",56));
    assert(GAME("IsWildMonNuzlockeDupe",SPECIES_MANKEY));
    struct SoulLinkConfig config;
    assert(GAME("SoulLink_GetConfig",SCRATCH+128));Read(SCRATCH+128,&config,sizeof(config));
    config.dupesClause=0;Write(SCRATCH+128,&config,sizeof(config));assert(GAME("SoulLink_SetConfig",SCRATCH+128));
    assert(!GAME("IsWildMonNuzlockeDupe",SPECIES_RATTATA));
    puts("PASS native duplicate history: seen-only excluded, caught-before-link included, dead captures retained, clause toggle respected");
    for(unsigned p=0;p<2;p++){mCoreConfigDeinit(&games[p]->config);games[p]->deinit(games[p]);}
    return 0;
}
