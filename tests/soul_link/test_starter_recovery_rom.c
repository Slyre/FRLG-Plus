// Native starter recovery with a partial partner row, provenance and death checks.
#define SOUL_LINK_CABLE_NO_MAIN
#include "test_cable_rom.c"
#include "soul_link_companion.h"
#include "soul_link_link.h"
#include "constants/region_map_sections.h"
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


    // B received a starter before enabling Soul Link, as in the reported game.
    Gift(1,MAP_PALLET_TOWN_PROFESSOR_OAKS_LAB,SPECIES_CHARMANDER,601);
    CreateRun(0);CreateRun(1);
    Gift(0,MAP_PALLET_TOWN_PROFESSOR_OAKS_LAB,SPECIES_BULBASAUR,501);
    struct SoulLinkPair row=Pair(0,0);assert(row.areaId==SOUL_LINK_AREA_STARTER);
    Select(1);Write(SCRATCH+128,&row,sizeof(row));assert(GAME("SoulLink_StorePair",SCRATCH+128));
    assert(GAME0("SoulLink_GetPairCount")==1);assert(Pair(1,0).encounterStates[1]==SOUL_LINK_ENCOUNTER_NONE);
    Select(1);uint8_t location=MAPSEC_ROUTE_1;Write(SCRATCH+128,&location,1);
    GAME("SetMonData",Symbol("gPlayerParty"),MON_DATA_MET_LOCATION,SCRATCH+128);
    assert(!GAME0("SoulLink_CompanionRecoverStarter"));
    location=MAPSEC_PALLET_TOWN;Write(SCRATCH+128,&location,1);
    GAME("SetMonData",Symbol("gPlayerParty"),MON_DATA_MET_LOCATION,SCRATCH+128);
    assert(GAME0("SoulLink_CompanionRecoverStarter"));
    row=Pair(1,0);assert(row.state==SOUL_LINK_PAIR_ALIVE&&row.mons[0].species==SPECIES_BULBASAUR&&row.mons[1].species==SPECIES_CHARMANDER);
    Select(1);assert(!GAME0("SoulLink_CompanionRecoverStarter"));assert(GAME0("SoulLink_GetPairCount")==1);
    Write(SCRATCH+128,&row.mons[1],sizeof(row.mons[1]));assert(GAME("SoulLink_MarkLocalDeath",SCRATCH+128));
    assert(!GAME0("SoulLink_CompanionRecoverStarter"));assert(Pair(1,0).state==SOUL_LINK_PAIR_DEAD);
    puts("PASS native starter: pre-activation acquisition, partial A row, provenance refusal, idempotence and no revival of a dead pair");
    // An owned first Route 1 capture already present before activation.
    Select(1);
    uint32_t party=Symbol("gPlayerParty"), save=sCore->busRead32(sCore,Symbol("gSaveBlock1Ptr"));
    sCore->busWrite16(sCore,save+0x634,sCore->busRead16(sCore,save+0x634)|8);
    GAME("CreateMon",party+100,SPECIES_PIDGEY,3,31,1,602,OT_ID_PRESET,20);
    location=MAPSEC_ROUTE_1;Write(SCRATCH+128,&location,1);
    GAME("SetMonData",party+100,MON_DATA_MET_LOCATION,SCRATCH+128);
    sCore->busWrite8(sCore,Symbol("gPlayerPartyCount"),2);
    assert(!GAME0("SoulLink_CompanionRecoverEarlyCaptures")); // No consumed-area evidence.
    GAME("FlagSet",NUZLOCKE_FLAGS_START+7);
    // A second owned mon from the same area makes reconstruction ambiguous.
    GAME("CreateMon",party+200,SPECIES_RATTATA,3,31,1,603,OT_ID_PRESET,20);
    GAME("SetMonData",party+200,MON_DATA_MET_LOCATION,SCRATCH+128);
    assert(!GAME0("SoulLink_CompanionRecoverEarlyCaptures"));
    GAME("ZeroMonData",party+200);
    sCore->busWrite32(sCore,Symbol("gSoulLinkRemoteEnabled"),SOUL_LINK_REMOTE_MAGIC);
    for(unsigned n=0;n<30;n++) GAME0("SoulLink_CompanionUpdate");
    assert(GAME0("SoulLink_GetPairCount")==2);row=Pair(1,1);
    assert(row.areaId==8&&row.mons[1].species==SPECIES_PIDGEY&&row.state==SOUL_LINK_PAIR_PENDING);
    assert(!GAME0("SoulLink_CompanionRecoverEarlyCaptures"));
    Write(SCRATCH+128,&row.mons[1],sizeof(row.mons[1]));assert(GAME("SoulLink_MarkLocalDeath",SCRATCH+128));
    assert(!GAME0("SoulLink_CompanionRecoverEarlyCaptures"));assert(Pair(1,1).state==SOUL_LINK_PAIR_DEAD);
    puts("PASS native early capture: automatic idle recovery, consumed-area proof, ambiguous refusal, idempotence and no revival");
    for(unsigned p=0;p<2;p++){mCoreConfigDeinit(&games[p]->config);games[p]->deinit(games[p]);}
    return 0;
}
