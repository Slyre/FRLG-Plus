#if defined(SOUL_LINK_FOUR_PLAYERS) && SOUL_LINK_FOUR_PLAYERS
#include "global.h"
#include "soul_link_group.h"

#include <string.h>
#include "constants/species.h"

typedef char GroupRowSize[(sizeof(struct SoulLinkGroup) == 60) ? 1 : -1];
typedef char GroupHeaderSize[(sizeof(struct SoulLinkGroupHeader) == 112) ? 1 : -1];
typedef char GroupSaveSize[(sizeof(struct SoulLinkGroupSave) == 5392) ? 1 : -1];

static u16 Read16(const u8 *p) { return p[0] | (p[1] << 8); }
static u32 Read32(const u8 *p) { return Read16(p) | ((u32)Read16(p + 2) << 16); }
static void Write16(u8 *p, u16 v) { p[0] = v; p[1] = v >> 8; }
static void Write32(u8 *p, u32 v) { Write16(p, v); Write16(p + 2, v >> 16); }
static bool8 Zero(const void *p, u32 n)
{
    const u8 *b = p;
    while (n--) if (*b++) return FALSE;
    return TRUE;
}
static bool8 RunSet(const struct SoulLinkRunId *r) { return r->words[0] || r->words[1]; }
static bool8 SameMon(const struct SoulLinkMonIdentity *a, const struct SoulLinkMonIdentity *b)
{ return a->otId == b->otId && a->personality == b->personality; }
static bool8 ConfigValid(const struct SoulLinkConfig *c)
{
    return c->enabled <= 1 && c->linkedDeaths <= 1 && c->sameAreaPairing <= 1
        && c->firstEncounterOnly <= 1 && c->dupesClause <= 1 && c->typeClause < SOUL_LINK_TYPE_COUNT
        && c->enforcePartyPairs <= 1 && !c->reserved;
}
static bool8 ParticipantsValid(const u32 *ids)
{
    u8 a, b;
    for (a = 0; a < 4; a++)
    {
        if (!ids[a]) return FALSE;
        for (b = 0; b < a; b++) if (ids[a] == ids[b]) return FALSE;
    }
    return TRUE;
}
static bool8 NameValid(const u8 *name)
{
    u8 i;
    for (i = 0; i < 8; i++)
    {
        if (name[i] == 0xFF) return i != 0;
        // Do not accept embedded Gen III text control codes or newlines.
        if (name[i] >= 0xFA) return FALSE;
    }
    return FALSE;
}
u16 SoulLinkGroup_Key(u16 area, u8 round, bool8 sameArea)
{
    if ((sameArea && !area) || area > SOUL_LINK_AREA_STARTER || !round || round > 88) return 0;
    if (area == SOUL_LINK_AREA_STARTER) return round == 1 ? (area << 7) | 1 : 0;
    return sameArea ? (area << 7) | round : 0x8000 | round;
}
static u8 GroupState(const struct SoulLinkGroup *g)
{
    u8 p;
    bool8 failed = FALSE, complete = TRUE;
    for (p = 0; p < 4; p++)
    {
        if (g->encounterStates[p] == SOUL_LINK_ENCOUNTER_DEAD) return SOUL_LINK_PAIR_DEAD;
        if (g->encounterStates[p] == SOUL_LINK_ENCOUNTER_FAILED) failed = TRUE;
        if (g->encounterStates[p] != SOUL_LINK_ENCOUNTER_CAPTURED) complete = FALSE;
    }
    return failed ? SOUL_LINK_PAIR_FAILED : complete ? SOUL_LINK_PAIR_ALIVE : SOUL_LINK_PAIR_PENDING;
}
static bool8 MonValid(const struct SoulLinkMonIdentity *m, u8 state)
{
    if (state == SOUL_LINK_ENCOUNTER_NONE) return Zero(m, sizeof(*m));
    if (state > SOUL_LINK_ENCOUNTER_DEAD || !m->encounterOrdinal) return FALSE;
    if (state == SOUL_LINK_ENCOUNTER_FAILED) return !m->otId && !m->personality && !m->species;
    return m->species && m->species < NUM_SPECIES && m->species != SPECIES_EGG;
}
static bool8 RowValid(const struct SoulLinkGroup *g, const struct SoulLinkConfig *config)
{
    u8 p;
    bool8 any = FALSE;
    if (g->groupId != SoulLinkGroup_Key(g->areaId, g->groupId & 127, config->sameAreaPairing)
     || !g->groupId || (g->cleanupFlags & ~15) || g->state != GroupState(g)) return FALSE;
    if ((config->firstEncounterOnly && config->sameAreaPairing && (g->groupId & 127) != 1)
     || (!config->sameAreaPairing && g->areaId != 0 && g->areaId != SOUL_LINK_AREA_STARTER)) return FALSE;
    for (p = 0; p < 4; p++)
    {
        if (!MonValid(&g->mons[p], g->encounterStates[p])) return FALSE;
        if ((g->cleanupFlags & (1 << p)) && g->encounterStates[p] != SOUL_LINK_ENCOUNTER_DEAD) return FALSE;
        if (config->linkedDeaths && g->state == SOUL_LINK_PAIR_DEAD && g->encounterStates[p] == SOUL_LINK_ENCOUNTER_CAPTURED) return FALSE;
        if (g->encounterStates[p] != SOUL_LINK_ENCOUNTER_NONE) any = TRUE;
    }
    return any;
}
bool8 SoulLinkGroup_Validate(const struct SoulLinkGroupSave *s)
{
    u16 i, j;
    u8 p;
    const struct SoulLinkGroupHeader *h;
    if (!s) return FALSE;
    h = &s->header;
    if (h->magic != SOUL_LINK_GROUP_MAGIC || h->version != 4 || h->areaVersion != SOUL_LINK_AREA_VERSION
     || !RunSet(&h->runId) || !ParticipantsValid(h->participantIds) || h->participantCount != 4
     || h->localPlayer >= 4 || h->groupCount > 88 || !ConfigValid(&h->config)
     || h->knownNameMask & ~15 || (h->graveBox != 0xFF && h->graveBox >= 14)
     || h->teamCount > 6 || h->teamLocked > 1 || h->reserved) return FALSE;
    for (p = 0; p < 4; p++)
        if ((h->knownNameMask & (1 << p)) && (!NameValid(h->trainerNames[p])
         || !h->trainerLanguages[p] || h->trainerLanguages[p] > 7 || h->trainerLanguages[p] == 6)) return FALSE;
    for (i = 0; i < h->groupCount; i++)
    {
        const struct SoulLinkGroup *g = &s->groups[i];
        if (!RowValid(g, &h->config) || (i && s->groups[i - 1].groupId >= g->groupId)) return FALSE;
        if (h->nextLocalOrdinal && g->mons[h->localPlayer].encounterOrdinal >= h->nextLocalOrdinal) return FALSE;
        for (j = 0; j < i; j++)
            for (p = 0; p < 4; p++)
                if (g->encounterStates[p] && s->groups[j].encounterStates[p]
                 && (g->mons[p].encounterOrdinal == s->groups[j].mons[p].encounterOrdinal
                  || (g->mons[p].species && s->groups[j].mons[p].species && SameMon(&g->mons[p], &s->groups[j].mons[p])))) return FALSE;
    }
    for (i = 0; i < 6; i++)
    {
        if (i >= h->teamCount) { if (h->teamIds[i]) return FALSE; continue; }
        if (!h->teamIds[i] || (i && h->teamIds[i - 1] >= h->teamIds[i])) return FALSE;
        for (j = 0; j < h->groupCount && s->groups[j].groupId != h->teamIds[i]; j++) {}
        if (j == h->groupCount) return FALSE;
    }
    return TRUE;
}
u8 SoulLinkGroup_Initialize(struct SoulLinkGroupSave *s, const struct SoulLinkRunId *run,
    const u32 participants[4], u8 local, const struct SoulLinkConfig *config)
{
    if (!s || !run || !participants || !config || !RunSet(run) || !ParticipantsValid(participants)
     || local >= 4 || !ConfigValid(config)) return SOUL_LINK_GROUP_INVALID;
    memset(s, 0, sizeof(*s));
    s->header.magic = SOUL_LINK_GROUP_MAGIC; s->header.version = 4; s->header.areaVersion = SOUL_LINK_AREA_VERSION;
    s->header.runId = *run; memcpy(s->header.participantIds, participants, 16);
    memset(s->header.trainerNames, 0xFF, 32);
    s->header.config = *config; s->header.localPlayer = local; s->header.participantCount = 4;
    s->header.nextLocalOrdinal = 1; s->header.graveBox = 0xFF;
    return SOUL_LINK_GROUP_OK;
}
static u32 Crc32(const u8 *b, u32 size)
{
    u32 crc = 0xFFFFFFFFu;
    u8 bit;
    while (size--)
    {
        crc ^= *b++;
        for (bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return ~crc;
}
static void EncodeRow(const struct SoulLinkGroup *g, u8 *b)
{
    u8 p;
    for (p = 0; p < 4; p++)
    {
        Write32(b + 12*p, g->mons[p].otId); Write32(b + 12*p+4, g->mons[p].personality);
        Write16(b + 12*p+8, g->mons[p].species); Write16(b + 12*p+10, g->mons[p].encounterOrdinal);
    }
    Write16(b+48, g->groupId); Write16(b+50, g->areaId);
    b[52] = 0;
    for (p = 0; p < 4; p++) b[52] |= g->encounterStates[p] << (2*p);
    b[53] = g->cleanupFlags;
}
static void DecodeRow(const u8 *b, struct SoulLinkGroup *g)
{
    u8 p;
    memset(g, 0, sizeof(*g));
    for (p = 0; p < 4; p++)
    {
        g->mons[p].otId = Read32(b+12*p); g->mons[p].personality = Read32(b+12*p+4);
        g->mons[p].species = Read16(b+12*p+8); g->mons[p].encounterOrdinal = Read16(b+12*p+10);
        g->encounterStates[p] = (b[52] >> (2*p)) & 3;
    }
    g->groupId = Read16(b+48); g->areaId = Read16(b+50); g->cleanupFlags = b[53]; g->state = GroupState(g);
}
u8 SoulLinkGroup_Encode(const struct SoulLinkGroupSave *s, u8 *b, u32 size)
{
    const struct SoulLinkGroupHeader *h;
    u16 i;
    if (!b || size != SOUL_LINK_GROUP_SAVE_BYTES || !SoulLinkGroup_Validate(s)) return SOUL_LINK_GROUP_INVALID;
    h = &s->header; memset(b, 0, size);
    Write32(b, h->magic); Write16(b+4, h->version); Write16(b+6, h->areaVersion);
    for (i = 0; i < 2; i++) Write32(b+8+4*i, h->runId.words[i]);
    for (i = 0; i < 4; i++) Write32(b+16+4*i, h->participantIds[i]);
    memcpy(b+32, h->trainerNames, 32); memcpy(b+64, h->trainerLanguages, 4); memcpy(b+68, &h->config, 8);
    Write32(b+76, h->configRevision);
    for (i = 0; i < 6; i++) Write16(b+80+2*i, h->teamIds[i]);
    Write32(b+92, h->teamRevision); Write16(b+96, h->groupCount); Write16(b+98, h->nextLocalOrdinal);
    b[100]=h->localPlayer; b[101]=h->participantCount; b[102]=h->knownNameMask; b[103]=h->graveBox;
    b[104]=h->teamCount; b[105]=h->teamLocked; Write16(b+106, h->reserved); Write32(b+108, h->agreementRevision);
    for (i = 0; i < h->groupCount; i++) EncodeRow(&s->groups[i], b+112+54*i);
    Write32(b+size-4, Crc32(b, size-4));
    return SOUL_LINK_GROUP_OK;
}
u8 SoulLinkGroup_Decode(const u8 *b, u32 size, struct SoulLinkGroupSave *dst, struct SoulLinkGroupSave *s)
{
    struct SoulLinkGroupHeader *h;
    u16 i;
    if (!b || !dst || !s || dst == s || size != SOUL_LINK_GROUP_SAVE_BYTES) return SOUL_LINK_GROUP_INVALID;
    if (Crc32(b, size-4) != Read32(b+size-4)) return SOUL_LINK_GROUP_CORRUPT;
    memset(s, 0, sizeof(*s)); h = &s->header;
    h->magic=Read32(b); h->version=Read16(b+4); h->areaVersion=Read16(b+6);
    for (i = 0; i < 2; i++) h->runId.words[i]=Read32(b+8+4*i);
    for (i = 0; i < 4; i++) h->participantIds[i]=Read32(b+16+4*i);
    memcpy(h->trainerNames,b+32,32); memcpy(h->trainerLanguages,b+64,4); memcpy(&h->config,b+68,8);
    h->configRevision=Read32(b+76);
    for (i=0;i<6;i++) h->teamIds[i]=Read16(b+80+2*i);
    h->teamRevision=Read32(b+92); h->groupCount=Read16(b+96); h->nextLocalOrdinal=Read16(b+98);
    h->localPlayer=b[100]; h->participantCount=b[101]; h->knownNameMask=b[102]; h->graveBox=b[103];
    h->teamCount=b[104]; h->teamLocked=b[105]; h->reserved=Read16(b+106); h->agreementRevision=Read32(b+108);
    if (h->groupCount > 88) return SOUL_LINK_GROUP_INVALID;
    for(i=0;i<h->groupCount;i++) DecodeRow(b+112+54*i,&s->groups[i]);
    if (!Zero(b+112+54*h->groupCount,54*(88-h->groupCount)) || !SoulLinkGroup_Validate(s)) return SOUL_LINK_GROUP_INVALID;
    *dst=*s; return SOUL_LINK_GROUP_OK;
}
static void Propagate(struct SoulLinkGroupSave *s)
{
    u16 i;
    u8 p;
    for (i=0;i<s->header.groupCount;i++)
    {
        struct SoulLinkGroup *g=&s->groups[i];
        if (s->header.config.linkedDeaths && GroupState(g)==SOUL_LINK_PAIR_DEAD)
            for(p=0;p<4;p++) if(g->encounterStates[p]==SOUL_LINK_ENCOUNTER_CAPTURED)
            {
                g->encounterStates[p]=SOUL_LINK_ENCOUNTER_DEAD;
                if(p==s->header.localPlayer) g->cleanupFlags|=1<<p;
            }
        g->state=GroupState(g);
    }
}
static bool8 ContextMatches(const struct SoulLinkGroupSave *s,const struct SoulLinkRunId *run,const u32 *ids)
{ return !memcmp(&s->header.runId,run,sizeof(*run)) && !memcmp(s->header.participantIds,ids,16); }
u8 SoulLinkGroup_MergeOwner(struct SoulLinkGroupSave *save,const struct SoulLinkGroupOwnerSnapshot *snap,
    u8 owner,struct SoulLinkGroupSave *s)
{
    u16 i,j,k;
    if (!save || !snap || !s || s==save || owner>=4 || snap->owner!=owner || snap->count>88
     || !SoulLinkGroup_Validate(save)) return SOUL_LINK_GROUP_INVALID;
    if (!ContextMatches(save,&snap->runId,snap->participantIds)
     || memcmp(&save->header.config,&snap->config,8) || save->header.configRevision!=snap->configRevision) return SOUL_LINK_GROUP_CONTEXT;
    if (snap->hasTrainerName>1 || (snap->hasTrainerName && (!NameValid(snap->trainerName)
     || !snap->language || snap->language>7 || snap->language==6))) return SOUL_LINK_GROUP_INVALID;
    *s=*save;
    for(i=0;i<snap->count;i++)
    {
        const struct SoulLinkGroupObservation *o=&snap->observations[i];
        u16 id=SoulLinkGroup_Key(o->areaId,o->areaRound,s->header.config.sameAreaPairing);
        struct SoulLinkGroup *g;
        u8 before;
        if(!id || !o->state || !MonValid(&o->mon,o->state)) return SOUL_LINK_GROUP_INVALID;
        for(k=0;k<i;k++) if(SoulLinkGroup_Key(snap->observations[k].areaId,snap->observations[k].areaRound,s->header.config.sameAreaPairing)==id) return SOUL_LINK_GROUP_INVALID;
        for(j=0;j<s->header.groupCount && s->groups[j].groupId<id;j++) {}
        if(j==s->header.groupCount || s->groups[j].groupId!=id)
        {
            if(s->header.groupCount==88) return SOUL_LINK_GROUP_CAPACITY_ERROR;
            for(k=s->header.groupCount;k>j;k--) s->groups[k]=s->groups[k-1];
            memset(&s->groups[j],0,sizeof(s->groups[j])); s->header.groupCount++;
            s->groups[j].groupId=id;
            s->groups[j].areaId=s->header.config.sameAreaPairing || o->areaId==SOUL_LINK_AREA_STARTER ? o->areaId : 0;
        }
        g=&s->groups[j]; before=g->encounterStates[owner];
        if(before && (g->mons[owner].encounterOrdinal!=o->mon.encounterOrdinal
         || ((before==SOUL_LINK_ENCOUNTER_FAILED)!=(o->state==SOUL_LINK_ENCOUNTER_FAILED))
         || (g->mons[owner].species && !SameMon(&g->mons[owner],&o->mon)))) return SOUL_LINK_GROUP_IDENTITY;
        if(before!=SOUL_LINK_ENCOUNTER_DEAD && before!=SOUL_LINK_ENCOUNTER_FAILED)
        {
            g->mons[owner]=o->mon; g->encounterStates[owner]=o->state;
            if(o->state==SOUL_LINK_ENCOUNTER_DEAD && owner==s->header.localPlayer) g->cleanupFlags|=1<<owner;
        }
        if(owner==s->header.localPlayer && s->header.nextLocalOrdinal && o->mon.encounterOrdinal>=s->header.nextLocalOrdinal)
            s->header.nextLocalOrdinal=o->mon.encounterOrdinal+1;
    }
    if(snap->hasTrainerName)
    {
        memcpy(s->header.trainerNames[owner],snap->trainerName,8); s->header.trainerLanguages[owner]=snap->language;
        s->header.knownNameMask|=1<<owner;
    }
    Propagate(s);
    if(!SoulLinkGroup_Validate(s)) return SOUL_LINK_GROUP_INVALID;
    *save=*s; return SOUL_LINK_GROUP_OK;
}
u32 SoulLinkGroup_RegistryDigest(const struct SoulLinkGroupSave *s)
{
    u32 hash=2166136261u;
    u16 i;
    u8 b[54],j;
    if(!SoulLinkGroup_Validate(s)) return 0;
    for(i=0;i<s->header.groupCount;i++)
    {
        EncodeRow(&s->groups[i],b); b[53]=0;
        for(j=0;j<54;j++) hash=(hash^b[j])*16777619u;
    }
    return hash;
}
bool8 SoulLinkGroup_TypesAllowed(const struct SoulLinkGroup *g,u8 mode,SoulLinkGroupTypes types,void *ctx)
{
    u8 p,q,a,b,t[4][2];
    if(!g || mode>=SOUL_LINK_TYPE_COUNT) return FALSE;
    if(mode==SOUL_LINK_TYPE_NONE) return TRUE;
    if(!types) return FALSE;
    for(p=0;p<4;p++)
    {
        if(!g->mons[p].species || !types(ctx,g->mons[p].species,t[p])) return FALSE;
        for(q=0;q<p;q++) for(a=0;a<(mode==SOUL_LINK_TYPE_PRIMARY?1:2);a++)
            for(b=0;b<(mode==SOUL_LINK_TYPE_PRIMARY?1:2);b++) if(t[p][a]==t[q][b]) return FALSE;
    }
    return TRUE;
}
u8 SoulLinkGroup_ValidateTeamCommand(const struct SoulLinkGroupSave *s,const struct SoulLinkGroupTeamCommand *c,
    SoulLinkGroupTypes types,SoulLinkGroupTeamPreflight preflight,void *ctx)
{
    u8 i;
    u16 j;
    if(!s || !c || !SoulLinkGroup_Validate(s) || c->reserved || !c->count || c->count>6) return SOUL_LINK_GROUP_INVALID;
    if(c->approvedMask!=15 || c->readyMask!=15) return SOUL_LINK_GROUP_UNANIMOUS;
    if(!ContextMatches(s,&c->runId,c->participantIds) || c->configRevision!=s->header.configRevision
     || c->registryDigest!=SoulLinkGroup_RegistryDigest(s) || c->expectedTeamRevision!=s->header.teamRevision
     || c->agreementRevision<=s->header.agreementRevision || s->header.teamRevision==0xFFFFFFFFu) return SOUL_LINK_GROUP_CONTEXT;
    for(i=0;i<6;i++)
    {
        if(i>=c->count) { if(c->groupIds[i]) return SOUL_LINK_GROUP_INVALID; continue; }
        if(!c->groupIds[i] || (i && c->groupIds[i-1]>=c->groupIds[i])) return SOUL_LINK_GROUP_INVALID;
        for(j=0;j<s->header.groupCount && s->groups[j].groupId!=c->groupIds[i];j++) {}
        if(j==s->header.groupCount || s->groups[j].state!=SOUL_LINK_PAIR_ALIVE) return SOUL_LINK_GROUP_TEAM;
        if(!SoulLinkGroup_TypesAllowed(&s->groups[j],s->header.config.typeClause,types,ctx)) return SOUL_LINK_GROUP_TYPES;
    }
    return preflight && preflight(ctx,c->groupIds,c->count) ? SOUL_LINK_GROUP_OK : SOUL_LINK_GROUP_TEAM;
}

u8 SoulLinkGroup_MigrateV3(const struct SoulLinkSaveHeader *old,const struct SoulLinkPair *rows,
    const struct SoulLinkRunId *run,const u32 ids[4],const u8 map[2],struct SoulLinkGroupSave *dst,
    struct SoulLinkGroupSave *s)
{
    u16 i,j,k;
    u8 p,anchor;
    if(!old || !rows || !run || !ids || !map || !dst || !s || s==dst || map[0]>=4 || map[1]>=4 || map[0]==map[1]
     || old->magic!=SOUL_LINK_SAVE_MAGIC || old->version<1 || old->version>3 || old->areaVersion!=SOUL_LINK_AREA_VERSION
     || old->localPlayer>1 || old->pairCount>88 || !RunSet(&old->runId) || !memcmp(run,&old->runId,sizeof(*run))
     || ids[map[0]]!=old->participantIds[0] || ids[map[1]]!=old->participantIds[1]) return SOUL_LINK_GROUP_INVALID;
    if(SoulLinkGroup_Initialize(s,run,ids,map[old->localPlayer],&old->config)) return SOUL_LINK_GROUP_INVALID;
    s->header.configRevision=old->configRevision; s->header.graveBox=old->graveBox;
    for(i=0;i<old->pairCount;i++)
    {
        u8 rank=1;
        struct SoulLinkGroup g;
        if(!rows[i].areaId || rows[i].areaId>SOUL_LINK_AREA_STARTER || rows[i].cleanupFlags&~3) return SOUL_LINK_GROUP_INVALID;
        // An anchor observed every encounter in this area/order. Without that
        // evidence, a unilateral legacy record cannot be assigned a safe rank.
        for(anchor=0;anchor<2;anchor++)
        {
            for(j=0;j<old->pairCount;j++)
                if((rows[j].areaId==rows[i].areaId || (!old->config.sameAreaPairing
                  && rows[j].areaId!=SOUL_LINK_AREA_STARTER && rows[i].areaId!=SOUL_LINK_AREA_STARTER))
                 && !rows[j].encounterStates[anchor]) break;
            if(j==old->pairCount) break;
        }
        if(anchor==2) return SOUL_LINK_GROUP_MIGRATION_AMBIGUOUS;
        for(j=0;j<old->pairCount;j++)
            if(j!=i && (rows[j].areaId==rows[i].areaId || (!old->config.sameAreaPairing
             && rows[j].areaId!=SOUL_LINK_AREA_STARTER && rows[i].areaId!=SOUL_LINK_AREA_STARTER)))
            {
                bool8 earlier=rows[j].mons[anchor].encounterOrdinal<rows[i].mons[anchor].encounterOrdinal;
                if(rows[j].mons[anchor].encounterOrdinal==rows[i].mons[anchor].encounterOrdinal) return SOUL_LINK_GROUP_INVALID;
                p=anchor^1;
                if(rows[j].encounterStates[p] && rows[i].encounterStates[p]
                 && earlier!=(rows[j].mons[p].encounterOrdinal<rows[i].mons[p].encounterOrdinal)) return SOUL_LINK_GROUP_MIGRATION_AMBIGUOUS;
                if(earlier) rank++;
            }
        if(old->config.firstEncounterOnly && old->config.sameAreaPairing && rank>1) return SOUL_LINK_GROUP_MIGRATION_AMBIGUOUS;
        memset(&g,0,sizeof(g));g.areaId=old->config.sameAreaPairing || rows[i].areaId==SOUL_LINK_AREA_STARTER ? rows[i].areaId : 0;
        g.groupId=SoulLinkGroup_Key(g.areaId,rank,old->config.sameAreaPairing);
        for(p=0;p<2;p++)
        {
            g.mons[map[p]]=rows[i].mons[p];g.encounterStates[map[p]]=rows[i].encounterStates[p];
            if(rows[i].cleanupFlags&(1<<p)) g.cleanupFlags|=1<<map[p];
        }
        g.state=GroupState(&g);
        if(g.state!=rows[i].state && !(rows[i].state==SOUL_LINK_PAIR_ALIVE && g.state==SOUL_LINK_PAIR_PENDING)) return SOUL_LINK_GROUP_INVALID;
        for(j=0;j<s->header.groupCount && s->groups[j].groupId<g.groupId;j++) {}
        for(k=s->header.groupCount;k>j;k--) s->groups[k]=s->groups[k-1];
        s->groups[j]=g;s->header.groupCount++;
        if(s->header.nextLocalOrdinal && g.mons[s->header.localPlayer].encounterOrdinal>=s->header.nextLocalOrdinal)
            s->header.nextLocalOrdinal=g.mons[s->header.localPlayer].encounterOrdinal+1;
    }
    Propagate(s);
    if(!SoulLinkGroup_Validate(s)) return SOUL_LINK_GROUP_INVALID;
    *dst=*s;return SOUL_LINK_GROUP_OK;
}
#endif
