#ifndef GUARD_SOUL_LINK_TEAM_H
#define GUARD_SOUL_LINK_TEAM_H
#include "global.h"
#include "soul_link_types.h"
#include "soul_link_diagnostics.h"

// Allocated with the cable session; preparation never changes Pokémon storage.
struct SoulLinkTeamPlan
{
    struct Pokemon party[PARTY_SIZE];
    struct Pokemon deposits[PARTY_SIZE];
    struct BoxPokemon *sources[PARTY_SIZE];
    struct BoxPokemon *destinations[PARTY_SIZE];
    SoulLinkPairId ids[PARTY_SIZE];
    u8 count, depositCount;
    struct SoulLinkTeamDiagnostic diagnostic;
};
bool8 SoulLink_PrepareTeamChange(void *context, const SoulLinkPairId *ids, u8 count);
void SoulLink_CommitTeamChange(void *context);
void SoulLink_SelectCurrentParty(void);
void SoulLink_DescribeTeamChange(void *context, struct SoulLinkTeamDiagnostic *diagnostic);
#endif
