#if defined(SOUL_LINK_FOUR_PLAYERS) && SOUL_LINK_FOUR_PLAYERS
#include "global.h"
#include "soul_link_group_flash.h"
#include <string.h>
struct Span { u8 sector; u16 offset, size; };
// Existing v3 reserve locations and unused tails; production integration must
// retain save.c's struct-offset assertions when enabling this mapping.
static const struct Span spans[] = {
    {0, 0xB20, 1024}, {0, 0xF24, 92}, {0, 3968, 116},
    {1, 0x59C, 64}, {1, 3968, 116},
    {2, 3968, 116}, {3, 3968, 116},
    {4, 0x2F18 - 3*3968, 64}, {4, 0x2F58 - 3*3968, 32},
    {4, 0x348C - 3*3968, 160}, {4, 0x3A94 - 3*3968, 32},
    {4, 0x3D68 - 3*3968, 152}, {4, 3968, 116},
    {5, 3968, 116}, {6, 3968, 116}, {7, 3968, 116},
    {8, 3968, 116}, {9, 3968, 116}, {10, 3968, 116},
    {11, 3968, 116}, {12, 3968, 116},
    {13, 0x83D0 - 8*3968, 1504}, {13, 0x89B0 - 8*3968, 464}, {13, 3968, 116},
};
static bool8 Transfer(u8 *out, const u8 *in, u32 size, u8 logical, bool8 writing)
{
    u32 position=0, index;
    if (!out || !in || size!=SOUL_LINK_GROUP_SAVE_BYTES || logical>=14) return FALSE;
    for(index=0;index<sizeof(spans)/sizeof(spans[0]);index++)
    {
        const struct Span *span=&spans[index];
        u32 count=position<size ? size-position : 0;
        if(count>span->size) count=span->size;
        if(span->sector==logical && count)
        {
            if(writing) memcpy(out+span->offset,in+position,count);
            else memcpy(out+position,in+span->offset,count);
        }
        position+=span->size;
    }
    return position==SOUL_LINK_GROUP_FLASH_CAPACITY;
}
bool8 SoulLinkGroupFlash_Write(const u8 *codec,u32 size,u8 logical,u8 *sector)
{ return Transfer(sector,codec,size,logical,TRUE); }
bool8 SoulLinkGroupFlash_Read(u8 *codec,u32 size,u8 logical,const u8 *sector)
{ return Transfer(codec,sector,size,logical,FALSE); }
#endif
