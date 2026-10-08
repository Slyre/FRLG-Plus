#ifndef GUARD_SOUL_LINK_RULES_H
#define GUARD_SOUL_LINK_RULES_H

#include "global.h"
#include "soul_link_types.h"

// Restrictions compare the two members of one pair, never different pairs.
bool8 SoulLink_ArePairTypesAllowed(u16 speciesA, u16 speciesB, u8 mode);
bool8 SoulLink_IsPartyMon(struct Pokemon *mon);
bool8 SoulLink_CanUseSpecies(struct BoxPokemon *mon, u16 species);
bool8 SoulLink_ValidatePartyRules(const struct SoulLinkConfig *config);

#endif
