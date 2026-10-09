#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "global.h"
#include "soul_link_group.h"
#include "constants/species.h"

static struct SoulLinkGroupSave state, scratch, decoded, previous;
static u8 bytes[SOUL_LINK_GROUP_SAVE_BYTES];
static const u32 participants[4]={10,20,30,40};
static const struct SoulLinkRunId run={{100,200}};
static const struct SoulLinkConfig config={1,1,1,1,1,0,1,0};
static struct SoulLinkGroupOwnerSnapshot snap;

static void Reset(void)
{
    assert(SoulLinkGroup_Initialize(&state,&run,participants,0,&config)==0);
    assert(SoulLinkGroup_Validate(&state));
}
static void Snapshot(u8 owner,u16 area,u8 round,u8 encounter)
{
    memset(&snap,0,sizeof(snap)); snap.runId=state.header.runId;
    memcpy(snap.participantIds,state.header.participantIds,16);
    snap.owner=owner;snap.config=state.header.config;snap.configRevision=state.header.configRevision;snap.count=1;
    snap.observations[0].areaId=area;snap.observations[0].areaRound=round;snap.observations[0].state=encounter;
    snap.observations[0].mon=(struct SoulLinkMonIdentity){participants[owner],1000+owner,SPECIES_BULBASAUR+owner,1};
    if(encounter==SOUL_LINK_ENCOUNTER_FAILED)
        snap.observations[0].mon=(struct SoulLinkMonIdentity){0,0,0,1};
}
static void Add(u8 owner,u16 area,u8 round,u8 encounter)
{
    Snapshot(owner,area,round,encounter);
    assert(SoulLinkGroup_MergeOwner(&state,&snap,owner,&scratch)==0);
}
static void Quartet(void)
{
    u8 p;Reset();for(p=0;p<4;p++) Add(p,8,1,SOUL_LINK_ENCOUNTER_CAPTURED);
    assert(state.groups[0].state==SOUL_LINK_PAIR_ALIVE);
}
static void TestMerge(void)
{
    u8 death,p,order[4];unsigned permutation;
    for(death=0;death<4;death++)
    {
        Quartet();Snapshot(death,8,1,SOUL_LINK_ENCOUNTER_DEAD);
        assert(!SoulLinkGroup_MergeOwner(&state,&snap,death,&scratch));
        for(p=0;p<4;p++) assert(state.groups[0].encounterStates[p]==SOUL_LINK_ENCOUNTER_DEAD);
        assert(state.groups[0].cleanupFlags==1);
        previous=state;Add(death,8,1,SOUL_LINK_ENCOUNTER_CAPTURED);assert(!memcmp(&previous,&state,sizeof(state)));
        Reset();Add(death,8,1,SOUL_LINK_ENCOUNTER_DEAD);
        for(p=0;p<4;p++) if(p!=death) Add(p,8,1,SOUL_LINK_ENCOUNTER_CAPTURED);
        for(p=0;p<4;p++) assert(state.groups[0].encounterStates[p]==SOUL_LINK_ENCOUNTER_DEAD);
    }
    for(permutation=0;permutation<24;permutation++)
    {
        unsigned k=permutation,n=4,i,j;u8 available[4]={0,1,2,3};
        for(i=0;i<4;i++) {unsigned index=k%n;k/=n;order[i]=available[index];for(j=index;j<n-1;j++)available[j]=available[j+1];n--;}
        Reset();for(p=0;p<4;p++) Add(order[p],8,1,SOUL_LINK_ENCOUNTER_CAPTURED);
        if(!permutation) previous=state;else assert(!memcmp(&previous,&state,sizeof(state)));
    }
    Quartet();Snapshot(2,8,1,SOUL_LINK_ENCOUNTER_CAPTURED);previous=state;
    snap.observations[0].mon.personality++;
    assert(SoulLinkGroup_MergeOwner(&state,&snap,2,&scratch)==SOUL_LINK_GROUP_IDENTITY);
    assert(!memcmp(&previous,&state,sizeof(state)));
    Snapshot(2,8,1,SOUL_LINK_ENCOUNTER_CAPTURED);snap.owner=1;
    assert(SoulLinkGroup_MergeOwner(&state,&snap,2,&scratch)==SOUL_LINK_GROUP_INVALID);
    Snapshot(2,8,1,SOUL_LINK_ENCOUNTER_CAPTURED);snap.runId.words[0]++;
    assert(SoulLinkGroup_MergeOwner(&state,&snap,2,&scratch)==SOUL_LINK_GROUP_CONTEXT);
    assert(!memcmp(&previous,&state,sizeof(state)));
    Reset();Add(0,8,1,SOUL_LINK_ENCOUNTER_FAILED);Add(1,8,1,SOUL_LINK_ENCOUNTER_CAPTURED);
    assert(state.groups[0].state==SOUL_LINK_PAIR_FAILED && state.groups[0].encounterStates[1]==SOUL_LINK_ENCOUNTER_CAPTURED);
    previous=state;Snapshot(0,8,1,SOUL_LINK_ENCOUNTER_CAPTURED);
    assert(SoulLinkGroup_MergeOwner(&state,&snap,0,&scratch)==SOUL_LINK_GROUP_IDENTITY);
    assert(!memcmp(&previous,&state,sizeof(state)));
    Reset();state.header.config.sameAreaPairing=0;
    for(p=0;p<4;p++) Add(3-p,8+p,1,SOUL_LINK_ENCOUNTER_CAPTURED);
    assert(state.groups[0].groupId==0x8001 && state.groups[0].areaId==0);
    previous=state;Reset();state.header.config.sameAreaPairing=0;
    for(p=0;p<4;p++) Add(p,11-p,1,SOUL_LINK_ENCOUNTER_CAPTURED);
    assert(!memcmp(&previous,&state,sizeof(state)));
    Snapshot(0,SOUL_LINK_AREA_STARTER,1,SOUL_LINK_ENCOUNTER_CAPTURED);snap.observations[0].mon.personality++;snap.observations[0].mon.encounterOrdinal=2;
    assert(!SoulLinkGroup_MergeOwner(&state,&snap,0,&scratch));assert(state.header.groupCount==2);
    puts("PASS native4 model: four death directions, late captures, all24 owner orders, identity/auth/context rejection, failed catches, FIFO/starter isolation");
}
static void TestCodec(void)
{
    u32 i,bit;
    Quartet();state.header.knownNameMask=15;
    for(i=0;i<4;i++){memset(state.header.trainerNames[i],0xFF,8);state.header.trainerNames[i][0]=0xBB+i;state.header.trainerLanguages[i]=2;}
    assert(!SoulLinkGroup_Encode(&state,bytes,sizeof(bytes)));
    assert(!SoulLinkGroup_Decode(bytes,sizeof(bytes),&decoded,&scratch));assert(!memcmp(&state,&decoded,sizeof(state)));
    previous=decoded;
    for(i=0;i<sizeof(bytes);i++) for(bit=1;bit<256;bit<<=1)
    {
        bytes[i]^=bit;
        assert(SoulLinkGroup_Decode(bytes,sizeof(bytes),&decoded,&scratch)==SOUL_LINK_GROUP_CORRUPT);
        assert(!memcmp(&previous,&decoded,sizeof(decoded)));bytes[i]^=bit;
    }
    assert(SoulLinkGroup_Decode(bytes,sizeof(bytes)-1,&decoded,&scratch)==SOUL_LINK_GROUP_INVALID);
    assert(SoulLinkGroup_Decode(bytes,sizeof(bytes)+1,&decoded,&scratch)==SOUL_LINK_GROUP_INVALID);
    assert(SoulLinkGroup_Decode(bytes,sizeof(bytes),&decoded,&decoded)==SOUL_LINK_GROUP_INVALID);
    Reset();state.header.config.firstEncounterOnly=0;Snapshot(0,8,1,SOUL_LINK_ENCOUNTER_CAPTURED);snap.count=88;
    for(i=0;i<88;i++)snap.observations[i]=(struct SoulLinkGroupObservation){.mon={10,1000+i,SPECIES_BULBASAUR,1+i},.areaId=8,.areaRound=1+i,.state=SOUL_LINK_ENCOUNTER_CAPTURED};
    assert(!SoulLinkGroup_MergeOwner(&state,&snap,0,&scratch));assert(state.header.groupCount==88);
    assert(!SoulLinkGroup_Encode(&state,bytes,sizeof(bytes)));assert(!SoulLinkGroup_Decode(bytes,sizeof(bytes),&decoded,&scratch));
    assert(!memcmp(&state,&decoded,sizeof(state)));previous=state;
    Snapshot(0,9,1,SOUL_LINK_ENCOUNTER_CAPTURED);snap.observations[0].mon.encounterOrdinal=89;snap.observations[0].mon.personality=2000;
    assert(SoulLinkGroup_MergeOwner(&state,&snap,0,&scratch)==SOUL_LINK_GROUP_CAPACITY_ERROR);assert(!memcmp(&state,&previous,sizeof(state)));
    puts("PASS native4 codec: exact4868-byte LE roundtrip,38944 single-bit corruptions, size/alias rejection,88groups capacity, no partial writes");
}
static bool8 Types(void *ctx,u16 species,u8 types[2])
{
    (void)ctx;types[0]=species;types[1]=species+10;return TRUE;
}
static bool8 RejectTypes(void *ctx,u16 species,u8 types[2])
{
    Types(ctx,species,types);types[1]=30;return TRUE;
}
static unsigned preflights;
static bool8 Preflight(void *ctx,const u16 *ids,u8 count)
{ (void)ctx;assert(ids[0]&&count);preflights++;return TRUE; }
static bool8 RejectPreflight(void *ctx,const u16 *ids,u8 count)
{ Preflight(ctx,ids,count);return FALSE; }
static void TestTeams(void)
{
    struct SoulLinkGroupTeamCommand command;
    unsigned mask;
    Quartet();state.header.config.typeClause=SOUL_LINK_TYPE_ANY;
    memset(&command,0,sizeof(command));command.runId=state.header.runId;memcpy(command.participantIds,participants,16);
    command.registryDigest=SoulLinkGroup_RegistryDigest(&state);command.agreementRevision=1;command.count=1;command.groupIds[0]=state.groups[0].groupId;
    command.approvedMask=command.readyMask=15;
    assert(SoulLinkGroup_ValidateTeamCommand(&state,&command,Types,Preflight,NULL)==0);
    assert(SoulLinkGroup_ValidateTeamCommand(&state,&command,RejectTypes,Preflight,NULL)==SOUL_LINK_GROUP_TYPES);
    assert(SoulLinkGroup_ValidateTeamCommand(&state,&command,Types,RejectPreflight,NULL)==SOUL_LINK_GROUP_TEAM);
    for(mask=0;mask<15;mask++)
    {
        unsigned old=preflights;command.approvedMask=mask;
        assert(SoulLinkGroup_ValidateTeamCommand(&state,&command,Types,Preflight,NULL)==SOUL_LINK_GROUP_UNANIMOUS);assert(preflights==old);
        command.approvedMask=15;command.readyMask=mask;
        assert(SoulLinkGroup_ValidateTeamCommand(&state,&command,Types,Preflight,NULL)==SOUL_LINK_GROUP_UNANIMOUS);assert(preflights==old);command.readyMask=15;
    }
    command.expectedTeamRevision++;assert(SoulLinkGroup_ValidateTeamCommand(&state,&command,Types,Preflight,NULL)==SOUL_LINK_GROUP_CONTEXT);command.expectedTeamRevision--;
    command.agreementRevision=0;assert(SoulLinkGroup_ValidateTeamCommand(&state,&command,Types,Preflight,NULL)==SOUL_LINK_GROUP_CONTEXT);command.agreementRevision=1;
    Add(3,8,1,SOUL_LINK_ENCOUNTER_DEAD);assert(SoulLinkGroup_ValidateTeamCommand(&state,&command,Types,Preflight,NULL)==SOUL_LINK_GROUP_CONTEXT);
    command.registryDigest=SoulLinkGroup_RegistryDigest(&state);assert(SoulLinkGroup_ValidateTeamCommand(&state,&command,Types,Preflight,NULL)==SOUL_LINK_GROUP_TEAM);
    puts("PASS native4 teams: all4 approvals+readiness, six pairwise type comparisons, pure local preflight, stale revision/digest/death refusal");
}
static void TestMigration(void)
{
    struct SoulLinkSaveHeader old;struct SoulLinkPair rows[2];u8 map[2]={1,3};
    const u32 newParticipants[4]={30,10,40,20};struct SoulLinkRunId newRun={{300,400}};
    memset(&old,0,sizeof(old));memset(rows,0,sizeof(rows));old.magic=SOUL_LINK_SAVE_MAGIC;old.version=3;old.areaVersion=SOUL_LINK_AREA_VERSION;
    old.runId=run;old.participantIds[0]=10;old.participantIds[1]=20;old.config=config;old.pairCount=2;old.graveBox=13;
    old.team.count=1;old.team.locked=1;old.team.pairIds[0]=2;old.team.revision=44;
    rows[0]=(struct SoulLinkPair){.mons={{10,100,SPECIES_BULBASAUR,1},{20,200,SPECIES_CHARMANDER,1}},.pairId=1,.areaId=8,
        .encounterStates={SOUL_LINK_ENCOUNTER_DEAD,SOUL_LINK_ENCOUNTER_DEAD},.state=SOUL_LINK_PAIR_DEAD,.cleanupFlags=1};
    rows[1]=(struct SoulLinkPair){.mons={{10,101,SPECIES_BULBASAUR,2},{20,201,SPECIES_CHARMANDER,2}},.pairId=2,.areaId=9,
        .encounterStates={SOUL_LINK_ENCOUNTER_CAPTURED,SOUL_LINK_ENCOUNTER_CAPTURED},.state=SOUL_LINK_PAIR_ALIVE};
    assert(!SoulLinkGroup_MigrateV3(&old,rows,&newRun,newParticipants,map,&decoded,&scratch));
    assert(decoded.header.localPlayer==1 && decoded.header.groupCount==2 && !decoded.header.teamLocked && !decoded.header.teamCount && !decoded.header.agreementRevision);
    assert(decoded.groups[0].encounterStates[1]==SOUL_LINK_ENCOUNTER_DEAD && decoded.groups[0].encounterStates[3]==SOUL_LINK_ENCOUNTER_DEAD);
    assert(decoded.groups[0].encounterStates[0]==0 && decoded.groups[0].cleanupFlags==2);
    assert(decoded.groups[1].state==SOUL_LINK_PAIR_PENDING && decoded.groups[1].mons[1].personality==101);
    assert(!SoulLinkGroup_Encode(&decoded,bytes,sizeof(bytes)));previous=decoded;
    assert(SoulLinkGroup_MigrateV3(&old,rows,&run,newParticipants,map,&decoded,&scratch)==SOUL_LINK_GROUP_INVALID);assert(!memcmp(&decoded,&previous,sizeof(decoded)));
    old.config.firstEncounterOnly=0;rows[1].areaId=8;rows[0].mons[1]=(struct SoulLinkMonIdentity){0};rows[0].encounterStates[1]=0;
    rows[1].mons[0]=(struct SoulLinkMonIdentity){0};rows[1].encounterStates[0]=0;rows[1].state=SOUL_LINK_PAIR_PENDING;
    assert(SoulLinkGroup_MigrateV3(&old,rows,&newRun,newParticipants,map,&decoded,&scratch)==SOUL_LINK_GROUP_MIGRATION_AMBIGUOUS);
    assert(!memcmp(&decoded,&previous,sizeof(decoded)));
    puts("PASS native4 migration: explicit newrun+slotmap, terminal preservation, cleared2p approvals, rejected same-run/ambiguous chronology without writes");
}
int main(void)
{
    TestMerge();TestCodec();TestTeams();TestMigration();
    printf("native4 foundation only: runtime=%u disk=%u; production integration disabled\n",(unsigned)sizeof(state),SOUL_LINK_GROUP_SAVE_BYTES);
    return 0;
}
