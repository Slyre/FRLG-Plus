#include <assert.h>
#include <stdio.h>
#include "global.h"
#include "pokemon.h"
#include "soul_link.h"
#include "soul_link_rules.h"
#include "constants/battle.h"

static struct SaveBlock1 sBlock1;
static struct SaveBlock2 sBlock2;
static struct PokemonStorage sStorage;
struct SaveBlock1 *gSaveBlock1Ptr = &sBlock1;
struct SaveBlock2 *gSaveBlock2Ptr = &sBlock2;
struct PokemonStorage *gPokemonStoragePtr = &sStorage;
struct Pokemon gPlayerParty[PARTY_SIZE];
u8 gPlayerPartyCount;
const struct SpeciesInfo gSpeciesInfo[NUM_SPECIES] = {
    [SPECIES_CHARIZARD] = {.types = {TYPE_FIRE, TYPE_FLYING}},
    [SPECIES_GYARADOS] = {.types = {TYPE_WATER, TYPE_FLYING}},
    [SPECIES_VULPIX] = {.types = {TYPE_FIRE, TYPE_FIRE}},
    [SPECIES_PIDGEOT] = {.types = {TYPE_NORMAL, TYPE_FLYING}},
};
u16 Random(void) { return 123; }
u32 GetBoxMonData(struct BoxPokemon *mon, s32 field, u8 *dest)
{
    (void)dest;
    switch (field)
    {
    case MON_DATA_OT_ID: return mon->otId;
    case MON_DATA_PERSONALITY: return mon->personality;
    case MON_DATA_SPECIES: return mon->species;
    case MON_DATA_IS_EGG: return mon->isEgg;
    }
    assert(!"Unexpected field"); return 0;
}
u32 GetMonData(struct Pokemon *mon, s32 field, u8 *dest) { return GetBoxMonData(&mon->box, field, dest); }

int main(void)
{
    struct SoulLinkRunId run = {{1, 2}};
    u32 players[2] = {10, 20};
    struct SoulLinkConfig config;
    struct SoulLinkPair pair = {0};
    assert(SoulLink_ArePairTypesAllowed(SPECIES_CHARIZARD, SPECIES_GYARADOS, SOUL_LINK_TYPE_PRIMARY));
    assert(!SoulLink_ArePairTypesAllowed(SPECIES_CHARIZARD, SPECIES_GYARADOS, SOUL_LINK_TYPE_ANY));
    assert(!SoulLink_ArePairTypesAllowed(SPECIES_CHARIZARD, SPECIES_VULPIX, SOUL_LINK_TYPE_PRIMARY));
    assert(SoulLink_ArePairTypesAllowed(SPECIES_VULPIX, SPECIES_PIDGEOT, SOUL_LINK_TYPE_ANY));
    assert(SoulLink_ArePairTypesAllowed(SPECIES_NONE, SPECIES_NONE, SOUL_LINK_TYPE_NONE));
    assert(!SoulLink_ArePairTypesAllowed(NUM_SPECIES, SPECIES_GYARADOS, SOUL_LINK_TYPE_ANY));
    assert(!SoulLink_ArePairTypesAllowed(SPECIES_CHARIZARD, SPECIES_GYARADOS, SOUL_LINK_TYPE_COUNT));
    SoulLink_ResetSaveData(); assert(SoulLink_BeginRun(&run, players, 0));
    assert(SoulLink_GetConfig(&config)); config.enabled = TRUE; config.typeClause = SOUL_LINK_TYPE_PRIMARY;
    assert(SoulLink_SetConfig(&config));
    pair.mons[0] = (struct SoulLinkMonIdentity){10, 101, SPECIES_CHARIZARD, 1};
    pair.mons[1] = (struct SoulLinkMonIdentity){20, 201, SPECIES_GYARADOS, 1};
    pair.areaId = 1; pair.state = SOUL_LINK_PAIR_ALIVE;
    pair.encounterStates[0] = pair.encounterStates[1] = SOUL_LINK_ENCOUNTER_CAPTURED;
    SoulLinkPairId id;
    assert(SoulLink_CreatePair(&pair, &id));
    gPlayerParty[0].box = (struct BoxPokemon){.otId = 10, .personality = 101, .species = SPECIES_CHARIZARD};
    assert(SoulLink_ValidatePartyRules(&config));
    config.typeClause = SOUL_LINK_TYPE_ANY;
    assert(!SoulLink_ValidatePartyRules(&config));
    assert(SoulLink_SetConfig(&config));
    assert(!SoulLink_CanUseSpecies(&gPlayerParty[0].box, SPECIES_CHARIZARD));
    assert(SoulLink_CanUseSpecies(&gPlayerParty[0].box, SPECIES_VULPIX));
    gPlayerParty[1].box = (struct BoxPokemon){.otId = 10, .personality = 999, .species = SPECIES_CHARIZARD};
    assert(SoulLink_CanUseSpecies(&gPlayerParty[1].box, SPECIES_CHARIZARD)); // Never compare different pairs.
    gPlayerParty[0].box.isEgg = TRUE;
    assert(SoulLink_ValidatePartyRules(&config));
    config.enabled = FALSE; gPlayerParty[0].box.isEgg = FALSE;
    assert(SoulLink_ValidatePartyRules(&config));
    puts("Soul Link pair type rules: primary, any, secondary overlap, invalid species, evolution candidate and scope passed");
    return 0;
}
