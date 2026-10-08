// Two normal game loops over mGBA SIO; no packet forwarding.
#define main IsolatedRegressionMain
#include "test_rom.c"
#undef main
// Internal mGBA layouts must match the Release library.
#define NDEBUG
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/sio/lockstep.h>
#undef NDEBUG
#include "constants/flags.h"
#include "constants/maps.h"
#include "save.h"
static struct mCore *games[2];
static color_t pixels[2][240*160];
static uint32_t scratch[2];
static struct GBASIOLockstep cable;
static struct GBASIOLockstepNode nodes[2];
static bool awake[2] = {true, true}, attached;
static int32_t posted[2];
static unsigned waitMask;
static bool Signal(struct mLockstep *c,unsigned m) {
    (void)c;
    waitMask&=~m;
    if(!waitMask)awake[0]=true;
    return true;
}
static bool Wait(struct mLockstep *c,unsigned m) {
    (void)c;
    waitMask|=m;
    awake[0]=false;
    return true;
}
static void AddCycles(struct mLockstep *c,int id,int32_t n) {
    (void)c;
    assert(n>=0);
    posted[id?id:1]+=n;
    if(!id) {
        if(!awake[1])nodes[1].nextEvent+=posted[1];
        awake[1]=true;
    }
}
static int32_t UseCycles(struct mLockstep *c,int id,int32_t n) {
    (void)c;
    posted[id]-=n;
    if(posted[id]<=0)awake[id]=false;
    return posted[id];
}
static int32_t Unused(struct mLockstep *c,int id) {
    (void)c;
    return posted[id];
}
static void Unload(struct mLockstep *c,int id) {
    if(id) {
        posted[id]=0;
        Signal(c,1u<<id);
    }
    else {
        posted[1]=0;
        nodes[1].nextEvent=0;
        awake[1]=true;
    }
    awake[id]=true;
}
static void Select(unsigned p) {
    sCore=games[p];
    sScratch=scratch[p];
}
// Preserve system bank and live stack when a fixture call interrupts an IRQ.
static uint32_t GameCall(const char *name,unsigned argc,const uint32_t *args) {
    uint32_t regs[16],cpsr,sp,lr,result;
    uint8_t stackImage[0x2000];
    char reg[8];
    Read(0x03006000,stackImage,sizeof(stackImage));
    uint16_t ime=sCore->busRead16(sCore,0x4000208);
    for(unsigned i=0;i<16;i++) {
        snprintf(reg,sizeof(reg),"r%u",i);
        assert(sCore->readRegister(sCore,reg,&regs[i]));
    }
    assert(sCore->readRegister(sCore,"cpsr",&cpsr));
    Reg("cpsr",(cpsr&~0x1Fu)|0x1F);
    assert(sCore->readRegister(sCore,"sp",&sp));
    assert(sCore->readRegister(sCore,"lr",&lr));
    result=Call(name,argc,args);
    Reg("sp",sp);
    Reg("lr",lr);
    Write(0x03006000,stackImage,sizeof(stackImage));
    Reg("cpsr",cpsr);
    for(unsigned i=0;i<15;i++) {
        snprintf(reg,sizeof(reg),"r%u",i);
        Reg(reg,regs[i]);
    }
    Reg("pc",regs[15]-((cpsr&0x20)?2:4));
    sCore->busWrite16(sCore,0x4000208,ime);
    return result;
}
#define GAME0(n) GameCall(n,0,NULL)
#define GAME(n,...) GameCall(n,sizeof((uint32_t[]){__VA_ARGS__})/sizeof(uint32_t),(uint32_t[]){__VA_ARGS__})
static void Frames(unsigned n) {
    uint32_t target[2]= {
        games[0]->frameCounter(games[0])+n,games[1]->frameCounter(games[1])+n
    }
    ;
    unsigned i=0;
    while(games[0]->frameCounter(games[0])<target[0]||games[1]->frameCounter(games[1])<target[1]) {
        assert(awake[0]||awake[1]);
        for(unsigned p=0;p<2;p++)if(awake[p])games[p]->runLoop(games[p]);
        assert(++i<1000000);
    }
}
static void Press(unsigned p,unsigned key) {
    games[p]->setKeys(games[p],key);
    Frames(10);
    games[p]->setKeys(games[p],0);
    Frames(30);
}
static void Connect(void) {
    memset(&cable,0,sizeof(cable));
    memset(nodes,0,sizeof(nodes));
    mLockstepInit(&cable.d);
    GBASIOLockstepInit(&cable);
    cable.d.signal=Signal;
    cable.d.wait=Wait;
    cable.d.addCycles=AddCycles;
    cable.d.useCycles=UseCycles;
    cable.d.unusedCycles=Unused;
    cable.d.unload=Unload;
    awake[0]=awake[1]=true;
    posted[0]=posted[1]=0;
    waitMask=0;
    for(unsigned p=0;p<2;p++) {
        GBASIOLockstepNodeCreate(&nodes[p]);
        assert(GBASIOLockstepAttachNode(&cable,&nodes[p]));
        struct GBA *g=games[p]->board;
        GBASIOSetDriver(&g->sio,&nodes[p].d,SIO_MULTI);
        GBASIOSetDriver(&g->sio,&nodes[p].d,SIO_NORMAL_32);
    }
    attached=true;
}
static void Disconnect(void) {
    if(!attached)return;
    for(unsigned p=0;p<2;p++) {
        struct GBA *g=games[p]->board;
        GBASIOSetDriver(&g->sio,NULL,SIO_MULTI);
        GBASIOSetDriver(&g->sio,NULL,SIO_NORMAL_32);
        GBASIOLockstepDetachNode(&cable,&nodes[p]);
    }
    attached=false;
    awake[0]=awake[1]=true;
}
static void Shot(unsigned p,const char *path) {
    Select(p);
    memcpy(sPixels,pixels[p],sizeof(sPixels));
    Screenshot(path);
}
static void OpenMenu(unsigned p) {
    uint32_t until=games[p]->frameCounter(games[p])+2400;
    bool opened=false;
    while(games[p]->frameCounter(games[p])<until) {
        Select(p);
        if(GAME("FuncIsActiveTask",Symbol("Task_StartMenuHandleInput")|1)) {opened=true;break;}
        if(GAME0("IsOverworldIdle"))Press(p,8);else Frames(1);
    }
    assert(opened);
    Select(p);
    assert(sCore->busRead8(sCore,Symbol("sNumStartMenuItems"))==8);
    for(unsigned i=0;i<8;i++) {
        unsigned cursor=sCore->busRead8(sCore,Symbol("sStartMenuCursorPos"));
        if(sCore->busRead8(sCore,Symbol("sStartMenuOrder")+cursor)==9)break;
        Press(p,128);
    }
    if(!p) {
        Shot(p,"/mnt/c/Pokemon Soul Link/FRLG-Plus/build/tests/stage11-start-menu.png");
    }
    Press(p,1);
    Frames(80);
    Select(p);
    assert(sCore->busRead32(sCore,Symbol("sSoulLinkMenu")));
}
static void CreateRun(unsigned p) {
    Select(p);
    struct SoulLinkRunId run={{0x12345678,SOUL_LINK_SAVE_MAGIC}};
    uint32_t participants[2]={10,20};
    Write(SCRATCH,&run,sizeof(run));Write(SCRATCH+32,participants,sizeof(participants));
    assert(GAME("SoulLink_BeginRun",SCRATCH,SCRATCH+32,p));
    // The reduced menu still edits rules, and B discards unapplied changes.
    OpenMenu(p);Press(p,1);Press(p,16);Press(p,2);
    Select(p);assert(!GAME0("SoulLink_IsEnabled"));
    Press(p,1);Press(p,16);
    for(unsigned row=0;row<6;row++)Press(p,128);
    Press(p,1);Press(p,1);Press(p,2);Frames(100);Press(p,2);Frames(60);
    Select(p);struct SoulLinkSaveHeader h;
    Read(sCore->busRead32(sCore,Symbol("gSaveBlock2Ptr"))+0xB20,&h,sizeof(h));
    assert(h.runId.words[0]==run.words[0]&&h.localPlayer==p&&h.config.enabled);
    assert(h.participantIds[0]==10&&h.participantIds[1]==20);
    assert(GAME0("IsOverworldIdle"));
    fprintf(stderr,"Player %u: reduced menu rule save and cancel passed\n",p);
}
static void Dismiss(unsigned p) {
    for(unsigned n=0;n<30;n++) {
        Select(p);
        if(GAME0("IsOverworldIdle"))return;
        Press(p,1);
    }
    Shot(p,"/mnt/c/Pokemon Soul Link/FRLG-Plus/build/tests/stage11-locked.png");
    assert(!"Field controls remained locked");
}
static bool Sync(void) {
    for(unsigned p=0;p<2;p++) {
        Select(p);assert(GAME0("IsOverworldIdle"));
        assert(GAME0("SoulLink_StartCableSession"));
        Frames(100);
    }
    uint32_t a=Symbol("sSessionStatus");
    for(unsigned f=0;f<2000;f++) {
        Frames(1);
        if(games[0]->busRead8(games[0],a)>=SOUL_LINK_SESSION_COMPLETE&&games[1]->busRead8(games[1],a)>=SOUL_LINK_SESSION_COMPLETE)break;
    }
    bool ok=true;
    for(unsigned p=0;p<2;p++) {
        unsigned s=games[p]->busRead8(games[p],a);
        fprintf(stderr,"Player %u serial status=%u\n",p,s);
        ok&=s==SOUL_LINK_SESSION_COMPLETE;
    }
    Frames(90);
    for(unsigned p=0;p<2;p++) {
        Select(p);
        if(GAME("SoulLink_GetTeamDiagnostic",SCRATCH+64,SCRATCH+80)) {
            struct SoulLinkTeamDiagnostic diagnostic;
            Read(SCRATCH+64,&diagnostic,sizeof(diagnostic));
            if(diagnostic.reason==SOUL_LINK_TEAM_MAIL) {
                Press(p,1);
                Shot(p,p ? "/mnt/c/Pokemon Soul Link/FRLG-Plus/build/tests/stage11-result-mail-b.png" : "/mnt/c/Pokemon Soul Link/FRLG-Plus/build/tests/stage11-result-mail-a.png");
            }
        }
    }
    Dismiss(0);
    Dismiss(1);
    Frames(90);
    Dismiss(0);
    Dismiss(1);
    return ok;
}
static void Gift(unsigned p,uint16_t map,uint16_t species,uint32_t pid) {
    Select(p);
    GAME("SetWarpDestination",map>>8,map&255,(uint32_t)-1,10,10);
    GAME0("WarpIntoMap");
    GAME("SetMainCallback2",Symbol("CB2_LoadMap")|1);
    Frames(100);
    Select(p);
    assert(GAME0("IsOverworldIdle"));
    scratch[p]=GAME("AllocZeroed",512);
    assert(scratch[p]);
    Select(p);
    GAME("CreateMon",SCRATCH,species,5,31,1,pid,OT_ID_PRESET,p?20:10);
    assert(GAME("GiveMonToPlayer",SCRATCH)==MON_GIVEN_TO_PARTY);
    Frames(90);
    Dismiss(p);
}
static struct SoulLinkPair Pair(unsigned p,unsigned i) {
    struct SoulLinkPair row;
    Select(p);
    assert(GAME("SoulLink_ReadPair",i,SCRATCH+128));
    Read(SCRATCH+128,&row,sizeof(row));
    return row;
}
static void CheckGrave(unsigned p) {
    Select(p);
    uint32_t mon = GAME("GetBoxedMonPtr", 13, 0);
    assert(mon && GAME("GetBoxMonData", mon, MON_DATA_PERSONALITY, 0) == (p ? 601 : 501));
    assert(GAME("GetBoxMonData", mon, MON_DATA_SPECIES, 0) == (p ? SPECIES_RATTATA : SPECIES_BULBASAUR));
}
static void CancelFromMenu(void) {
    Select(0);assert(GAME0("SoulLink_StartCableSession"));
    Frames(100);
    assert(games[0]->busRead8(games[0], Symbol("sSessionStatus")) == SOUL_LINK_SESSION_CONNECTING);
    Press(0, 2);
    Frames(90);
    Dismiss(0);
    assert(games[0]->busRead8(games[0], Symbol("sSessionStatus")) == SOUL_LINK_SESSION_CANCELED);
    assert(games[0]->busRead32(games[0], Symbol("sSession")) == 0);
    assert(games[0]->busRead16(games[0], Symbol("gLinkType")) == 0);
    assert(Pair(0, 0).state == SOUL_LINK_PAIR_DEAD);
    fprintf(stderr, "Native connection cancellation and field recovery passed\n");
}
static void SaveReload(void) {
    struct SoulLinkPair savedPairs[2][3];
    struct SoulLinkTeamSelection savedTeams[2];
    for (unsigned p = 0; p < 2; p++) {
        for (unsigned row = 0; row < 3; row++) savedPairs[p][row] = Pair(p, row);
        Select(p); Read(sCore->busRead32(sCore, Symbol("gSaveBlock2Ptr")) + 0xB20
                     + offsetof(struct SoulLinkSaveHeader, team), &savedTeams[p], sizeof(savedTeams[p]));
    }
    for (unsigned p = 0; p < 2; p++) {
        Select(p);
        CheckGrave(p);
        uint32_t before = sCore->busRead32(sCore, Symbol("gSaveCounter"));
        Press(p, 8);
        Select(p);
        for (unsigned row = 0; row < 8; row++) {
            unsigned cursor = sCore->busRead8(sCore, Symbol("sStartMenuCursorPos"));
            if (sCore->busRead8(sCore, Symbol("sStartMenuOrder") + cursor) == 4) break;
            Press(p, 128);
        }
        Press(p, 1);
        Frames(60);
        for (unsigned attempt = 0; attempt < 20; attempt++) {
            Press(p, 1);
            Frames(60);
            if (sCore->busRead32(sCore, Symbol("gSaveCounter")) != before && sCore->busRead8(sCore, Symbol("gSaveAttemptStatus")) == SAVE_STATUS_OK) break;
        }
        assert(sCore->busRead32(sCore, Symbol("gSaveCounter")) != before);
        assert(sCore->busRead8(sCore, Symbol("gSaveAttemptStatus")) == SAVE_STATUS_OK);
        Frames(120);
        Dismiss(p);
        void *flash = NULL;
        size_t size = games[p]->savedataClone(games[p], &flash);
        assert(size == 128 * 1024 && flash);
        FILE *file = fopen(p ? "/mnt/c/Pokemon Soul Link/FRLG-Plus/build/tests/stage11-player-b.sav" : "/mnt/c/Pokemon Soul Link/FRLG-Plus/build/tests/stage11-player-a.sav", "wb");
        assert(file && fwrite(flash, 1, size, file) == size);
        fclose(file);
        free(flash);
        games[p]->reset(games[p]);
        for (unsigned f = 0; f < 720; f++) games[p]->runFrame(games[p]);
        Select(p);
        GAME0("SetSaveBlocksPointers");
        assert(GAME("LoadGameSave", SAVE_NORMAL) == SAVE_STATUS_OK);
        assert(GAME0("SoulLink_GetPairCount") == 3);
        sCore->busWrite32(sCore, Symbol("gMain") + 4, Symbol("CB2_ContinueSavedGame") | 1);
        sCore->busWrite8(sCore, Symbol("gMain") + 0x438, 0);
    }
    Frames(180);
    for (unsigned p = 0; p < 2; p++) {
        Select(p);
        assert(GAME0("IsOverworldIdle"));
        scratch[p] = GAME("AllocZeroed", 512);
        assert(scratch[p]);
        assert(Pair(p, 0).state == SOUL_LINK_PAIR_DEAD);
        for (unsigned row = 0; row < 3; row++) {
            struct SoulLinkPair restored = Pair(p, row);
            assert(!memcmp(&restored, &savedPairs[p][row], sizeof(restored)));
        }
        struct SoulLinkTeamSelection restoredTeam;
        Read(sCore->busRead32(sCore, Symbol("gSaveBlock2Ptr")) + 0xB20
            + offsetof(struct SoulLinkSaveHeader, team), &restoredTeam, sizeof(restoredTeam));
        assert(!memcmp(&restoredTeam, &savedTeams[p], sizeof(restoredTeam)));
    }
    Connect();
    assert(Sync());
    Disconnect();
    CheckGrave(0);
    CheckGrave(1);
    fprintf(stderr, "Native flash save, hard reset, continue and resynchronization passed\n");
}

static void EnableTeamRules(unsigned p)
{
    Select(p);struct SoulLinkSaveHeader header;
    Read(sCore->busRead32(sCore,Symbol("gSaveBlock2Ptr"))+0xB20,&header,sizeof(header));
    struct SoulLinkConfig config=header.config;
    config.enforcePartyPairs=1;config.typeClause=SOUL_LINK_TYPE_PRIMARY;
    Write(SCRATCH+64,&config,sizeof(config));assert(GAME("SoulLink_SetConfig",SCRATCH+64));
    if(!p)GAME0("SoulLink_SelectCurrentParty");
    Read(sCore->busRead32(sCore,Symbol("gSaveBlock2Ptr"))+0xB20,&header,sizeof(header));
    assert(header.config.enforcePartyPairs&&header.config.typeClause==SOUL_LINK_TYPE_PRIMARY);
    assert(header.team.count==(p?0:2));
}
static void ChooseTeam(unsigned first, bool toggleSecond)
{
    Select(0);struct SoulLinkSaveHeader header;
    Read(sCore->busRead32(sCore,Symbol("gSaveBlock2Ptr"))+0xB20,&header,sizeof(header));
    SoulLinkPairId ids[6];unsigned count=0;
    SoulLinkPairId target[2]={Pair(0,first).pairId,toggleSecond?Pair(0,first+1).pairId:0};
    for(unsigned i=0;i<header.team.count;i++)
        if(header.team.pairIds[i]!=target[0]&&header.team.pairIds[i]!=target[1])ids[count++]=header.team.pairIds[i];
    for(unsigned t=0;t<(toggleSecond?2u:1u);t++) {
        bool found=false;
        for(unsigned i=0;i<header.team.count;i++)found|=header.team.pairIds[i]==target[t];
        if(!found)ids[count++]=target[t];
    }
    for(unsigned i=0;i<count;i++)for(unsigned j=i+1;j<count;j++)
        if(ids[i]>ids[j]){SoulLinkPairId temp=ids[i];ids[i]=ids[j];ids[j]=temp;}
    Write(SCRATCH+64,ids,count*sizeof(*ids));assert(GAME("SoulLink_SetTeamSelection",SCRATCH+64,count));

}
static void TestAutomaticTeams(void)
{
    Gift(1,MAP_ROUTE2,SPECIES_GYARADOS,603);
    Gift(0,MAP_ROUTE3,SPECIES_VULPIX,503);
    Connect();assert(Sync());Disconnect();
    assert(Pair(0,1).state==SOUL_LINK_PAIR_ALIVE&&Pair(0,2).state==SOUL_LINK_PAIR_ALIVE);
    // B initially has a different party: put the route-2 body in its PC.
    Select(1);uint32_t mon=Symbol("gPlayerParty")+100;
    GAME("StoreHPAndStatusInBoxMon",mon);
    uint8_t body[80];Read(mon,body,80);Write(GAME("GetBoxedMonPtr",0,0),body,80);
    GAME("ZeroMonData",mon);GAME0("CompactPartySlots");GAME0("CalculatePlayerPartyCount");
    EnableTeamRules(0);EnableTeamRules(1);
    ChooseTeam(1,false); // A removes route-2, retaining route-3.
    Connect();assert(Sync());Disconnect();
    for(unsigned p=0;p<2;p++) {
        Select(p);assert(GAME0("SoulLink_ArePartyChangesLocked"));
        assert(sCore->busRead8(sCore,Symbol("gPlayerPartyCount"))==1);
        assert(GAME("GetMonData",Symbol("gPlayerParty"),MON_DATA_PERSONALITY,0)==(p?602:503));
    }
    ChooseTeam(1,true); // Select route-2 and remove route-3: both PCs supply the pair.
    Connect();assert(Sync());Disconnect();
    for(unsigned p=0;p<2;p++) {
        Select(p);assert(sCore->busRead8(sCore,Symbol("gPlayerPartyCount"))==1);
        assert(GAME("GetMonData",Symbol("gPlayerParty"),MON_DATA_PERSONALITY,0)==(p?603:502));
        struct SoulLinkSaveHeader header;
        Read(sCore->busRead32(sCore,Symbol("gSaveBlock2Ptr"))+0xB20,&header,sizeof(header));
        assert(header.team.count==1&&header.team.pairIds[0]==Pair(p,1).pairId&&header.team.locked);
    }

    for(unsigned p=0;p<2;p++)
    {
        OpenMenu(p);Press(p,128);Press(p,1);
        Shot(p,p ? "/mnt/c/Pokemon Soul Link/FRLG-Plus/build/tests/stage11-duo-live-b.png" : "/mnt/c/Pokemon Soul Link/FRLG-Plus/build/tests/stage11-duo-live-a.png");
        Press(p,16);Press(p,32);Press(p,2);Press(p,2);Frames(100);Press(p,2);Frames(60);
    }
    SaveReload();
    for (unsigned p = 0; p < 2; p++) { Select(p); assert(GAME0("SoulLink_ArePartyChangesLocked")); }
    // Only B refuses: both clients must retain and display B's mail diagnostic.
    Select(1);uint16_t heldMail=ITEM_ORANGE_MAIL;Write(SCRATCH+64,&heldMail,2);
    GAME("SetMonData",Symbol("gPlayerParty"),MON_DATA_HELD_ITEM,SCRATCH+64);
    ChooseTeam(1,true);
    Connect();assert(!Sync());Disconnect();
    for(unsigned p=0;p<2;p++) {
        Select(p);assert(GAME("SoulLink_GetTeamDiagnostic",SCRATCH+64,SCRATCH+80));
        struct SoulLinkTeamDiagnostic diagnostic;Read(SCRATCH+64,&diagnostic,sizeof(diagnostic));
        assert(diagnostic.reason==SOUL_LINK_TEAM_MAIL);
        assert(diagnostic.pairId==Pair(p,1).pairId);
        assert(sCore->busRead8(sCore,SCRATCH+80)==1);
        assert(GAME("GetMonData",Symbol("gPlayerParty"),MON_DATA_PERSONALITY,0)==(p?603:502));
    }
    Select(1);uint16_t noMail=0;Write(SCRATCH+64,&noMail,2);
    GAME("SetMonData",Symbol("gPlayerParty"),MON_DATA_HELD_ITEM,SCRATCH+64);
    ChooseTeam(1,true);
    fprintf(stderr,"Native remote mail diagnostic, retained player identity and unchanged parties passed\n");
    // A later local evolution can conflict with the partner's secondary type.
    Select(0);uint16_t evolved=SPECIES_CHARIZARD;Write(SCRATCH+64,&evolved,2);
    GAME("SetMonData",Symbol("gPlayerParty"),MON_DATA_SPECIES,SCRATCH+64);
    GAME("CalculateMonStats",Symbol("gPlayerParty"),0);
    struct SoulLinkConfig rules;uint32_t header=sCore->busRead32(sCore,Symbol("gSaveBlock2Ptr"))+0xB20;
    Read(header+offsetof(struct SoulLinkSaveHeader,config),&rules,sizeof(rules));
    rules.typeClause=SOUL_LINK_TYPE_ANY;Write(SCRATCH+64,&rules,sizeof(rules));assert(GAME("SoulLink_SetConfig",SCRATCH+64));
    assert(!GAME("SoulLink_ValidatePartyRules",SCRATCH+64));
    GAME("CreateMon",Symbol("gEnemyParty"),SPECIES_RATTATA,5,31,1,999,OT_ID_PRESET,99);
    GAME0("StartScriptedWildBattle");Frames(80);
    Shot(0,"/mnt/c/Pokemon Soul Link/FRLG-Plus/build/tests/stage11-invalid-team.png");
    for(unsigned i=0;i<5;i++)Press(0,1);
    Frames(80);Select(0);assert(GAME0("IsOverworldIdle"));
    assert(!sCore->busRead8(sCore,Symbol("gMain")+0x439));
    assert(GAME("FindTaskIdByFunc",Symbol("Task_BattleStart")|1)==0xFF);
    rules.typeClause=SOUL_LINK_TYPE_PRIMARY;Write(SCRATCH+64,&rules,sizeof(rules));assert(GAME("SoulLink_SetConfig",SCRATCH+64));
    // One side carries mail: no team moves on either side, but deaths still commit.
    Select(1);uint16_t mail=ITEM_ORANGE_MAIL;Write(SCRATCH+64,&mail,2);
    GAME("SetMonData",Symbol("gPlayerParty"),MON_DATA_HELD_ITEM,SCRATCH+64);
    Select(0);uint16_t hp=0;Write(SCRATCH+64,&hp,2);
    GAME("SetMonData",Symbol("gPlayerParty"),MON_DATA_HP,SCRATCH+64);
    Frames(160);Dismiss(0);
    Connect();assert(!Sync());Disconnect();
    for(unsigned p=0;p<2;p++) {
        Select(p);assert(GAME0("SoulLink_GetSessionStatus")==SOUL_LINK_SESSION_TEAM_CONFLICT);
        assert(Pair(p,1).state==SOUL_LINK_PAIR_DEAD);
        assert(GAME("GetMonData",Symbol("gPlayerParty"),MON_DATA_HP,0)==0);
        assert(sCore->busRead8(sCore,Symbol("gPlayerPartyCount"))==1);
    }
    puts("mGBA real cable: menu pair selection, automatic bilateral team replacement, locking and death propagation despite team refusal passed");
}

#ifndef SOUL_LINK_CABLE_NO_MAIN
int main(int argc,char **argv) {
    assert(argc==3);
    sSymbols=argv[2];
    mLogSetDefaultLogger(&sLogger);
    for(unsigned p=0;p<2;p++) {
        games[p]=mCoreFind(argv[1]);
        assert(games[p]&&games[p]->init(games[p]));
        mCoreInitConfig(games[p],"soul-link-cable-tests");
        games[p]->setVideoBuffer(games[p],pixels[p],240);
        assert(mCoreLoadFile(games[p],argv[1]));
        games[p]->reset(games[p]);
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
        Select(p);
        scratch[p]=GAME("AllocZeroed",512);
        assert(scratch[p]);
        Select(p);
        GAME("SetTrainerId",p?20:10,sCore->busRead32(sCore,Symbol("gSaveBlock2Ptr"))+0xA);
        GAME("FlagSet",FLAG_SYS_POKEMON_GET);
        GAME("FlagSet",FLAG_SYS_POKEDEX_GET);
    }
    Frames(120);
    CreateRun(0);
    CreateRun(1);
    Gift(0,MAP_ROUTE1,SPECIES_BULBASAUR,501);
    Gift(1,MAP_ROUTE1,SPECIES_RATTATA,601);
    Connect();
    assert(Sync());
    assert(Pair(0,0).state==SOUL_LINK_PAIR_ALIVE&&Pair(1,0).state==SOUL_LINK_PAIR_ALIVE);
    Gift(0,MAP_ROUTE2,SPECIES_CHARMANDER,502);
    Gift(1,MAP_ROUTE3,SPECIES_SQUIRTLE,602);
    assert(Sync());
    for(unsigned p=0;p<2;p++) {
        Select(p);
        assert(GAME0("SoulLink_GetPairCount")==3);
        assert(Pair(p,1).state==SOUL_LINK_PAIR_PENDING&&Pair(p,2).state==SOUL_LINK_PAIR_PENDING);
    }
    Disconnect();
    void *oldB=malloc(games[1]->stateSize(games[1]));
    assert(oldB&&games[1]->saveState(games[1],oldB));
    Select(0);
    uint16_t hp=0;
    Write(SCRATCH+128,&hp,2);
    GAME("SetMonData",Symbol("gPlayerParty"),MON_DATA_HP,SCRATCH+128);
    Frames(160);
    Dismiss(0);
    assert(Pair(0,0).state==SOUL_LINK_PAIR_DEAD&&Pair(1,0).state==SOUL_LINK_PAIR_ALIVE);
    Connect();
    assert(Sync());
    Disconnect();
    for(unsigned p=0;p<2;p++) {
        struct SoulLinkPair row=Pair(p,0);
        assert(row.state==SOUL_LINK_PAIR_DEAD&&row.cleanupFlags==0);
        Select(p);
        assert(sCore->busRead8(sCore,Symbol("gPlayerPartyCount"))==1);
        assert(GAME("GetMonData",Symbol("gPlayerParty"),MON_DATA_PERSONALITY,0)==(p?602:502));
    }
    assert(games[1]->loadState(games[1],oldB));
    free(oldB);
    assert(Pair(1,0).state==SOUL_LINK_PAIR_ALIVE);
    Frames(60);
    Connect();
    if(!Sync()) {
        Disconnect();
        Frames(60);
        Connect();
        assert(Sync());
    }
    Disconnect();
    assert(Pair(1,0).state==SOUL_LINK_PAIR_DEAD);
    SaveReload();
    CancelFromMenu();
    for(unsigned p=0;p<2;p++) {
        Select(p);struct SoulLinkSaveHeader before,after;
        uint32_t header=sCore->busRead32(sCore,Symbol("gSaveBlock2Ptr"))+0xB20;
        Read(header,&before,sizeof(before));
        OpenMenu(p);Shot(p,p ? "/mnt/c/Pokemon Soul Link/FRLG-Plus/build/tests/menu-home-b.png" : "/mnt/c/Pokemon Soul Link/FRLG-Plus/build/tests/menu-home-a.png");
        Press(p,128);Press(p,1);
        Shot(p,p ? "/mnt/c/Pokemon Soul Link/FRLG-Plus/build/tests/menu-duo-b.png" : "/mnt/c/Pokemon Soul Link/FRLG-Plus/build/tests/menu-duo-a.png");
        Press(p,4);Press(p,4);Press(p,1);
        Shot(p,p ? "/mnt/c/Pokemon Soul Link/FRLG-Plus/build/tests/menu-pairs-b.png" : "/mnt/c/Pokemon Soul Link/FRLG-Plus/build/tests/menu-pairs-a.png");
        Press(p,2);Press(p,128);Press(p,128);Press(p,1);Press(p,1);
        Shot(p,p ? "/mnt/c/Pokemon Soul Link/FRLG-Plus/build/tests/menu-diagnostics-b.png" : "/mnt/c/Pokemon Soul Link/FRLG-Plus/build/tests/menu-diagnostics-a.png");
        Select(p);assert(!sCore->busRead32(sCore,Symbol("sSession")));
        Read(header,&after,sizeof(after));assert(!memcmp(&before,&after,sizeof(before)));
        Press(p,2);Press(p,2);Frames(100);Press(p,2);Frames(60);
        Select(p);assert(GAME0("IsOverworldIdle"));
    }
    fprintf(stderr,"Native ROM: duo and pairs are read-only, diagnostics do not start a connection\n");
    Press(0, 8); Select(0);
    for (unsigned row = 0; row < 8; row++)
    {
        unsigned cursor = sCore->busRead8(sCore, Symbol("sStartMenuCursorPos"));
        if (sCore->busRead8(sCore, Symbol("sStartMenuOrder") + cursor) == 6) break;
        Press(0, 128);
    }
    Shot(0, "/mnt/c/Pokemon Soul Link/FRLG-Plus/build/tests/stage11-start-exit.png");
    Press(0, 1); Frames(60); Select(0); assert(GAME0("IsOverworldIdle"));
    TestAutomaticTeams();
    puts("mGBA real cable: reduced native START menu, route pairing, offline death, Soul Grave and quick-load reconciliation passed");
    for(unsigned p=0;p<2;p++) {
        mCoreConfigDeinit(&games[p]->config);
        games[p]->deinit(games[p]);
    }
    return 0;
}
#endif
