#include "global.h"
#include "constants/pokemon.h"
#define TOTAL_BOXES_COUNT 14
#define IN_BOX_COUNT 30
u32 GetMonData(struct Pokemon *mon, s32 field, u8 *dest);
u32 GetBoxMonData(struct BoxPokemon *mon, s32 field, u8 *dest);
void SetBoxMonData(struct BoxPokemon *mon, s32 field, const void *value);
void ZeroMonData(struct Pokemon *mon);
void ZeroBoxMonData(struct BoxPokemon *mon);
u8 CalculatePlayerPartyCount(void);
u16 SpeciesToNationalPokedexNum(u16 species);
struct BoxPokemon *GetBoxedMonPtr(u8 box, u8 pos);
u8 *GetBoxNamePtr(u8 box);
void ZeroBoxMonAt(u8 box, u8 pos);
s16 CompactPartySlots(void);
