// Real Thumb tasks, game loops, mGBA Lua/socket API and external TCP companions.
#define SOUL_LINK_CABLE_NO_MAIN
#include "test_cable_rom.c"
#include <mgba/core/scripting.h>
#include <mgba/internal/script/lua.h>
#include <mgba/script/context.h>
#include <unistd.h>
#include <time.h>

static struct mScriptContext contexts[2];
static unsigned scriptErrors;
static bool normalPacing;
static uint64_t nextFrameNs;
static void ScriptLog(struct mLogger *logger,int category,enum mLogLevel level,const char *format,va_list args)
{
    (void)logger;(void)category;
    vfprintf(stderr,format,args);fputc('\n',stderr);
    if(level==mLOG_WARN||level==mLOG_ERROR)scriptErrors++;
}
static struct mLogger scriptLogger={.log=ScriptLog};
static void ScriptFrame(void *context)
{
    mScriptContextTriggerCallback(context,"frame");
    if(normalPacing) {
        if(context==&contexts[1]) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC,&now);
            uint64_t ns=(uint64_t)now.tv_sec*1000000000+now.tv_nsec;
            if(nextFrameNs>ns) {
                struct timespec wait={.tv_sec=0,.tv_nsec=nextFrameNs-ns};
                nanosleep(&wait,NULL);
                ns=nextFrameNs;
            }
            nextFrameNs=ns+16666667; // At most 60 emulator frames per second.
        }
    } else usleep(1000); // Let the real socket processes run; no forwarding here.
}
static void ScriptStart(unsigned p,const char *path)
{
    mScriptContextInit(&contexts[p]);
    struct mScriptEngineContext *lua=mScriptContextRegisterEngine(&contexts[p],mSCRIPT_ENGINE_LUA);
    assert(lua);
    mScriptContextAttachStdlib(&contexts[p]);
    mScriptContextAttachSocket(&contexts[p]);
    mScriptContextAttachLogger(&contexts[p],&scriptLogger);
    mScriptContextAttachCore(&contexts[p],games[p]);
    struct mScriptValue *port=mScriptValueCreateFromUInt(18766+p);
    mScriptContextSetGlobal(&contexts[p],"SOUL_LINK_LOCAL_PORT",port);
    assert(mScriptContextLoadFile(&contexts[p],path));
    if(!lua->run(lua)) { fprintf(stderr,"Lua: %s\n",lua->getError(lua));abort(); }
    struct mCoreCallbacks callbacks={.context=&contexts[p],.videoFrameEnded=ScriptFrame};
    games[p]->addCoreCallbacks(games[p],&callbacks);
}
static void StartRemote(unsigned p)
{
    Select(p);assert(GAME0("IsOverworldIdle"));
    assert(GAME0("SoulLink_StartCableSession"));

}
static bool RemoteSync(void)
{
    StartRemote(0);Frames(180);StartRemote(1);
    uint32_t address=Symbol("sSessionStatus");
    for(unsigned i=0;i<60000;i++) {
        Frames(1);
        if(games[0]->busRead8(games[0],address)>=3&&games[1]->busRead8(games[1],address)>=3)break;
    }
    bool ok=true;
    for(unsigned p=0;p<2;p++) {
        unsigned status=games[p]->busRead8(games[p],address);
        fprintf(stderr,"WAN ROM player %u status %u\n",p,status);
        assert(status>=3);
        ok&=status==SOUL_LINK_SESSION_COMPLETE;
        assert(games[p]->busRead32(games[p],Symbol("gSoulLinkRemoteMailbox"))==0);
        assert(games[p]->busRead16(games[p],Symbol("gLinkType"))==0);
    }
    Frames(100);Dismiss(0);Dismiss(1);Frames(100);Dismiss(0);Dismiss(1);
    return ok;
}

#ifndef SOUL_LINK_NETWORK_NO_MAIN
int main(int argc,char **argv)
{
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
    Frames(120);CreateRun(0);CreateRun(1);
    Gift(0,MAP_ROUTE1,SPECIES_BULBASAUR,501);Gift(1,MAP_ROUTE1,SPECIES_RATTATA,601);
    for(unsigned p=0;p<2;p++)ScriptStart(p,argv[3+p]);
    Frames(240);
    assert(!scriptErrors);
    assert(RemoteSync());
    assert(Pair(0,0).state==SOUL_LINK_PAIR_ALIVE&&Pair(1,0).state==SOUL_LINK_PAIR_ALIVE);
    Gift(0,MAP_ROUTE2,SPECIES_CHARMANDER,502);Gift(1,MAP_ROUTE3,SPECIES_SQUIRTLE,602);
    assert(RemoteSync());
    void *oldB=malloc(games[1]->stateSize(games[1]));assert(oldB&&games[1]->saveState(games[1],oldB));
    Select(0);uint16_t hp=0;Write(SCRATCH+128,&hp,2);
    GAME("SetMonData",Symbol("gPlayerParty"),MON_DATA_HP,SCRATCH+128);
    Frames(160);Dismiss(0);assert(RemoteSync());
    assert(Pair(1,0).state==SOUL_LINK_PAIR_DEAD);
    assert(games[1]->loadState(games[1],oldB));free(oldB);Frames(240);
    assert(Pair(1,0).state==SOUL_LINK_PAIR_ALIVE);assert(RemoteSync());
    assert(Pair(1,0).state==SOUL_LINK_PAIR_DEAD);
    // Quick load during an active attempt aborts it rather than injecting old frames.
    // Cancellation waits below are measured in game frames. With injected
    // wall-clock socket scheduling, always run this section at normal speed
    // rather than turbo, which otherwise turns four seconds into half a second.
    bool previousPacing=normalPacing;
    normalPacing=true;nextFrameNs=0; // Socket cancellation uses wall-clock deadlines even on loopback.
    oldB=malloc(games[1]->stateSize(games[1]));assert(oldB&&games[1]->saveState(games[1],oldB));
    StartRemote(0);StartRemote(1);
    for(unsigned i=0;i<240 && !games[1]->busRead32(games[1],Symbol("gSoulLinkRemoteMailbox"));i++)Frames(1);
    assert(games[1]->busRead32(games[1],Symbol("gSoulLinkRemoteMailbox"))!=0);
    Frames(2); // Let the Lua bridge announce B's active epoch before rollback.
    assert(games[1]->loadState(games[1],oldB));free(oldB);Frames(240);Dismiss(0);Dismiss(1);
    assert(RemoteSync());
    assert(Pair(1,0).state==SOUL_LINK_PAIR_DEAD);
    StartRemote(0);StartRemote(1);
    for(unsigned i=0;i<240 && !games[1]->busRead32(games[1],Symbol("gSoulLinkRemoteMailbox"));i++)Frames(1);
    assert(games[1]->busRead32(games[1],Symbol("gSoulLinkRemoteMailbox"))!=0);
    Frames(2);
    FILE *cut=fopen("build/tests/network-drop.request","wb");assert(cut);fclose(cut);
    for(unsigned i=0;i<3000;i++) {
        Frames(1);
        if(games[0]->busRead8(games[0],Symbol("sSessionStatus"))==SOUL_LINK_SESSION_DISCONNECTED
         &&games[1]->busRead8(games[1],Symbol("sSessionStatus"))==SOUL_LINK_SESSION_DISCONNECTED)break;
    }
    for(unsigned p=0;p<2;p++)assert(games[p]->busRead8(games[p],Symbol("sSessionStatus"))==SOUL_LINK_SESSION_DISCONNECTED);
    Frames(120);Dismiss(0);Dismiss(1);Frames(500);
    assert(RemoteSync());
    assert(Pair(1,0).state==SOUL_LINK_PAIR_DEAD);
    normalPacing=previousPacing;nextFrameNs=0;
    puts("WAN socket cut: both native attempts aborted, automatic channel reconnect and fresh death reconciliation passed");
    // Existing native team menu/rules run through the same WAN transport.
    Gift(1,MAP_ROUTE2,SPECIES_GYARADOS,603);Gift(0,MAP_ROUTE3,SPECIES_VULPIX,503);
    assert(RemoteSync());EnableTeamRules(0);EnableTeamRules(1);ChooseTeam(1,false);
    assert(RemoteSync());
    for(unsigned p=0;p<2;p++) {
        Select(p);assert(GAME0("SoulLink_ArePartyChangesLocked"));
        assert(GAME("GetMonData",Symbol("gPlayerParty"),MON_DATA_PERSONALITY,0)==(p?602:503));
    }
    ChooseTeam(1,true);Select(1);uint16_t mail=ITEM_ORANGE_MAIL;Write(SCRATCH+64,&mail,2);
    GAME("SetMonData",Symbol("gPlayerParty"),MON_DATA_HELD_ITEM,SCRATCH+64);
    assert(!RemoteSync());
    for(unsigned p=0;p<2;p++) {
        Select(p);assert(GAME("SoulLink_GetTeamDiagnostic",SCRATCH+64,SCRATCH+80));
        struct SoulLinkTeamDiagnostic diagnostic;Read(SCRATCH+64,&diagnostic,sizeof(diagnostic));
        assert(diagnostic.reason==SOUL_LINK_TEAM_MAIL&&sCore->busRead8(sCore,SCRATCH+80)==1);
    }
    Select(1);mail=0;Write(SCRATCH+64,&mail,2);
    GAME("SetMonData",Symbol("gPlayerParty"),MON_DATA_HELD_ITEM,SCRATCH+64);
    assert(RemoteSync());
    for(unsigned p=0;p<2;p++) {
        Select(p);assert(GAME("GetMonData",Symbol("gPlayerParty"),MON_DATA_PERSONALITY,0)==(p?603:502));
        Shot(p,p ? "/mnt/c/Pokemon Soul Link/FRLG-Plus/build/tests/stage13-internet-b.png" : "/mnt/c/Pokemon Soul Link/FRLG-Plus/build/tests/stage13-internet-a.png");
    }
    assert(scriptErrors==0);
    // Full-capacity metadata snapshot: exercise the actual ROM packet stream,
    // not just a synthetic TCP stream. Fixtures do not create 85 boxed bodies.
    for(unsigned p=0;p<2;p++) {
        Select(p);struct SoulLinkConfig config;
        uint32_t header=sCore->busRead32(sCore,Symbol("gSaveBlock2Ptr"))+0xB20;
        Read(header+offsetof(struct SoulLinkSaveHeader,config),&config,sizeof(config));
        config.enforcePartyPairs=FALSE;Write(SCRATCH+64,&config,sizeof(config));
        assert(GAME("SoulLink_SetConfig",SCRATCH+64));
        for(unsigned i=0;i<85;i++) {
            struct SoulLinkMonIdentity identity={p?20:10,2000+p*1000+i,SPECIES_BULBASAUR,0};
            Write(SCRATCH+128,&identity,sizeof(identity));
            assert(GAME("SoulLink_RecordLocalEncounter",500+i,SCRATCH+128,SOUL_LINK_ENCOUNTER_CAPTURED));
        }
        assert(GAME0("SoulLink_GetPairCount")==SOUL_LINK_PAIR_CAPACITY);
    }
    assert(RemoteSync());
    for(unsigned p=0;p<2;p++) {
        Select(p);assert(GAME0("SoulLink_GetPairCount")==88);
        for(unsigned i=3;i<88;i++)assert(Pair(p,i).state==SOUL_LINK_PAIR_ALIVE);
    }
    // Force the complete fallback, then cut while its row batch is being transmitted.
    for(unsigned p=0;p<2;p++) { Select(p); GAME("SoulLink_ProtocolForgetBaseline",p); }
    StartRemote(0);StartRemote(1);
    bool batchStarted=false;
    for(unsigned i=0;i<3000;i++) {
        Frames(1);
        uint32_t mailbox=games[1]->busRead32(games[1],Symbol("gSoulLinkRemoteMailbox"));
        if(mailbox && games[1]->busRead8(games[1],mailbox+8)
           && games[1]->busRead16(games[1],mailbox+140)>1) {batchStarted=true;break;}
    }
    assert(batchStarted);
    cut=fopen("build/tests/network-drop.request","wb");assert(cut);fclose(cut);
    for(unsigned i=0;i<3000;i++) {
        Frames(1);
        if(games[0]->busRead8(games[0],Symbol("sSessionStatus"))==SOUL_LINK_SESSION_DISCONNECTED
         &&games[1]->busRead8(games[1],Symbol("sSessionStatus"))==SOUL_LINK_SESSION_DISCONNECTED)break;
    }
    for(unsigned p=0;p<2;p++) {
        assert(games[p]->busRead8(games[p],Symbol("sSessionStatus"))==SOUL_LINK_SESSION_DISCONNECTED);
        assert(games[p]->busRead32(games[p],Symbol("gSoulLinkRemoteMailbox"))==0);
        Select(p);assert(Pair(p,0).state==SOUL_LINK_PAIR_DEAD);
    }
    Frames(120);Dismiss(0);Dismiss(1);Frames(500);
    normalPacing=true;nextFrameNs=0;
    fprintf(stderr,"Starting full-capacity recovery benchmark at at most 60 fps\n");
    assert(RemoteSync());
    normalPacing=false;
    for(unsigned p=0;p<2;p++) {
        Select(p);assert(GAME0("SoulLink_GetPairCount")==88);
        assert(Pair(p,0).state==SOUL_LINK_PAIR_DEAD);
        for(unsigned i=3;i<88;i++)assert(Pair(p,i).state==SOUL_LINK_PAIR_ALIVE);
    }
    puts("WAN full-capacity batch cut and fresh 88-row recovery passed");
    Select(0);struct SoulLinkPair deltaPair=Pair(0,87);
    Write(SCRATCH+128,&deltaPair.mons[0],sizeof(deltaPair.mons[0]));
    assert(GAME("SoulLink_MarkLocalDeath",SCRATCH+128));
    normalPacing=true;nextFrameNs=0;
    fprintf(stderr,"Starting one-change 88-pair differential benchmark at at most 60 fps\n");
    assert(RemoteSync());
    normalPacing=false;
    for(unsigned p=0;p<2;p++) {
        Select(p);assert(GAME0("SoulLink_GetPairCount")==88);
        assert(Pair(p,87).state==SOUL_LINK_PAIR_DEAD);
        for(unsigned i=3;i<87;i++)assert(Pair(p,i).state==SOUL_LINK_PAIR_ALIVE);
    }
    puts("WAN differential one-row death with 88 pairs passed");

    assert(scriptErrors==0);
    for(unsigned p=0;p<2;p++) {
        OpenMenu(p);Shot(p,p ? "/mnt/c/Pokemon Soul Link/FRLG-Plus/build/tests/stage13-internet-menu-b.png" : "/mnt/c/Pokemon Soul Link/FRLG-Plus/build/tests/stage13-internet-menu-a.png");
        Press(p,2);Frames(100);Press(p,2);Frames(60);
    }
    puts("WAN end-to-end: real ROM + mGBA Lua + TCP companions; captures, deaths, quick loads, fresh sessions, automatic teams and remote mail diagnosis passed");
    for(unsigned p=0;p<2;p++) {
        mScriptContextTriggerCallback(&contexts[p],"stop");mScriptContextDeinit(&contexts[p]);
        mCoreConfigDeinit(&games[p]->config);games[p]->deinit(games[p]);
    }
    return 0;
}

#endif
