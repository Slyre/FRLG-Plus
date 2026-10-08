// Compare the new indexes against the former exact pairwise validator.
#define main ExistingModelMain
#include "test_model.c"
#undef main
#include "../../src/soul_link.c"

static bool8 Reference(const struct SoulLinkPair *rows,u16 count,u8 player) {
    if(count>88||(count&&!rows))return FALSE;
    for(u16 i=0;i<count;i++) {
        if(!IsPairValidForPlayer(&rows[i],player))return FALSE;
        for(u16 j=0;j<i;j++) {
            if(rows[i].pairId&&rows[i].pairId==rows[j].pairId)return FALSE;
            for(u8 side=0;side<2;side++)
                if(rows[i].encounterStates[side]&&rows[j].encounterStates[side]
                   &&(rows[i].mons[side].encounterOrdinal==rows[j].mons[side].encounterOrdinal
                      ||(rows[i].mons[side].species&&rows[j].mons[side].species
                         &&SameMon(&rows[i].mons[side],&rows[j].mons[side]))))return FALSE;
        }
    }
    return TRUE;
}
static u32 Next(void) {static u32 state=0x34567891;state^=state<<13;state^=state>>17;state^=state<<5;return state;}
static void Fill(struct SoulLinkPair *rows,u16 count) {
    memset(rows,0,sizeof(*rows)*88);
    for(u16 i=0;i<count;i++) {
        rows[i].pairId=i+1;rows[i].areaId=10;rows[i].state=SOUL_LINK_PAIR_ALIVE;
        for(u8 side=0;side<2;side++) {
            rows[i].encounterStates[side]=SOUL_LINK_ENCOUNTER_CAPTURED;
            rows[i].mons[side]=(struct SoulLinkMonIdentity){10+side,1000+i,SPECIES_BULBASAUR,i+1};
        }
    }
}
static u8 Bucket(u32 key) {key^=key>>16;key*=0x9E3779B1u;key^=key>>16;return key;}
static void CheckInterruptedApply(void) {
    const struct SoulLinkConfig config={1,1,1,1,1,0,0,0};
    struct SoulLinkPair remote[88];
    unsigned interruptions=0;
    for(u8 role=0;role<2;role++) {
        unsigned length=0;
        for(unsigned cut=0;;cut++) {
            if(length && cut>=length)break;
            for(unsigned mutation=0;mutation<7;mutation++) {
                BeginRun(role);assert(SoulLink_SetConfig(&config));Fill(remote,87);
                GetHeader()->pairCount=87;GetHeader()->nextPairId=88;
                for(u16 i=0;i<87;i++)*GetPair(i)=remote[i];
                remote[0].encounterStates[role^1]=SOUL_LINK_ENCOUNTER_DEAD;
                remote[0].state=SOUL_LINK_PAIR_DEAD;
                struct SoulLinkApplyJob job;
                assert(SoulLink_BeginApplyJob(&job,remote,87,role^1));
                if(!length) {
                    struct SoulLinkApplyJob probe=job;u8 result=0;
                    while(!result){length++;assert(length<200);result=SoulLink_StepApplyJob(&probe);}
                    assert(result==1);
                }
                for(unsigned step=0;step<cut;step++)assert(SoulLink_StepApplyJob(&job)==0);
                struct SoulLinkMonIdentity identity=GetPair(0)->mons[role];
                switch(mutation) {
                case 0:assert(SoulLink_MarkLocalDeath(&identity));break;
                case 1:identity.personality+=999;assert(SoulLink_RecordLocalEncounter(600,&identity,SOUL_LINK_ENCOUNTER_CAPTURED));break;
                case 2:{struct SoulLinkMonIdentity evolved=identity;evolved.species=SPECIES_IVYSAUR;assert(SoulLink_ReplaceLocalIdentity(&identity,&evolved));break;}
                case 3:{struct SoulLinkConfig changed=config;changed.sameAreaPairing=0;assert(SoulLink_SetConfig(&changed));break;}
                case 4:GetPair(0)->cleanupFlags^=1<<role;break;
                case 5:{SoulLinkPairId id=1;assert(SoulLink_SetTeamSelection(&id,1));break;}
                case 6:SoulLink_ResetSaveData();break;
                }
                struct SaveBlock1 after1=sBlock1;struct SaveBlock2 after2=sBlock2;struct PokemonStorage afterStorage=sStorage;
                assert(SoulLink_StepApplyJob(&job)==3);
                assert(!memcmp(&sBlock1,&after1,sizeof(sBlock1))&&!memcmp(&sBlock2,&after2,sizeof(sBlock2))&&!memcmp(&sStorage,&afterStorage,sizeof(sStorage)));
                interruptions++;
            }
        }
    }
    printf("PASS sliced apply: %u interrupted boundaries preserve captures, deaths, evolution, rules, cleanup, team and reset\n",interruptions);
    for(u16 index=0;index<88;index++) {
        BeginRun(0);assert(SoulLink_SetConfig(&config));Fill(remote,88);GetHeader()->pairCount=88;
        for(u16 i=0;i<88;i++)*GetPair(i)=remote[i];
        struct SoulLinkApplyJob job;assert(SoulLink_BeginApplyJob(&job,remote,88,1));
        assert(SoulLink_StepApplyJob(&job)==0);
        GetPair(index)->cleanupFlags^=1;
        assert(SoulLink_StepApplyJob(&job)==3);
    }
    puts("PASS exact baseline: mutations detected in every row of all seven save storage banks");
}
#include "reference_merge.inc"
static void CheckMergeIndexes(void) {
    struct SoulLinkPair remote[88],actual[88],expected[88];
    const struct SoulLinkConfig config={1,1,1,1,1,0,0,0};
    unsigned successful[2]={0,0};
    for(unsigned trial=0;trial<4000;trial++) {
        u16 count=Next()%89;u8 player=Next()%2;
        BeginRun(player);assert(SoulLink_SetConfig(&config));Fill(remote,count);
        GetHeader()->pairCount=count;
        for(u16 i=0;i<count;i++) {
            *GetPair(i)=remote[i];
            if(player==1&&i%3==0) {
                struct SoulLinkPair *old=GetPair(i);old->pairId=0;memset(&old->mons[0],0,sizeof(old->mons[0]));
                old->encounterStates[0]=0;old->state=SOUL_LINK_PAIR_PENDING;
            }
        }
        for(unsigned edit=0;edit<4&&count;edit++) {
            u16 i=Next()%count,j=Next()%count;
            switch(Next()%6) {
            case 0:{struct SoulLinkPair swap=remote[i];remote[i]=remote[j];remote[j]=swap;break;}
            case 1:remote[i].pairId+=100;break;
            case 2:remote[i].mons[player].personality++;break;
            case 3:remote[i].encounterStates[player]=SOUL_LINK_ENCOUNTER_DEAD;remote[i].state=SOUL_LINK_PAIR_DEAD;remote[i].cleanupFlags|=1<<player;break;
            case 4:remote[i].mons[1].encounterOrdinal=remote[j].mons[1].encounterOrdinal;break;
            case 5:remote[i].areaId++;break;
            }
        }
        assert(CanCommitRemoteState(remote,count)==ReferenceCanCommit(remote,count));
        if(player==1) {
            u16 a=99,b=99;
            bool8 accepted=PrepareCoordinatorState(remote,count,actual,&a);
            assert(accepted==ReferenceCoordinator(remote,count,expected,&b));
            if(accepted)assert(a==b&&!memcmp(actual,expected,a*sizeof(*actual)));
        }
        {
            struct SaveBlock1 before1=sBlock1;
            struct SaveBlock2 before2=sBlock2;
            struct PokemonStorage beforeStorage=sStorage;
            u16 mergedCount=0;
            bool8 staged=SoulLink_PrepareRemoteState(remote,count,player^1,expected,&mergedCount);
            if(staged)successful[player]++;
            if(staged)assert(SoulLink_CommitRemoteState(expected,mergedCount));
            struct SaveBlock1 after1=sBlock1;
            struct SaveBlock2 after2=sBlock2;
            struct PokemonStorage afterStorage=sStorage;
            sBlock1=before1;sBlock2=before2;sStorage=beforeStorage;
            assert(SoulLink_ApplyRemoteState(remote,count,player^1,actual)==staged);
            assert(!memcmp(&sBlock1,&after1,sizeof(sBlock1)));
            assert(!memcmp(&sBlock2,&after2,sizeof(sBlock2)));
            assert(!memcmp(&sStorage,&afterStorage,sizeof(sStorage)));
            assert(!SoulLink_ApplyRemoteState(remote,count,player^1,remote));
            sBlock1=before1;sBlock2=before2;sStorage=beforeStorage;
            struct SoulLinkApplyJob job;
            assert(SoulLink_BeginApplyJob(&job,remote,count,player^1));
            u8 result=0;unsigned steps=0;
            while(!result) {
                assert(++steps<200);
                result=SoulLink_StepApplyJob(&job);
                if(!result) {
                    assert(!memcmp(&sBlock1,&before1,sizeof(sBlock1)));
                    assert(!memcmp(&sBlock2,&before2,sizeof(sBlock2)));
                    assert(!memcmp(&sStorage,&beforeStorage,sizeof(sStorage)));
                }
            }
            assert(result==(staged?1:2));
            assert(!memcmp(&sBlock1,&after1,sizeof(sBlock1)));
            assert(!memcmp(&sBlock2,&after2,sizeof(sBlock2)));
            assert(!memcmp(&sStorage,&afterStorage,sizeof(sStorage)));
        }
    }
    assert(successful[0]>50&&successful[1]>50);
    // Every key hashes to the same slot, but must resolve to its own row.
    Fill(remote,88);
    u16 found=0;
    for(u32 key=1;key<=65535&&found<88;key++) if(PairIndexSlot(key)==7) {
        remote[found].pairId=key;remote[found].mons[1].encounterOrdinal=key;found++;
    }
    assert(found==88);BeginRun(1);assert(SoulLink_SetConfig(&config));GetHeader()->pairCount=88;
    for(u16 i=0;i<88;i++)*GetPair(i)=remote[i];
    u16 a=0,b=0;
    assert(PrepareCoordinatorState(remote,88,actual,&a));
    assert(ReferenceCoordinator(remote,88,expected,&b));
    assert(a==b&&!memcmp(actual,expected,a*sizeof(*actual)));
    assert(CanCommitRemoteState(remote,88)&&ReferenceCanCommit(remote,88));
    puts("PASS merge indexes: 4000 prior-validator comparisons, shuffled rows, pending candidates, missing IDs, stale identities, deaths and area mismatch");
    puts("PASS synchronous apply: 4000 byte-exact staged-path comparisons and input/output alias rejection");
    puts("PASS sliced apply: 4000 byte-exact staged-path comparisons, with no partial save writes");
}
int main(void) {
    struct SoulLinkPair rows[88];
    assert(IsTableValid(NULL,0,0));assert(!IsTableValid(NULL,1,0));assert(!IsTableValid(rows,89,0));
    Fill(rows,88);
    u16 n=0;
    for(u32 key=1;key<=65535&&n<88;key++) if(Bucket(key)==7)rows[n++].pairId=key;
    assert(n==88&&IsTableValid(rows,88,0));
    n=0;
    for(u32 key=1;n<88;key++)if(Bucket(10^(key*33u))==7)rows[n++].mons[0].personality=key;
    assert(IsTableValid(rows,88,0));
    rows[87].mons[0].otId=rows[0].mons[0].otId;
    rows[87].mons[0].personality=rows[0].mons[0].personality;
    assert(!IsTableValid(rows,88,0));
    for(unsigned trial=0;trial<10000;trial++) {
        u16 count=Next()%89;u8 player=Next()%2;Fill(rows,count);
        for(unsigned edit=0;edit<8&&count;edit++) {
            u16 i=Next()%count,j=Next()%count;u8 side=Next()%2;
            switch(Next()%8) {
            case 0:rows[i].pairId=rows[j].pairId;break;
            case 1:rows[i].mons[side].encounterOrdinal=rows[j].mons[side].encounterOrdinal;break;
            case 2:rows[i].mons[side].otId=rows[j].mons[side].otId;rows[i].mons[side].personality=rows[j].mons[side].personality;break;
            case 3:rows[i].cleanupFlags=Next()%8;break;
            case 4:rows[i].mons[side].personality=Next();break;
            case 5:{struct SoulLinkPair swap=rows[i];rows[i]=rows[j];rows[j]=swap;break;}
            case 6:rows[i].mons[side].species=SPECIES_NONE;rows[i].encounterStates[side]=SOUL_LINK_ENCOUNTER_FAILED;rows[i].state=SOUL_LINK_PAIR_FAILED;break;
            case 7:rows[i].pairId=0;memset(&rows[i].mons[0],0,sizeof(rows[i].mons[0]));rows[i].encounterStates[0]=0;rows[i].state=GetPairState(&rows[i]);break;
            }
        }
        assert(IsTableValid(rows,count,player)==Reference(rows,count,player));
        struct SoulLinkApplyJob validationJob;
        bool8 slicedValid=TRUE;
        for(u16 i=0;i<count&&slicedValid;i++)slicedValid=ApplyJobValidRow(&validationJob,rows,i,player);
        assert(slicedValid==Reference(rows,count,player));
    }
    puts("PASS exact table index: 10000 differential cases, 88-way collisions, candidates, terminal states and duplicate identities");
    CheckMergeIndexes();
    CheckInterruptedApply();
}
