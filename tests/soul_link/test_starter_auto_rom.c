// Reproduce A's tracked starter and B's pre-activation starter through real TCP/Lua.
#define SOUL_LINK_NETWORK_NO_MAIN
#include "test_network_rom.c"
#include "constants/region_map_sections.h"
int main(int argc,char **argv) {
    assert(argc==5);sSymbols=argv[2];mLogSetDefaultLogger(&sLogger);
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


    Gift(1,MAP_PALLET_TOWN_PROFESSOR_OAKS_LAB,SPECIES_CHARMANDER,601);
    CreateRun(0);CreateRun(1);
    Gift(0,MAP_PALLET_TOWN_PROFESSOR_OAKS_LAB,SPECIES_BULBASAUR,501);
    assert(Pair(0,0).state==SOUL_LINK_PAIR_PENDING);
    Select(1);assert(GAME0("SoulLink_GetPairCount")==0);
    for(unsigned p=0;p<2;p++)ScriptStart(p,argv[3+p]);
    normalPacing=true;nextFrameNs=0;bool paired=false;
    for(unsigned f=0;f<2400;f++) {
        Frames(1);Select(0);unsigned a=GAME0("SoulLink_GetPairCount");Select(1);unsigned b=GAME0("SoulLink_GetPairCount");
        if(a==1&&b==1&&Pair(0,0).state==SOUL_LINK_PAIR_ALIVE&&Pair(1,0).state==SOUL_LINK_PAIR_ALIVE){paired=true;break;}
    }
    assert(paired);Frames(180);
    for(unsigned p=0;p<2;p++) {
        struct SoulLinkPair row=Pair(p,0);
        assert(row.areaId==SOUL_LINK_AREA_STARTER&&row.mons[0].species==SPECIES_BULBASAUR&&row.mons[1].species==SPECIES_CHARMANDER);
        Select(p);assert(GAME0("IsOverworldIdle"));
    }
    puts("PASS real automatic starter recovery: A's Bulbasaur and B's pre-activation Charmander paired on both games without restarting");
    // Existing captures made before tracking must not require a third capture.
    for(unsigned p=0;p<2;p++) {
        Select(p);uint32_t party=Symbol("gPlayerParty"),save=sCore->busRead32(sCore,Symbol("gSaveBlock1Ptr"));
        sCore->busWrite16(sCore,save+0x634,sCore->busRead16(sCore,save+0x634)|8);
        GAME("CreateMon",party+100,p?SPECIES_RATTATA:SPECIES_PIDGEY,3,31,1,502+p*100,OT_ID_PRESET,p?20:10);
        uint8_t location=MAPSEC_ROUTE_1;Write(SCRATCH+128,&location,1);
        GAME("SetMonData",party+100,MON_DATA_MET_LOCATION,SCRATCH+128);
        sCore->busWrite8(sCore,Symbol("gPlayerPartyCount"),2);
        GAME("FlagSet",NUZLOCKE_FLAGS_START+7);
    }
    bool capturesPaired=false;
    for(unsigned f=0;f<2400;f++) {
        Frames(1);Select(0);unsigned a=GAME0("SoulLink_GetPairCount");Select(1);unsigned b=GAME0("SoulLink_GetPairCount");
        if(a==2&&b==2&&Pair(0,1).state==SOUL_LINK_PAIR_ALIVE&&Pair(1,1).state==SOUL_LINK_PAIR_ALIVE){capturesPaired=true;break;}
    }
    assert(capturesPaired);
    for(unsigned p=0;p<2;p++) {
        struct SoulLinkPair row=Pair(p,1);
        assert(row.areaId==8&&row.mons[0].species==SPECIES_PIDGEY&&row.mons[1].species==SPECIES_RATTATA);
    }
    puts("PASS real TCP/Lua automatic early-capture recovery: Pidgey/Rattata paired on both games without another catch or a manual sync");
    assert(scriptErrors==0);
    for(unsigned p=0;p<2;p++){mScriptContextTriggerCallback(&contexts[p],"stop");mScriptContextDeinit(&contexts[p]);mCoreConfigDeinit(&games[p]->config);games[p]->deinit(games[p]);}
    return 0;
}
