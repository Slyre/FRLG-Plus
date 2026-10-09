#ifndef GUARD_SOUL_LINK_GROUP_FLASH_H
#define GUARD_SOUL_LINK_GROUP_FLASH_H
#include "soul_link_group.h"
#define SOUL_LINK_GROUP_FLASH_CAPACITY 5212
#define SOUL_LINK_GROUP_FLASH_SECTORS 14
#define SOUL_LINK_GROUP_FLASH_SECTOR_BYTES 4096
// These operate on logical sector IDs, never on physical rotated slot indices.
// They only access existing Soul Link reserve bytes, unused data tails, and
// the 116 unused footer bytes. The last 12 footer bytes are left untouched.
bool8 SoulLinkGroupFlash_Write(const u8 *codec, u32 size, u8 logicalSector, u8 *sector);
bool8 SoulLinkGroupFlash_Read(u8 *codec, u32 size, u8 logicalSector, const u8 *sector);
#endif
