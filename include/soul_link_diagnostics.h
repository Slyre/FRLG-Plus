#ifndef GUARD_SOUL_LINK_DIAGNOSTICS_H
#define GUARD_SOUL_LINK_DIAGNOSTICS_H
#include "soul_link_types.h"

enum SoulLinkTeamIssue
{
    SOUL_LINK_TEAM_OK,
    SOUL_LINK_TEAM_SELECTION,
    SOUL_LINK_TEAM_INVALID_PAIR,
    SOUL_LINK_TEAM_MISSING,
    SOUL_LINK_TEAM_DUPLICATE,
    SOUL_LINK_TEAM_DAYCARE,
    SOUL_LINK_TEAM_GRAVE,
    SOUL_LINK_TEAM_EGG,
    SOUL_LINK_TEAM_FAINTED,
    SOUL_LINK_TEAM_MAIL,
    SOUL_LINK_TEAM_TYPES,
    SOUL_LINK_TEAM_PC_SPACE,
    SOUL_LINK_TEAM_REVISION,
    SOUL_LINK_TEAM_UNKNOWN,
    SOUL_LINK_TEAM_ISSUE_COUNT
};

struct SoulLinkTeamDiagnostic
{
    SoulLinkPairId pairId;
    u16 species;
    u8 reason;
    u8 neededSlots;
    u8 availableSlots;
    u8 reserved;
};
#endif
