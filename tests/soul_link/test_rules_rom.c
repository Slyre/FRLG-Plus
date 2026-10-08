// Storage and rules use the actual encrypted Pokemon routines in the compiled ROM.
#define main PreviousRegressionMain
#include "test_rom.c"
#undef main
#include "constants/battle.h"

static uint32_t party, plan, sb2, box;
static struct SoulLinkConfig config = {1,1,1,1,1,SOUL_LINK_TYPE_PRIMARY,1,0};
static void SetupRules(void)
{
    sCore->busWrite32(sCore, Symbol("gSaveBlock1Ptr"), Symbol("gSaveBlock1"));
    sCore->busWrite32(sCore, Symbol("gSaveBlock2Ptr"), Symbol("gSaveBlock2"));
    sCore->busWrite32(sCore, Symbol("gPokemonStoragePtr"), Symbol("gPokemonStorage"));
    sb2 = Symbol("gSaveBlock2"); party = Symbol("gPlayerParty");
    CALL0("ResetPokemonStorageSystem"); CALL0("ZeroPlayerPartyMons"); CALL0("SoulLink_ResetSaveData");
    struct SoulLinkRunId run = {{123,456}}; uint32_t players[2]={10,20};
    Write(SCRATCH,&run,sizeof(run)); Write(SCRATCH+16,players,sizeof(players));
    assert(CALL("SoulLink_BeginRun",SCRATCH,SCRATCH+16,0));
    Write(SCRATCH+32,&config,sizeof(config)); assert(CALL("SoulLink_SetConfig",SCRATCH+32));
    CALL("CreateMon",party,SPECIES_CHARIZARD,40,31,1,101,OT_ID_PRESET,10);
    CALL("CreateMon",party+100,SPECIES_BULBASAUR,5,31,1,102,OT_ID_PRESET,10);
    CALL0("CalculatePlayerPartyCount");
    struct SoulLinkPair pair={0};
    pair.pairId=1; pair.areaId=1; pair.state=SOUL_LINK_PAIR_ALIVE;
    pair.encounterStates[0]=pair.encounterStates[1]=SOUL_LINK_ENCOUNTER_CAPTURED;
    pair.mons[0]=(struct SoulLinkMonIdentity){10,101,SPECIES_CHARIZARD,1};
    pair.mons[1]=(struct SoulLinkMonIdentity){20,201,SPECIES_GYARADOS,1};
    Write(SCRATCH+128,&pair,sizeof(pair)); assert(CALL("SoulLink_StorePair",SCRATCH+128));
    pair.pairId=2; pair.areaId=2;
    pair.mons[0]=(struct SoulLinkMonIdentity){10,103,SPECIES_VULPIX,2};
    pair.mons[1]=(struct SoulLinkMonIdentity){20,203,SPECIES_PIDGEOT,2};
    Write(SCRATCH+128,&pair,sizeof(pair)); assert(CALL("SoulLink_StorePair",SCRATCH+128));
    box=CALL("GetBoxedMonPtr",0,0);
    CALL("CreateMon",SCRATCH+256,SPECIES_VULPIX,8,31,1,103,OT_ID_PRESET,10);
    uint16_t item=ITEM_POTION; uint8_t pp=3; Write(SCRATCH+64,&item,2);
    CALL("SetMonData",SCRATCH+256,MON_DATA_HELD_ITEM,SCRATCH+64);
    Write(SCRATCH+64,&pp,1);CALL("SetMonData",SCRATCH+256,MON_DATA_PP1,SCRATCH+64);
    CALL("StoreHPAndStatusInBoxMon",SCRATCH+256);
    uint8_t body[80]; Read(SCRATCH+256,body,80); Write(box,body,80);
    SoulLinkPairId id=1; Write(SCRATCH+96,&id,2); assert(CALL("SoulLink_SetTeamSelection",SCRATCH+96,1));
}
static void TestTeamStorage(void)
{
    SetupRules();
    uint8_t original[600], boxes[33600], after[600];
    Read(party,original,sizeof(original)); Read(box,boxes,sizeof(boxes));
    uint16_t ids[2]={1,2}; Write(SCRATCH+96,ids,4);
    assert(CALL("SoulLink_PrepareTeamChange",plan,SCRATCH+96,2));
    Read(party,after,sizeof(after)); assert(!memcmp(original,after,sizeof(after)));
    uint8_t boxAfter[33600]; Read(box,boxAfter,sizeof(boxAfter)); assert(!memcmp(boxes,boxAfter,sizeof(boxes)));
    CALL("SoulLink_CommitTeamChange",plan);
    assert(CALL0("SoulLink_ArePartyChangesLocked"));
    assert(CALL("GetMonData",party,MON_DATA_PERSONALITY,0)==101);
    assert(CALL("GetMonData",party+100,MON_DATA_PERSONALITY,0)==103);
    assert(CALL("GetMonData",party+100,MON_DATA_PP1,0)==3);
    assert(CALL("GetMonData",party+100,MON_DATA_HELD_ITEM,0)==ITEM_POTION);
    assert(CALL("GetBoxMonData",box,MON_DATA_PERSONALITY,0)==102); // Vacated source reused.
    assert(sCore->busRead8(sCore,Symbol("gPlayerPartyCount"))==2);
    assert(CALL("SoulLink_ValidatePartyRules",SCRATCH+32)); // Different pairs can share FIRE.
    // Changes of order preserve membership.
    uint8_t first[100],second[100];Read(party,first,100);Read(party+100,second,100);
    Write(party,second,100);Write(party+100,first,100);assert(CALL("SoulLink_ValidatePartyRules",SCRATCH+32));
    ids[0]=2;Write(SCRATCH+96,ids,2);assert(CALL("SoulLink_SetTeamSelection",SCRATCH+96,1));
    assert(!CALL("SoulLink_ValidatePartyRules",SCRATCH+32)); // Pending selection requires sync.
    assert(CALL("SoulLink_PrepareTeamChange",plan,SCRATCH+96,1));CALL("SoulLink_CommitTeamChange",plan);
    assert(sCore->busRead8(sCore,Symbol("gPlayerPartyCount"))==1);
    assert(CALL("GetMonData",party,MON_DATA_PERSONALITY,0)==103);
    assert(CALL("SoulLink_ValidatePartyRules",SCRATCH+32));
    puts("mGBA teams: pure preflight, automatic withdraw/deposit, slot reuse, identity roster, order and same-type distinct pairs passed");
}
static struct SoulLinkTeamDiagnostic Diagnostic(void)
{
    struct SoulLinkTeamDiagnostic diagnostic;
    CALL("SoulLink_DescribeTeamChange",plan,SCRATCH+72);
    Read(SCRATCH+72,&diagnostic,sizeof(diagnostic));
    return diagnostic;
}
static void TestFailures(void)
{
    SetupRules();
    uint8_t original[600],after[600],body[80];Read(party,original,600);
    uint16_t id=2;Write(SCRATCH+96,&id,2);
    // One available source slot cannot store two outgoing party Pokemon.
    Read(box,body,80);
    for(unsigned b=0;b<14;b++)for(unsigned s=0;s<30;s++) {
        uint32_t dest=CALL("GetBoxedMonPtr",b,s);
        CALL("CreateMon",SCRATCH+256,SPECIES_RATTATA,5,31,1,1000+b*30+s,OT_ID_PRESET,10);
        uint8_t filler[80];Read(SCRATCH+256,filler,80);Write(dest,filler,80);
    }
    Write(box,body,80);
    assert(!CALL("SoulLink_PrepareTeamChange",plan,SCRATCH+96,1));
    assert(Diagnostic().reason == SOUL_LINK_TEAM_PC_SPACE);
    assert(Diagnostic().neededSlots == 2 && Diagnostic().availableSlots == 1);
    Read(party,after,600);assert(!memcmp(original,after,600));
    SetupRules();id=1;Write(SCRATCH+96,&id,2);
    uint16_t mail=ITEM_ORANGE_MAIL;Write(SCRATCH+64,&mail,2);CALL("SetMonData",party,MON_DATA_HELD_ITEM,SCRATCH+64);
    assert(!CALL("SoulLink_PrepareTeamChange",plan,SCRATCH+96,1));
    assert(Diagnostic().reason == SOUL_LINK_TEAM_MAIL && Diagnostic().pairId == 1);
    SetupRules();id=2;Write(SCRATCH+96,&id,2);
    Write(CALL("GetBoxedMonPtr",0,1),body,80); // Duplicate OT/PID.
    assert(!CALL("SoulLink_PrepareTeamChange",plan,SCRATCH+96,1));
    assert(Diagnostic().reason == SOUL_LINK_TEAM_DUPLICATE && Diagnostic().pairId == 2);
    SetupRules();id=1;Write(SCRATCH+96,&id,2);
    config.typeClause=SOUL_LINK_TYPE_ANY;Write(SCRATCH+32,&config,sizeof(config));assert(CALL("SoulLink_SetConfig",SCRATCH+32));
    assert(!CALL("SoulLink_ValidatePartyRules",SCRATCH+32));
    assert(!CALL("SoulLink_PrepareTeamChange",plan,SCRATCH+96,1));
    assert(Diagnostic().reason == SOUL_LINK_TEAM_TYPES && Diagnostic().pairId == 1);
    assert(!CALL("SoulLink_ArePairTypesAllowed",SPECIES_CHARIZARD,SPECIES_GYARADOS,SOUL_LINK_TYPE_ANY));
    assert(CALL("SoulLink_ArePairTypesAllowed",SPECIES_CHARIZARD,SPECIES_GYARADOS,SOUL_LINK_TYPE_PRIMARY));
    config.typeClause=SOUL_LINK_TYPE_PRIMARY;
    puts("mGBA teams: full PC, mail, duplicate identity and secondary type conflict refuse before mutation passed");
}
static void RenderMenu(void)
{
    for(unsigned frame=0;frame<8;frame++)
    {
        CALL0("CB2_SoulLinkMenuMain"); CALL0("ProcessDma3Requests");
        CALL0("CopyBufferedValuesToGpuRegs"); CALL0("VBlankCB_SoulLinkMenu");
        sCore->runFrame(sCore);
    }
}
static void TestEvolutionAndCapture(void)
{
    SetupRules();
    CALL0("ZeroPlayerPartyMons");CALL("CreateMon",party,SPECIES_CHARMELEON,36,31,1,101,OT_ID_PRESET,10);
    // Gyarados is WATER/FLYING: Charmeleon's FIRE allowed, evolved FIRE/FLYING refused in ANY.
    config.typeClause=SOUL_LINK_TYPE_ANY;Write(SCRATCH+32,&config,sizeof(config));assert(CALL("SoulLink_SetConfig",SCRATCH+32));
    assert(CALL("GetEvolutionTargetSpecies",party,EVO_MODE_NORMAL,0)==SPECIES_NONE);
    config.typeClause=SOUL_LINK_TYPE_PRIMARY;Write(SCRATCH+32,&config,sizeof(config));assert(CALL("SoulLink_SetConfig",SCRATCH+32));
    assert(CALL("GetEvolutionTargetSpecies",party,EVO_MODE_NORMAL,0)==SPECIES_CHARIZARD);
    uint16_t id=1;Write(SCRATCH+96,&id,2);assert(CALL("SoulLink_PrepareTeamChange",plan,SCRATCH+96,1));CALL("SoulLink_CommitTeamChange",plan);
    CALL("CreateMon",SCRATCH+256,SPECIES_SQUIRTLE,5,31,1,104,OT_ID_PRESET,10);
    assert(CALL("GiveMonToPlayer",SCRATCH+256)==MON_GIVEN_TO_PC);
    assert(sCore->busRead8(sCore,Symbol("gPlayerPartyCount"))==1);
    // Storage withdraw denied before touching the cursor/moving body.
    assert(!CALL("SoulLink_CanChangeStorageParty",1));
    puts("mGBA rules: evolution candidate refused/allowed, locked captures routed to PC and withdraw guard passed");
    CALL0("CB2_SoulLinkMenu");
    for (unsigned frame = 0; frame < 32; frame++) { CALL0("CB2_SoulLinkMenuMain"); CALL0("VBlankCB_SoulLinkMenu"); }
    MenuKey(128); MenuKey(128); MenuKey(1);
    MenuKey(4);
    assert(sCore->busRead8(sCore, sb2 + 0xB20 + offsetof(struct SoulLinkSaveHeader, team) + 12) == 0);
    MenuKey(4);
    assert(sCore->busRead8(sCore, sb2 + 0xB20 + offsetof(struct SoulLinkSaveHeader, team) + 12) == 1);
    RenderMenu();
    Screenshot("/mnt/c/Pokemon Soul Link/FRLG-Plus/build/tests/stage11-active-pair.png");
    puts("mGBA UI: SELECT toggles the chosen pair and IN TEAM indicator renders passed");
    MenuKey(2);
    const uint16_t species[5]={SPECIES_BULBASAUR,SPECIES_CHARMANDER,SPECIES_SQUIRTLE,SPECIES_VULPIX,SPECIES_RATTATA};
    for(unsigned i=0;i<5;i++) CALL("CreateMon",party+(i+1)*100,species[i],5,31,1,105+i,OT_ID_PRESET,10);
    for(unsigned i=0;i<4;i++){MenuKey(128);}
    MenuKey(1);
    RenderMenu();
    Screenshot("/mnt/c/Pokemon Soul Link/FRLG-Plus/build/tests/stage11-duo-page1.png");
    MenuKey(16);
    RenderMenu();
    Screenshot("/mnt/c/Pokemon Soul Link/FRLG-Plus/build/tests/stage11-duo-page2.png");
    MenuKey(2);for(unsigned i=0;i<3;i++){MenuKey(128);}
    MenuKey(1);
    RenderMenu();
    Screenshot("/mnt/c/Pokemon Soul Link/FRLG-Plus/build/tests/stage11-check-ready.png");
    MenuKey(2);
    uint16_t mail=ITEM_ORANGE_MAIL;Write(SCRATCH+64,&mail,2);CALL("SetMonData",party,MON_DATA_HELD_ITEM,SCRATCH+64);
    for(unsigned i=0;i<3;i++){MenuKey(128);}
    MenuKey(1);
    RenderMenu();
    Screenshot("/mnt/c/Pokemon Soul Link/FRLG-Plus/build/tests/stage11-check-mail.png");
    MenuKey(2);uint16_t none=ITEM_NONE;Write(SCRATCH+64,&none,2);CALL("SetMonData",party,MON_DATA_HELD_ITEM,SCRATCH+64);
    // Reverse role and body to check the actual local/partner column mapping.
    sCore->busWrite8(sCore,sb2+0xB20+offsetof(struct SoulLinkSaveHeader,localPlayer),1);
    CALL("CreateMon",party,SPECIES_GYARADOS,40,31,1,201,OT_ID_PRESET,20);
    for(unsigned i=0;i<4;i++){MenuKey(128);}
    MenuKey(1);
    RenderMenu();
    Screenshot("/mnt/c/Pokemon Soul Link/FRLG-Plus/build/tests/stage11-duo-player-b.png");
    MenuKey(2);MenuKey(2);
    for(unsigned f=0;f<40&&sCore->busRead32(sCore,Symbol("sSoulLinkMenu"));f++){CALL0("CB2_SoulLinkMenuMain");CALL0("VBlankCB_SoulLinkMenu");}
    assert(!sCore->busRead32(sCore,Symbol("sSoulLinkMenu")));
    uint32_t allocation=CALL("Alloc",90000);assert(allocation);CALL("Free",allocation);
    puts("mGBA UI: six-slot duo pages, both player orientations, ready/mail diagnostics and icon cleanup passed");
}
int main(int argc,char **argv)
{
    assert(argc==3);sSymbols=argv[2];mLogSetDefaultLogger(&sLogger);
    sCore=mCoreFind(argv[1]);assert(sCore&&sCore->init(sCore));mCoreInitConfig(sCore,"soul-link-rules");
    sCore->setVideoBuffer(sCore,sPixels,240);assert(mCoreLoadFile(sCore,argv[1]));sCore->reset(sCore);
    for(unsigned f=0;f<720;f++)sCore->runFrame(sCore);
    CALL("InitHeap",Symbol("gHeap"),0x1C000);sScratch=CALL("AllocZeroed",1024);plan=CALL("AllocZeroed",1600);assert(sScratch&&plan);
    TestTeamStorage();TestFailures();TestEvolutionAndCapture();
    mCoreConfigDeinit(&sCore->config);sCore->deinit(sCore);return 0;
}
