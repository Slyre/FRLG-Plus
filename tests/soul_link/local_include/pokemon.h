#ifndef TEST_SOUL_LINK_POKEMON_H
#define TEST_SOUL_LINK_POKEMON_H
#include "pokemon_storage_system.h"
struct SpeciesInfo { u8 baseStats[6]; u8 types[2]; };
extern const struct SpeciesInfo gSpeciesInfo[];
#endif
