// Real automatic desktop coordination, not calls to the manual Soul Link menu.
#define SOUL_LINK_NETWORK_NO_MAIN
#include "test_network_rom.c"
#include "soul_link_companion.h"
static void Request(unsigned role,const char *json) {
    char name[128];snprintf(name,sizeof(name),"build/tests/desktop-command-%c.request",'a'+role);
    FILE *file=fopen(name,"wb");assert(file);fputs(json,file);fclose(file);
}
static void AwaitAlive(unsigned row) {
    for(unsigned f=0;f<2400;f++) {
        Frames(1);
        Select(0);unsigned count=GAME0("SoulLink_GetPairCount");
        Select(1);unsigned other=GAME0("SoulLink_GetPairCount");
        if(count>row&&other>row&&Pair(0,row).state==SOUL_LINK_PAIR_ALIVE&&Pair(1,row).state==SOUL_LINK_PAIR_ALIVE)return;
    }
    assert(!"Automatic pair sync timed out");
}
static void RejectInvalidBackgroundViews(void) {
    struct SoulLinkCompanionView view, original;
    Select(0);uint32_t control=sCore->busRead32(sCore,Symbol("gSoulLinkCompanionControl"));assert(control);
    Read(control+44,&original,sizeof(original));
    Select(1);uint32_t buffer=GAME("AllocZeroed",sizeof(view));assert(buffer);
    struct SoulLinkPair before=Pair(1,0), after;
    for(unsigned bad=0;bad<5;bad++) {
        view=original;
        if(bad==0)view.run.words[0]^=1;
        if(bad==1)view.player=1;
        if(bad==2)view.pairDigest^=1;
        if(bad==3)view.pairCount=89;
        if(bad==4)view.participants[0]^=1;
        Write(buffer,&view,sizeof(view));assert(!GAME("SoulLink_CompanionApplyView",buffer));
        assert(!GAME("SoulLink_CompanionStartApplyView",buffer));
        assert(GAME0("SoulLink_CompanionStepApplyView")==2);
        after=Pair(1,0);assert(!memcmp(&before,&after,sizeof(before)));
    }
    GAME("Free",buffer);
    puts("PASS native background validation rejects wrong run, wrong role, bad digest, wrong participant and oversized registry without modifying the save");fflush(stdout);
}
static bool IsChosenParty(unsigned role) {
    Select(role);
    uint32_t header=sCore->busRead32(sCore,Symbol("gSaveBlock2Ptr"))+0xB20;
    return sCore->busRead8(sCore,header+offsetof(struct SoulLinkSaveHeader,team)+offsetof(struct SoulLinkTeamSelection,locked))
        && sCore->busRead32(sCore,Symbol("gPlayerParty"))==(role?602:502)
        && sCore->busRead8(sCore,Symbol("gPlayerPartyCount"))==1;
}
static void RecoverInterruptedTeam(void) {
    unsigned first=2;
    unsigned preferred=getenv("SOUL_LINK_PARTIAL_FIRST")?atoi(getenv("SOUL_LINK_PARTIAL_FIRST")):0;
    assert(preferred<2);
    // Observe each emulator run-loop boundary, rather than running both games
    // through the entire next frame. Stop the second game immediately once
    // the first native party write and its final lock flag are visible.
    for(unsigned n=0;n<2000000&&first==2;n++) {
        for(unsigned order=0;order<2;order++) {
            unsigned role=preferred^order;if(!awake[role])continue;
            games[role]->runLoop(games[role]);
            if(IsChosenParty(role)) {
                first=role;break;
            }
        }
    }
    assert(first==preferred);unsigned paused=first^1;
    assert(!IsChosenParty(paused));
    printf("PASS actual one-sided native commit: player %c changed party; player %c has not committed\n",'A'+first,'A'+paused);fflush(stdout);
    FILE *request=fopen("build/tests/desktop-restart.request","wb");assert(request);fclose(request);
    bool restarted=false;
    for(unsigned wait=0;wait<2000;wait++) {
        games[first]->runFrame(games[first]);usleep(10000);
        if(access("build/tests/desktop-restart.done",F_OK)==0){restarted=true;break;}
    }
    assert(restarted);assert(!IsChosenParty(paused));
    bool applied=false;
    for(unsigned frame=0;frame<3600;frame++) {
        Frames(1);
        if(IsChosenParty(0)&&IsChosenParty(1)){applied=true;break;}
    }
    assert(applied);Frames(180);
    assert(access("build/tests/desktop-partial-observed.done",F_OK)==0);
    for(unsigned role=0;role<2;role++) {
        Select(role);assert(GAME0("IsOverworldIdle"));
        assert(!GAME0("ArePlayerFieldControlsLocked"));
        assert(!sCore->busRead32(sCore,Symbol("gSoulLinkRemoteMailbox")));
    }
    puts("PASS native party recovery after both companions restart; saved approval reused without another manual vote");fflush(stdout);
}
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

    for(unsigned p=0;p<2;p++)ScriptStart(p,argv[3+p]);
    normalPacing=true;nextFrameNs=0;
    bool ready=false;
    for(unsigned f=0;f<2400;f++) {
        Frames(1);Select(0);bool a=GAME("SoulLink_GetRunId",SCRATCH+64);Select(1);bool b=GAME("SoulLink_GetRunId",SCRATCH+64);
        if(a&&b&&!games[0]->busRead32(games[0],Symbol("gSoulLinkRemoteMailbox"))&&!games[1]->busRead32(games[1],Symbol("gSoulLinkRemoteMailbox"))){ready=true;break;}
    }
    assert(ready);Frames(120);
    puts("PASS automatic role/run initialization and native synchronization");fflush(stdout);
    Gift(0,MAP_ROUTE1,SPECIES_BULBASAUR,501);Gift(1,MAP_ROUTE1,SPECIES_RATTATA,601);
    AwaitAlive(0);Frames(180);RejectInvalidBackgroundViews();
    if(getenv("SOUL_LINK_PARTIAL_TEAM_TEST")) {
        Gift(0,MAP_ROUTE2,SPECIES_CHARMANDER,502);Gift(1,MAP_ROUTE2,SPECIES_SQUIRTLE,602);
        AwaitAlive(1);Frames(120);
        goto propose_team;
    }
    OpenMenu(1);Gift(0,MAP_ROUTE2,SPECIES_CHARMANDER,502);
    Request(0,"{\"action\":\"sync\"}");Frames(600);
    Select(0);assert(GAME0("SoulLink_GetPairCount")==2);
    assert(Pair(0,1).state==SOUL_LINK_PAIR_PENDING);
    assert(!games[0]->busRead32(games[0],Symbol("gSoulLinkRemoteMailbox")));

    Select(0);assert(GAME0("IsOverworldIdle"));assert(!GAME0("ArePlayerFieldControlsLocked"));
    assert(!games[0]->busRead32(games[0],Symbol("gSoulLinkRemoteMailbox")));
    puts("PASS busy partner receives background readings while local capture is registered without a WAN movement lock");fflush(stdout);
    Press(1,2);Frames(100);Press(1,2);Frames(100);
    Gift(1,MAP_ROUTE2,SPECIES_GYARADOS,602);AwaitAlive(1);Frames(180);
    OpenMenu(0);Gift(1,MAP_ROUTE3,SPECIES_MANKEY,603);Frames(600);
    Select(1);assert(GAME0("IsOverworldIdle"));assert(!GAME0("ArePlayerFieldControlsLocked"));
    assert(GAME0("SoulLink_GetPairCount")==3);
    assert(Pair(1,2).mons[1].personality==603);
    assert(!games[1]->busRead32(games[1],Symbol("gSoulLinkRemoteMailbox")));
    puts("PASS follower capture survives older coordinator readings while A is busy");fflush(stdout);
    Press(0,2);Frames(100);Press(0,2);Frames(100);
    Gift(0,MAP_ROUTE3,SPECIES_PIDGEY,503);AwaitAlive(2);Frames(120);
propose_team:
    Request(1,"{\"action\":\"propose\",\"ids\":[2]}");
    FILE *flag=fopen("build/tests/desktop-accept.request","wb");assert(flag);fclose(flag);
    if(getenv("SOUL_LINK_PARTIAL_TEAM_TEST")) {
        RecoverInterruptedTeam();
        assert(scriptErrors==0);
        for(unsigned role=0;role<2;role++){mScriptContextTriggerCallback(&contexts[role],"stop");mScriptContextDeinit(&contexts[role]);mCoreConfigDeinit(&games[role]->config);games[role]->deinit(games[role]);}
        return 0;
    }
    bool applied=false;
    for(unsigned f=0;f<2400;f++) {
        Frames(1);Select(0);bool a=GAME0("SoulLink_ArePartyChangesLocked");Select(1);bool b=GAME0("SoulLink_ArePartyChangesLocked");
        if(a&&b){applied=true;break;}
    }
    assert(applied);Frames(120);
    for(unsigned p=0;p<2;p++){Select(p);assert(GAME("GetMonData",Symbol("gPlayerParty"),MON_DATA_PERSONALITY,0)==(p?602:502));}
    puts("PASS player B proposal, player A acceptance, native team application on both games");fflush(stdout);
    Select(0);uint16_t hp=0;Write(SCRATCH+128,&hp,2);
    GAME("SetMonData",Symbol("gPlayerParty"),MON_DATA_HP,SCRATCH+128);
    GAME("SoulLink_RecordMonDeath",Symbol("gPlayerParty"));
    GAME("ScriptContext_SetupScript",Symbol("EventScript_NuzlockeGameOver"));
    // Hold the actual last-Pokémon defeat dialogue open. A full native exchange
    // cannot start here; the separate death notification must reach B anyway.
    Frames(160);bool dead=false;
    for(unsigned f=0;f<2400;f++){Frames(1);if(Pair(1,1).state==SOUL_LINK_PAIR_DEAD){dead=true;break;}}
    assert(dead);Frames(180);Dismiss(1);Select(1);
    assert(GAME0("IsOverworldIdle"));assert(!GAME0("ArePlayerFieldControlsLocked"));
    assert(GAME("GetMonData",Symbol("gPlayerParty"),MON_DATA_PERSONALITY,0)==602);
    assert(GAME("GetMonData",Symbol("gPlayerParty"),MON_DATA_HP,0)==0);
    assert(GAME("SoulLink_IsBoxMonDead",Symbol("gPlayerParty")));
    // The last dead member stays in party to avoid an empty party, but even
    // healing is refused. Storage cleanup waits for another usable Pokémon.
    uint16_t healed=99;Write(SCRATCH+64,&healed,2);
    GAME("SetMonData",Symbol("gPlayerParty"),MON_DATA_HP,SCRATCH+64);
    assert(GAME("GetMonData",Symbol("gPlayerParty"),MON_DATA_HP,0)==0);
    puts("PASS linked death while sender remains in the actual Nuzlocke game-over dialogue");fflush(stdout);
    Select(0);GAME0("TurnOffNuzlockeMode");Frames(120);
    assert(Pair(0,1).state==SOUL_LINK_PAIR_DEAD && Pair(1,1).state==SOUL_LINK_PAIR_DEAD);
    puts("PASS disabling standalone Nuzlocke cannot resurrect either linked Pokémon");fflush(stdout);
    games[0]->reset(games[0]);Frames(180);
    assert(Pair(1,1).state==SOUL_LINK_PAIR_DEAD);
    puts("PASS sender restarting leaves partner death intact");fflush(stdout);
    assert(scriptErrors==0);
    for(unsigned p=0;p<2;p++){mScriptContextTriggerCallback(&contexts[p],"stop");mScriptContextDeinit(&contexts[p]);mCoreConfigDeinit(&games[p]->config);games[p]->deinit(games[p]);}
    return 0;
}
