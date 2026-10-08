// Measure emulated CPU cycles, not host stepping time or WAN latency.
#define SOUL_LINK_NETWORK_NO_MAIN
#include "test_network_rom.c"
#include "soul_link_companion.h"
#include "pokemon_storage_system.h"
#include <mgba/core/timing.h>
static void (*originalStep)(struct mCore *);
static uint32_t lowestSP;
static struct { const char *name; uint32_t begin,end; uint64_t cycles; } profile[] = {
    {"SoulLink_PrepareTeamChange",0,0,0}, {"SoulLink_ReadPair",0,0,0},
    {"GetPair",0,0,0}, {"Matches",0,0,0}, {"GetBoxMonData",0,0,0},
    {"GetMonData",0,0,0}, {"SoulLink_IsBoxMonDead",0,0,0},
    {"SoulLink_FindLocalPair",0,0,0}, {"BoxMonToMon",0,0,0},
    {"PopulateBoxHpAndStatusToPartyMon",0,0,0}, {"CalculateMonStats",0,0,0},
    {"GetSubstruct",0,0,0}, {"SoulLink_CanUseSpecies",0,0,0},
    {"__umodsi3",0,0,0}, {"__udivsi3",0,0,0}, {"memcpy",0,0,0},
    {"DecryptBoxMon",0,0,0}, {"EncryptBoxMon",0,0,0}, {"CalculateBoxMonChecksum",0,0,0}
};
static bool profiling;
static const char *bodyFixture = "six-box-swap";
static void ProfileBegin(void) {
    profiling=getenv("SOUL_LINK_TIMING_PROFILE")!=NULL;
    if(!profiling)return;
    for(unsigned i=0;i<ARRAY_COUNT(profile);i++) {
        profile[i].begin=Symbol(profile[i].name);profile[i].end=UINT32_MAX;profile[i].cycles=0;
        FILE *symbols=fopen(sSymbols,"r");assert(symbols);char line[512],type,name[256];unsigned address;
        // Two static Matches functions exist. Select the one immediately
        // preceding team preparation rather than the local-death matcher.
        if(!strcmp(profile[i].name,"Matches")) {
            uint32_t team=Symbol("SoulLink_PrepareTeamChange");
            while(fgets(line,sizeof(line),symbols))
                if(sscanf(line,"%x %c %255s",&address,&type,name)==3 && !strcmp(name,"Matches") && address<team && address>profile[i].begin)
                    profile[i].begin=address;
            rewind(symbols);
        }
        while(fgets(line,sizeof(line),symbols))
            if(sscanf(line,"%x %c %255s",&address,&type,name)==3 && (type=='t'||type=='T') && address>profile[i].begin && address<profile[i].end)
                profile[i].end=address;
        fclose(symbols);
    }
}
static void TimedStep(struct mCore *core) {
    uint32_t pc=0,begin=0;
    if(profiling){assert(core->readRegister(core,"pc",&pc));begin=(uint32_t)mTimingCurrentTime(core->timing);}
    originalStep(core);uint32_t sp;assert(core->readRegister(core,"sp",&sp));
    if(sp<lowestSP)lowestSP=sp;
    if(profiling)for(unsigned i=0;i<ARRAY_COUNT(profile);i++)
        if(pc>=profile[i].begin && pc<profile[i].end){profile[i].cycles+=(uint32_t)mTimingCurrentTime(core->timing)-begin;break;}
}

static uint32_t MeasureBodyCall(unsigned role, bool fullPC, const char *name, unsigned argc, const uint32_t *args) {
    uint32_t begin=(uint32_t)mTimingCurrentTime(sCore->timing);
    ProfileBegin();
    originalStep=sCore->step;lowestSP=STACK;sCore->step=TimedStep;
    uint32_t result=GameCall(name,argc,args);
    sCore->step=originalStep;
    if(profiling){printf("PROFILE role=%u fullPC=%u operation=%s",role,fullPC,name);for(unsigned i=0;i<ARRAY_COUNT(profile);i++)printf(" %s:%.3f",profile[i].name,profile[i].cycles*1000.0/16777216.0);puts("");profiling=false;}
    uint32_t cycles=(uint32_t)mTimingCurrentTime(sCore->timing)-begin;
    assert(cycles>0 && cycles<16777216u*30u);
    assert(lowestSP>=STACK-2048);
    printf("BODY {\"role\":%u,\"fullPC\":%s,\"fixture\":\"%s\",\"operation\":\"%s\",\"cycles\":%u,\"emulatedMs\":%.3f,\"frameEquivalents\":%.3f,\"stackBytesBelowFixtureSP\":%u}\n",
           role,fullPC?"true":"false",bodyFixture,name,cycles,cycles*1000.0/16777216.0,cycles/280896.0,STACK-lowestSP);fflush(stdout);
    return result;
}

static void BodyFixtures(unsigned role, bool fullPC) {
    Select(role);GAME0("SoulLink_ResetSaveData");GAME0("ZeroPlayerPartyMons");GAME0("ResetPokemonStorageSystem");
    struct SoulLinkRunId run={{123,456}};uint32_t participants[2]={10,20};
    struct SoulLinkConfig config={1,1,1,1,1,0,1,0};
    Write(SCRATCH,&run,sizeof(run));Write(SCRATCH+32,participants,sizeof(participants));
    assert(GAME("SoulLink_BeginRun",SCRATCH,SCRATCH+32,role));
    Write(SCRATCH+64,&config,sizeof(config));assert(GAME("SoulLink_SetConfig",SCRATCH+64));
    // Valid pair metadata at capacity, with six actual party members and six
    // selected bodies at the far end of a densely populated PC.
    for(unsigned i=0;i<88;i++) {
        struct SoulLinkPair pair={0};pair.pairId=i+1;pair.areaId=500+i;pair.state=SOUL_LINK_PAIR_ALIVE;
        for(unsigned p=0;p<2;p++) {
            pair.mons[p]=(struct SoulLinkMonIdentity){p?20:10,1000+p*100+i,SPECIES_BULBASAUR,i+1};
            pair.encounterStates[p]=SOUL_LINK_ENCOUNTER_CAPTURED;
        }
        Write(SCRATCH+128,&pair,sizeof(pair));assert(GAME("SoulLink_StorePair",SCRATCH+128));
    }
    unsigned occupied=fullPC?420:390;
    uint32_t party=Symbol("gPlayerParty");uint8_t body[100];
    for(unsigned i=0;i<6;i++)
        GAME("CreateMon",party+i*100,SPECIES_BULBASAUR,5,31,1,1000+role*100+i,OT_ID_PRESET,role?20:10);
    GAME0("CalculatePlayerPartyCount");
    for(unsigned i=0;i<occupied;i++) {
        bool chosen=i>=occupied-6;
        uint32_t pid=chosen?1000+role*100+82+i-(occupied-6):100000+i;
        GAME("CreateMon",SCRATCH,SPECIES_BULBASAUR,5,31,1,pid,OT_ID_PRESET,chosen?(role?20:10):30);
        Read(SCRATCH,body,80);Write(GAME("GetBoxedMonPtr",i/30,i%30),body,80);
    }
    // Preparation is read-only across both the complete PC and party.
    uint32_t storage=sCore->busRead32(sCore,Symbol("gPokemonStoragePtr"));
    uint8_t beforePC[sizeof(struct PokemonStorage)],afterPC[sizeof(struct PokemonStorage)];
    uint8_t beforeParty[600],afterParty[600];
    Read(storage,beforePC,sizeof(beforePC));Read(party,beforeParty,sizeof(beforeParty));
    uint32_t plan=GAME("AllocZeroed",2048);assert(plan);
    // Fewer boxed selections than deposits must still find real empty slots,
    // or refuse a full PC. Also cover an unchanged all-party composition.
    uint16_t narrow=83;Write(SCRATCH+64,&narrow,sizeof(narrow));
    uint32_t narrowArgs[3]={plan,SCRATCH+64,1};bodyFixture="one-box-selection";
    assert((bool)MeasureBodyCall(role,fullPC,"SoulLink_PrepareTeamChange",3,narrowArgs)==!fullPC);
    if(fullPC) {
        struct SoulLinkTeamDiagnostic diagnostic;
        GAME("SoulLink_DescribeTeamChange",plan,SCRATCH+200);Read(SCRATCH+200,&diagnostic,sizeof(diagnostic));
        assert(diagnostic.reason==SOUL_LINK_TEAM_PC_SPACE && diagnostic.neededSlots==6 && diagnostic.availableSlots==1);
    }
    narrow=1;Write(SCRATCH+64,&narrow,sizeof(narrow));
    bodyFixture="one-party-selection";
    assert((bool)MeasureBodyCall(role,fullPC,"SoulLink_PrepareTeamChange",3,narrowArgs)==!fullPC);
    uint16_t current[6]={1,2,3,4,5,6};Write(SCRATCH+64,current,sizeof(current));
    uint32_t currentArgs[3]={plan,SCRATCH+64,6};bodyFixture="unchanged-party";
    assert(MeasureBodyCall(role,fullPC,"SoulLink_PrepareTeamChange",3,currentArgs));
    Read(storage,afterPC,sizeof(afterPC));Read(party,afterParty,sizeof(afterParty));
    assert(!memcmp(beforePC,afterPC,sizeof(beforePC)));assert(!memcmp(beforeParty,afterParty,sizeof(beforeParty)));
    uint16_t ids[6]={83,84,85,86,87,88};Write(SCRATCH+64,ids,sizeof(ids));
    // A duplicate body in party must still refuse the boxed selection. A
    // matching personality alone or matching OT alone must not count as one.
    uint32_t boxed=GAME("GetBoxedMonPtr",(occupied-6)/30,(occupied-6)%30);
    Read(boxed,body,80);Write(party,body,80);
    assert(!GAME("SoulLink_PrepareTeamChange",plan,SCRATCH+64,6));
    struct SoulLinkTeamDiagnostic diagnostic;
    GAME("SoulLink_DescribeTeamChange",plan,SCRATCH+200);Read(SCRATCH+200,&diagnostic,sizeof(diagnostic));
    assert(diagnostic.reason==SOUL_LINK_TEAM_DUPLICATE && diagnostic.pairId==83);
    // The first member's body conflict must precede a later invalid ID.
    uint16_t invalid[2]={83,0};Write(SCRATCH+64,invalid,sizeof(invalid));
    assert(!GAME("SoulLink_PrepareTeamChange",plan,SCRATCH+64,2));
    GAME("SoulLink_DescribeTeamChange",plan,SCRATCH+200);Read(SCRATCH+200,&diagnostic,sizeof(diagnostic));
    assert(diagnostic.reason==SOUL_LINK_TEAM_DUPLICATE && diagnostic.pairId==83);
    Write(SCRATCH+64,ids,sizeof(ids));
    GAME("CreateMon",party,SPECIES_BULBASAUR,5,31,1,1000+role*100+82,OT_ID_PRESET,30);
    assert(GAME("SoulLink_PrepareTeamChange",plan,SCRATCH+64,6));
    GAME("CreateMon",party,SPECIES_BULBASAUR,5,31,1,999999,OT_ID_PRESET,role?20:10);
    assert(GAME("SoulLink_PrepareTeamChange",plan,SCRATCH+64,6));
    Write(party,beforeParty,sizeof(beforeParty));
    Write(SCRATCH+64,invalid,sizeof(invalid));
    assert(!GAME("SoulLink_PrepareTeamChange",plan,SCRATCH+64,2));
    GAME("SoulLink_DescribeTeamChange",plan,SCRATCH+200);Read(SCRATCH+200,&diagnostic,sizeof(diagnostic));
    assert(diagnostic.reason==SOUL_LINK_TEAM_INVALID_PAIR && diagnostic.pairId==0);
    invalid[1]=999;Write(SCRATCH+64,invalid,sizeof(invalid));
    assert(!GAME("SoulLink_PrepareTeamChange",plan,SCRATCH+64,2));
    GAME("SoulLink_DescribeTeamChange",plan,SCRATCH+200);Read(SCRATCH+200,&diagnostic,sizeof(diagnostic));
    assert(diagnostic.reason==SOUL_LINK_TEAM_INVALID_PAIR && diagnostic.pairId==999);
    Write(SCRATCH+64,ids,sizeof(ids));
    uint32_t args[3]={plan,SCRATCH+64,6};
    bodyFixture="six-box-swap";
    assert(MeasureBodyCall(role,fullPC,"SoulLink_PrepareTeamChange",3,args));
    Read(storage,afterPC,sizeof(afterPC));Read(party,afterParty,sizeof(afterParty));
    assert(!memcmp(beforePC,afterPC,sizeof(beforePC)));assert(!memcmp(beforeParty,afterParty,sizeof(beforeParty)));
    MeasureBodyCall(role,fullPC,"SoulLink_CommitTeamChange",1,args);
    assert(sCore->busRead8(sCore,Symbol("gPlayerPartyCount"))==6);
    for(unsigned i=0;i<6;i++)assert(GAME("GetMonData",party+i*100,MON_DATA_PERSONALITY,0)==1000+role*100+82+i);
    // Every displaced party body still exists exactly once in the PC.
    unsigned found[6]={0};
    for(unsigned i=0;i<420;i++) {
        uint32_t mon=GAME("GetBoxedMonPtr",i/30,i%30);
        uint32_t pid=GAME("GetBoxMonData",mon,MON_DATA_PERSONALITY,0);
        for(unsigned j=0;j<6;j++)if(pid==1000+role*100+j)found[j]++;
    }
    for(unsigned i=0;i<6;i++)assert(found[i]==1);
    GAME("Free",plan);
    struct SoulLinkMonIdentity dead={role?20:10,1000+role*100+82,SPECIES_BULBASAUR,83};
    Write(SCRATCH+128,&dead,sizeof(dead));assert(GAME("SoulLink_MarkLocalDeath",SCRATCH+128));
    GAME0("SoulLink_RequestCleanup");
    MeasureBodyCall(role,fullPC,"SoulLink_ProcessField",0,NULL);
    struct SoulLinkPair pair=Pair(role,82);assert(pair.state==SOUL_LINK_PAIR_DEAD);
    if(fullPC) {
        assert(sCore->busRead8(sCore,Symbol("gPlayerPartyCount"))==6);
        assert(pair.cleanupFlags&(1<<role));
    } else {
        assert(sCore->busRead8(sCore,Symbol("gPlayerPartyCount"))==5);
        assert(!(pair.cleanupFlags&(1<<role)));
        uint32_t grave=GAME("GetBoxedMonPtr",13,0);
        assert(GAME("GetBoxMonData",grave,MON_DATA_PERSONALITY,0)==dead.personality);
        assert(GAME("GetBoxMonData",grave,MON_DATA_BOX_HP,0)==0);
    }
    assert(!GAME0("ArePlayerFieldControlsLocked"));
    puts("PASS body fixture: read-only preparation, six-body swap, preserved deposits, bounded death cleanup or retained full-PC pending death");
}

static struct SoulLinkCompanionView View(unsigned role) {
    struct SoulLinkCompanionView view={0};Select(role);
    uint32_t header=sCore->busRead32(sCore,Symbol("gSaveBlock2Ptr"))+0xB20;
    struct SoulLinkSaveHeader h;Read(header,&h,sizeof(h));
    view.magic=SOUL_LINK_COMPANION_MAGIC;view.generation=1;
    view.saveStatus=SOUL_LINK_SAVE_READY;view.enabled=1;view.player=role;
    view.run=h.runId;memcpy(view.participants,h.participantIds,sizeof(view.participants));view.config=h.config;
    view.pairCount=h.pairCount;view.pairDigest=2166136261u;
    for(unsigned i=0;i<view.pairCount;i++) {
        assert(GAME("SoulLink_ReadPair",i,SCRATCH+128));Read(SCRATCH+128,&view.pairs[i],sizeof(view.pairs[i]));
        const uint8_t *bytes=(const uint8_t*)&view.pairs[i];
        for(unsigned j=0;j<31;j++)view.pairDigest=(view.pairDigest^bytes[j])*16777619u;
    }
    return view;
}
static void Measure(unsigned role, unsigned count, const char *change) {
    struct SoulLinkCompanionView view=View(role^1);Select(role);
    uint32_t buffer=GAME("Alloc",sizeof(view));assert(buffer);Write(buffer,&view,sizeof(view));
    assert(GAME0("IsOverworldIdle"));assert(!GAME0("ArePlayerFieldControlsLocked"));
    // Release mGBA does not maintain globalCycles (debugger-only). The
    // wrapping scheduler clock includes both processed and pending cycles.
    uint32_t begin=(uint32_t)mTimingCurrentTime(sCore->timing);
    originalStep=sCore->step;lowestSP=STACK;sCore->step=TimedStep;
    bool sliced=getenv("SOUL_LINK_TIMING_SLICED")!=NULL;
    bool ok;
    uint64_t peak=0,phasePeak[8]={0};unsigned calls=0;
    if(sliced) {
        assert(GAME0("SoulLink_ApplyJobSize")==9808); // Guard the phase offset below.
        ok=GAME("SoulLink_CompanionStartApplyView",buffer);
        peak=(uint32_t)((uint32_t)mTimingCurrentTime(sCore->timing)-begin);calls=1;
        uint32_t result=0;
        while(ok&&!result) {
            uint32_t job=sCore->busRead32(sCore,Symbol("sApplyJob"));assert(job);
            unsigned phase=sCore->busRead8(sCore,job+8525);assert(phase<8);
            uint32_t start=(uint32_t)mTimingCurrentTime(sCore->timing);
            result=GAME0("SoulLink_CompanionStepApplyView");
            uint64_t step=(uint32_t)((uint32_t)mTimingCurrentTime(sCore->timing)-start);
            if(step>peak)peak=step;
            if(step>phasePeak[phase])phasePeak[phase]=step;
            assert(++calls<200);
        }
        ok=ok&&result==1;
    } else ok=GAME("SoulLink_CompanionApplyView",buffer);
    sCore->step=originalStep;
    uint64_t cycles=(uint32_t)((uint32_t)mTimingCurrentTime(sCore->timing)-begin);
    assert(cycles>1000 && cycles<16777216u*30u);
    assert(lowestSP>=STACK-2048);
    assert(ok);assert(!GAME0("ArePlayerFieldControlsLocked"));
    assert(!sCore->busRead32(sCore,Symbol("gSoulLinkRemoteMailbox")));
    GAME("Free",buffer);
    if(sliced)printf("SLICED {\"role\":%u,\"pairs\":%u,\"change\":\"%s\",\"calls\":%u,\"peakCycles\":%llu,\"peakEmulatedMs\":%.3f,\"totalEmulatedMs\":%.3f,\"stackBytesBelowFixtureSP\":%u}\n",role,count,change,calls,(unsigned long long)peak,peak*1000.0/16777216.0,cycles*1000.0/16777216.0,STACK-lowestSP);
    if(sliced&&count>=87) {printf("PHASE role=%u pairs=%u change=%s",role,count,change);for(unsigned i=0;i<8;i++)printf(" %u:%.3f",i,phasePeak[i]*1000.0/16777216.0);puts("");}
    printf("TIMING {\"role\":%u,\"pairs\":%u,\"change\":\"%s\",\"cycles\":%llu,\"emulatedMs\":%.3f,\"frameEquivalents\":%.3f,\"stackBytesBelowFixtureSP\":%u}\n",
           role,count,change,(unsigned long long)cycles,cycles*1000.0/16777216.0,cycles/280896.0,STACK-lowestSP);fflush(stdout);
}
int main(int argc,char **argv) {
    assert(argc==3);sSymbols=argv[2];mLogSetDefaultLogger(&sLogger);
    for(unsigned p=0;p<2;p++) {
        games[p]=mCoreFind(argv[1]);assert(games[p]&&games[p]->init(games[p]));
        mCoreInitConfig(games[p],"soul-link-apply-timing");games[p]->setVideoBuffer(games[p],pixels[p],240);
        assert(mCoreLoadFile(games[p],argv[1]));games[p]->reset(games[p]);
        for(unsigned f=0;f<720;f++)games[p]->runFrame(games[p]);
        Select(p);
        sCore->busWrite32(sCore,Symbol("gSaveBlock1Ptr"),Symbol("gSaveBlock1"));
        sCore->busWrite32(sCore,Symbol("gSaveBlock2Ptr"),Symbol("gSaveBlock2"));
        sCore->busWrite32(sCore,Symbol("gPokemonStoragePtr"),Symbol("gPokemonStorage"));
        sCore->busWrite8(sCore,Symbol("gSaveBlock1")+0x3A4C,0xFF);
        sCore->busWrite8(sCore,Symbol("gSaveBlock2"),0xBB+p);sCore->busWrite8(sCore,Symbol("gSaveBlock2")+1,0xFF);
        sCore->busWrite32(sCore,Symbol("gMain")+4,Symbol("CB2_NewGame")|1);sCore->busWrite8(sCore,Symbol("gMain")+0x438,0);
    }
    Frames(200);
    for(unsigned p=0;p<2;p++) {
        Select(p);scratch[p]=GAME("AllocZeroed",512);assert(scratch[p]);
        Select(p);
        GAME0("SoulLink_ResetSaveData");
        GAME("FlagSet",FLAG_SYS_POKEMON_GET);GAME("FlagSet",FLAG_SYS_POKEDEX_GET);
        struct SoulLinkRunId run={{123,456}};uint32_t participants[2]={10,20};
        struct SoulLinkConfig config={1,1,1,1,1,0,0,0};
        Write(SCRATCH,&run,sizeof(run));Write(SCRATCH+32,participants,sizeof(participants));
        assert(GAME("SoulLink_BeginRun",SCRATCH,SCRATCH+32,p));
        Write(SCRATCH+64,&config,sizeof(config));assert(GAME("SoulLink_SetConfig",SCRATCH+64));
    }
    if(getenv("SOUL_LINK_TIMING_BODIES")) {
        for(unsigned role=0;role<2;role++)for(unsigned full=0;full<2;full++)BodyFixtures(role,full);
        for(unsigned p=0;p<2;p++){mCoreConfigDeinit(&games[p]->config);games[p]->deinit(games[p]);}
        return 0;
    }
    const unsigned sizes[]={1,6,30,87,88};unsigned previous=0;
    for(unsigned s=0;s<5;s++) {
        unsigned count=sizes[s];
        for(unsigned p=0;p<2;p++) for(unsigned i=previous;i<count;i++) {
            Select(p);struct SoulLinkMonIdentity identity={p?20:10,1000+p*100+i,SPECIES_BULBASAUR,0};
            Write(SCRATCH+128,&identity,sizeof(identity));
            assert(GAME("SoulLink_RecordLocalEncounter",500+i,SCRATCH+128,SOUL_LINK_ENCOUNTER_CAPTURED));
        }
        const char *change=count==previous+1?"one-capture":"capture-batch";
        Measure(0,count,change);Measure(1,count,change);
        for(unsigned repeat=0;repeat<3;repeat++){Measure(0,count,"unchanged");Measure(1,count,"unchanged");}
        previous=count;
    }
    Select(0);struct SoulLinkMonIdentity identity={10,1000,SPECIES_BULBASAUR,1};
    Write(SCRATCH+128,&identity,sizeof(identity));assert(GAME("SoulLink_MarkLocalDeath",SCRATCH+128));
    Measure(1,88,"one-death");Measure(0,88,"death-confirmation");
    for(unsigned p=0;p<2;p++){Select(p);assert(Pair(p,0).state==SOUL_LINK_PAIR_DEAD);mCoreConfigDeinit(&games[p]->config);games[p]->deinit(games[p]);}
    puts("PASS local apply timing: compiled ROM, both roles, capacity, terminal death and no movement/network lock");
    return 0;
}
