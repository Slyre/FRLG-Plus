#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "global.h"
#include "soul_link_group_flash.h"
static u8 sectors[14][4096], original[14][4096], codec[SOUL_LINK_GROUP_SAVE_BYTES], decoded[SOUL_LINK_GROUP_SAVE_BYTES];
static bool8 Allowed(u8 sector,u32 offset)
{
    if(offset>=3968 && offset<4084) return TRUE;
    if(sector==0) return (offset>=0xB20 && offset<0xF20) || (offset>=0xF24 && offset<3968);
    if(sector==1) return offset>=0x59C && offset<0x5DC;
    if(sector==4) {
        u32 full=offset+3*3968;
        return (full>=0x2F18 && full<0x2F78) || (full>=0x348C && full<0x352C)
            || (full>=0x3A94 && full<0x3AB4) || (full>=0x3D68 && offset<3968);
    }
    if(sector==13) return offset>=0x83D0-8*3968 && offset<3968;
    return FALSE;
}
int main(void)
{
    u32 i,j;
    memset(sectors,0xA5,sizeof(sectors));memcpy(original,sectors,sizeof(sectors));
    for(i=0;i<sizeof(codec);i++) codec[i]=(i*37+11)&255;
    // Arbitrary rotation order still reconstructs the logical image.
    for(i=0;i<14;i++) assert(SoulLinkGroupFlash_Write(codec,sizeof(codec),(i*5)%14,sectors[(i*5)%14]));
    for(i=0;i<14;i++) for(j=0;j<4096;j++) if(!Allowed(i,j)) assert(sectors[i][j]==original[i][j]);
    for(i=0;i<14;i++) assert(SoulLinkGroupFlash_Read(decoded,sizeof(decoded),(i*3)%14,sectors[(i*3)%14]));
    assert(!memcmp(codec,decoded,sizeof(codec)));
    assert(!SoulLinkGroupFlash_Write(codec,sizeof(codec)-1,0,sectors[0]));
    assert(!SoulLinkGroupFlash_Write(codec,sizeof(codec),14,sectors[0]));
    assert(!SoulLinkGroupFlash_Read(NULL,sizeof(codec),0,sectors[0]));
    puts("PASS four-player flash mapping: 4868-byte codec roundtrip; game fields and all sector footers preserved outside reserved spans");
    return 0;
}
