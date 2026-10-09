#ifndef GUARD_SOUL_LINK_GROUP_H
#define GUARD_SOUL_LINK_GROUP_H

#include "soul_link_types.h"

// Experimental foundation only. The production save and companion ABI remain v3/2p.
#ifndef SOUL_LINK_FOUR_PLAYERS
#define SOUL_LINK_FOUR_PLAYERS 0
#endif
#define SOUL_LINK_GROUP_PLAYERS 4
#define SOUL_LINK_GROUP_CAPACITY 88
#define SOUL_LINK_GROUP_MAGIC 0x34474C53u
#define SOUL_LINK_GROUP_VERSION 4
#define SOUL_LINK_GROUP_HEADER_BYTES 112
#define SOUL_LINK_GROUP_ROW_BYTES 54
#define SOUL_LINK_GROUP_SAVE_BYTES (112 + 88 * 54 + 4)
#define SOUL_LINK_GROUP_ALL_PLAYERS 15
#define SOUL_LINK_GROUP_NAME_BYTES 8

struct SoulLinkGroup
{
    struct SoulLinkMonIdentity mons[4];
    u16 groupId, areaId;
    u8 encounterStates[4];
    u8 state, cleanupFlags;
};

// This structure also describes the explicit little-endian header codec.
// Its C representation is never memcpy'd to disk or a transport.
struct SoulLinkGroupHeader
{
    u32 magic;
    u16 version, areaVersion;
    struct SoulLinkRunId runId;
    u32 participantIds[4];
    u8 trainerNames[4][8];
    u8 trainerLanguages[4];
    struct SoulLinkConfig config;
    u32 configRevision;
    u16 teamIds[6];
    u32 teamRevision;
    u16 groupCount, nextLocalOrdinal;
    u8 localPlayer, participantCount, knownNameMask, graveBox;
    u8 teamCount, teamLocked;
    u16 reserved;
    u32 agreementRevision;
};

struct SoulLinkGroupSave
{
    struct SoulLinkGroupHeader header;
    struct SoulLinkGroup groups[88];
};

// An authenticated transport binds owner to its room slot. Names are display data.
// areaRound is the owner's acquisition rank WITHIN this area, or among all
// non-starter acquisitions when sameAreaPairing is disabled. Global encounter
// ordinal remains part of the mon identity and must never be used as areaRound.
struct SoulLinkGroupObservation
{
    struct SoulLinkMonIdentity mon;
    u16 areaId;
    u8 areaRound, state;
};
struct SoulLinkGroupOwnerSnapshot
{
    struct SoulLinkRunId runId;
    u32 participantIds[4];
    struct SoulLinkConfig config;
    u32 configRevision;
    u8 owner, count, hasTrainerName, language;
    u8 trainerName[8];
    struct SoulLinkGroupObservation observations[88];
};

struct SoulLinkGroupTeamCommand
{
    struct SoulLinkRunId runId;
    u32 participantIds[4];
    u32 configRevision, registryDigest, expectedTeamRevision, agreementRevision;
    u16 groupIds[6];
    u8 count, approvedMask, readyMask, reserved;
};
typedef bool8 (*SoulLinkGroupTypes)(void *context, u16 species, u8 types[2]);
typedef bool8 (*SoulLinkGroupTeamPreflight)(void *context, const u16 *ids, u8 count);

enum SoulLinkGroupResult
{
    SOUL_LINK_GROUP_OK,
    SOUL_LINK_GROUP_INVALID,
    SOUL_LINK_GROUP_CONTEXT,
    SOUL_LINK_GROUP_IDENTITY,
    SOUL_LINK_GROUP_CAPACITY_ERROR,
    SOUL_LINK_GROUP_CORRUPT,
    SOUL_LINK_GROUP_UNANIMOUS,
    SOUL_LINK_GROUP_TEAM,
    SOUL_LINK_GROUP_TYPES,
    SOUL_LINK_GROUP_MIGRATION_AMBIGUOUS,
};

u16 SoulLinkGroup_Key(u16 area, u8 areaRound, bool8 sameArea);
bool8 SoulLinkGroup_Validate(const struct SoulLinkGroupSave *save);
u8 SoulLinkGroup_Initialize(struct SoulLinkGroupSave *save, const struct SoulLinkRunId *run,
    const u32 participants[4], u8 localPlayer, const struct SoulLinkConfig *config);
u8 SoulLinkGroup_Encode(const struct SoulLinkGroupSave *save, u8 *bytes, u32 size);
// Decode/merge/migrate stage into caller-owned scratch. On failure destination
// is untouched. Scratch must be a separate non-overlapping full save object.
u8 SoulLinkGroup_Decode(const u8 *bytes, u32 size, struct SoulLinkGroupSave *destination,
    struct SoulLinkGroupSave *scratch);
u8 SoulLinkGroup_MergeOwner(struct SoulLinkGroupSave *save,
    const struct SoulLinkGroupOwnerSnapshot *snapshot, u8 authenticatedOwner,
    struct SoulLinkGroupSave *scratch);
u32 SoulLinkGroup_RegistryDigest(const struct SoulLinkGroupSave *save);
bool8 SoulLinkGroup_TypesAllowed(const struct SoulLinkGroup *group, u8 mode,
    SoulLinkGroupTypes types, void *context);
u8 SoulLinkGroup_ValidateTeamCommand(const struct SoulLinkGroupSave *save,
    const struct SoulLinkGroupTeamCommand *command, SoulLinkGroupTypes types,
    SoulLinkGroupTeamPreflight preflight, void *context);
// Explicit opt-in conversion only; not called by production load. Retains the
// two existing participants at slotMap[0/1], requires a NEW run, clears every
// old team approval. Ambiguous per-area chronology is rejected without writes.
u8 SoulLinkGroup_MigrateV3(const struct SoulLinkSaveHeader *legacy,
    const struct SoulLinkPair *rows, const struct SoulLinkRunId *newRun,
    const u32 participants[4], const u8 slotMap[2], struct SoulLinkGroupSave *destination,
    struct SoulLinkGroupSave *scratch);

#endif
