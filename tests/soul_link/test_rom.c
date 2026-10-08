// Executes the compiled Thumb routines in mGBA; this complements host stubs.
#include <assert.h>
#include <png.h>
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include "soul_link_types.h"
#include "soul_link_protocol.h"
#include "constants/pokemon.h"
#include "constants/species.h"
#include "constants/items.h"

static struct mCore *sCore;
static const char *sSymbols;
static color_t sPixels[240 * 160];
static void QuietLog(struct mLogger *logger, int category, enum mLogLevel level, const char *format, va_list args)
{ (void)logger; (void)category; (void)level; (void)format; (void)args; }
static struct mLogger sLogger = {.log = QuietLog};
static uint32_t sScratch = 0x02001000;
#define SCRATCH sScratch
#define STACK 0x03007000
#define RETURN_TRAP 0x03007E00

static uint32_t Symbol(const char *name)
{
    static struct { char name[96]; uint32_t address; } cache[256];
    static unsigned count;
    for (unsigned i = 0; i < count; i++)
        if (!strcmp(cache[i].name, name)) return cache[i].address;
    FILE *file = fopen(sSymbols, "r");
    char line[512], symbol[256], kind;
    uint32_t address;
    assert(file != NULL);
    while (fgets(line, sizeof(line), file))
        if (sscanf(line, "%x %c %255s", &address, &kind, symbol) == 3 && strcmp(name, symbol) == 0)
        {
            fclose(file);
            assert(count < sizeof(cache) / sizeof(cache[0]) && strlen(name) < sizeof(cache[0].name));
            strcpy(cache[count].name, name); cache[count++].address = address;
            return address;
        }
    fclose(file);
    fprintf(stderr, "Missing ROM symbol: %s\n", name);
    abort();
}

static void Write(uint32_t address, const void *data, size_t size)
{
    const uint8_t *bytes = data;
    for (size_t i = 0; i < size; i++) sCore->busWrite8(sCore, address + i, bytes[i]);
}
static void Read(uint32_t address, void *data, size_t size)
{
    uint8_t *bytes = data;
    for (size_t i = 0; i < size; i++) bytes[i] = sCore->busRead8(sCore, address + i);
}
static void Reg(const char *name, uint32_t value) { assert(sCore->writeRegister(sCore, name, &value)); }
static uint32_t Call(const char *name, unsigned argc, const uint32_t *args)
{
    uint32_t pc, result;
    char reg[8];
    sCore->busWrite16(sCore, 0x04000208, 0);
    sCore->busWrite16(sCore, RETURN_TRAP, 0xE7FE); // Thumb b .
    Reg("cpsr", 0xFF);
    Reg("sp", STACK);
    Reg("lr", RETURN_TRAP | 1);
    for (unsigned i = 0; i < 4; i++)
    {
        snprintf(reg, sizeof(reg), "r%u", i);
        Reg(reg, i < argc ? args[i] : 0);
    }
    for (unsigned i = 4; i < argc; i++) sCore->busWrite32(sCore, STACK + (i - 4) * 4, args[i]);
    Reg("pc", Symbol(name));
    for (unsigned steps = 0; steps < 8000000; steps++)
    {
        sCore->step(sCore);
        assert(sCore->readRegister(sCore, "pc", &pc));
        if (pc >= RETURN_TRAP && pc < RETURN_TRAP + 8)
        {
            assert(sCore->readRegister(sCore, "r0", &result));
            return result;
        }
    }
    fprintf(stderr, "ROM routine timed out: %s, PC=%08x\n", name, pc);
    abort();
}
#define CALL0(name) Call(name, 0, NULL)
#define CALL(name, ...) Call(name, sizeof((uint32_t[]){__VA_ARGS__}) / sizeof(uint32_t), (uint32_t[]){__VA_ARGS__})

static void Screenshot(const char *path)
{
    uint8_t rgb[240 * 160 * 3];
    png_bytep rows[160];
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    for (unsigned y = 0; y < 160; y++)
    {
        rows[y] = rgb + y * 240 * 3;
        for (unsigned x = 0; x < 240; x++)
        {
            color_t pixel = sPixels[y * 240 + x];
            rows[y][x * 3] = pixel & 255;
            rows[y][x * 3 + 1] = (pixel >> 8) & 255;
            rows[y][x * 3 + 2] = (pixel >> 16) & 255;
        }
    }
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = png_create_info_struct(png);
    assert(png != NULL && info != NULL && !setjmp(png_jmpbuf(png)));
    png_init_io(png, file);
    png_set_IHDR(png, info, 240, 160, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    png_write_image(png, rows);
    png_write_end(png, info);
    png_destroy_write_struct(&png, &info);
    fclose(file);
}

static void TestTargetGameplay(void)
{
    // Isolated ROM routines use the actual engine buffers, independently of title-screen initialization.
    sCore->busWrite32(sCore, Symbol("gSaveBlock1Ptr"), Symbol("gSaveBlock1"));
    sCore->busWrite32(sCore, Symbol("gSaveBlock2Ptr"), Symbol("gSaveBlock2"));
    sCore->busWrite32(sCore, Symbol("gPokemonStoragePtr"), Symbol("gPokemonStorage"));
    uint32_t sb2 = sCore->busRead32(sCore, Symbol("gSaveBlock2Ptr"));
    uint32_t storage = sCore->busRead32(sCore, Symbol("gPokemonStoragePtr"));
    uint32_t party = Symbol("gPlayerParty");
    struct SoulLinkRunId run = {{123, 456}};
    uint32_t participants[2] = {10, 20};
    struct SoulLinkConfig config = {1, 1, 1, 1, 1, 0, 0, 0};
    struct SoulLinkMonIdentity identity = {10, 99, SPECIES_BULBASAUR, 0};
    struct SoulLinkPair pair;
    uint16_t hp = 20;
    assert(sb2 >= 0x02000000 && sb2 < 0x02040000);
    assert(storage >= 0x02000000 && storage < 0x02040000);
    CALL0("ResetPokemonStorageSystem");
    CALL0("ZeroPlayerPartyMons");
    CALL0("SoulLink_ResetSaveData");
    Write(SCRATCH, &run, sizeof(run));
    Write(SCRATCH + 16, participants, sizeof(participants));
    assert(CALL("SoulLink_BeginRun", SCRATCH, SCRATCH + 16, 0) == 1);
    Write(SCRATCH + 32, &config, sizeof(config));
    assert(CALL("SoulLink_SetConfig", SCRATCH + 32) == 1 && CALL0("SoulLink_IsEnabled") == 1);
    CALL("CreateMon", party, SPECIES_BULBASAUR, 5, 31, 1, 99, OT_ID_PRESET, 10);
    CALL("CreateMon", party + 100, SPECIES_CHARMANDER, 5, 31, 1, 100, OT_ID_PRESET, 10);
    Write(SCRATCH + 64, &identity, sizeof(identity));
    assert(CALL("SoulLink_RecordLocalEncounter", 8, SCRATCH + 64, SOUL_LINK_ENCOUNTER_CAPTURED) == 1);
    assert(CALL("SoulLink_MarkLocalDeath", SCRATCH + 64) == 1);
    Write(SCRATCH + 96, &hp, sizeof(hp));
    CALL("SetMonData", party, MON_DATA_HP, SCRATCH + 96);
    assert(CALL("GetMonData", party, MON_DATA_HP, 0) == 0);
    assert(CALL("SoulLink_ReadPair", 0, SCRATCH + 128) == 1);
    Read(SCRATCH + 128, &pair, sizeof(pair));
    assert(pair.state == SOUL_LINK_PAIR_DEAD && pair.cleanupFlags == 1);
    CALL0("SoulLink_RequestCleanup");
    CALL0("SoulLink_ProcessField");
    assert(sCore->busRead32(sCore, party) == 100); // Living Charmander compacted into the first slot.
    assert(sCore->busRead8(sCore, Symbol("gPlayerPartyCount")) == 1);
    assert(sCore->busRead8(sCore, sb2 + 0xB20 + offsetof(struct SoulLinkSaveHeader, graveBox)) == 13);
    uint32_t deadBoxMon = storage + 4 + 13 * 30 * 80;
    assert(sCore->busRead32(sCore, deadBoxMon) == 99);
    assert(CALL("GetBoxMonData", deadBoxMon, MON_DATA_BOX_HP, 0) == 0);
    assert(CALL("SoulLink_ReadPair", 0, SCRATCH + 128) == 1);
    Read(SCRATCH + 128, &pair, sizeof(pair));
    assert(pair.state == SOUL_LINK_PAIR_DEAD && pair.cleanupFlags == 0);
    config.enabled = 0;
    Write(SCRATCH + 32, &config, sizeof(config));
    assert(CALL("SoulLink_SetConfig", SCRATCH + 32) == 1);
    assert(CALL("GetBoxMonData", deadBoxMon, MON_DATA_BOX_HP, 0) == 0);
    puts("mGBA target: encrypted Pokemon, terminal HP, Soul Grave and party compaction passed");
}

static void TestTargetProtocol(const char *romPath)
{
    struct mCore *cores[2] = {sCore, mCoreFind(romPath)};
    struct SoulLinkRunId run = {{123, 456}};
    uint32_t participants[2] = {10, 20};
    struct SoulLinkConfig config = {1, 1, 1, 1, 1, 0, 0, 0};
    struct SoulLinkPair pair = {0}, result;
    struct SoulLinkPacket packets[2];
    uint32_t protocol = SCRATCH + 0x400;
    assert(cores[1] != NULL && cores[1]->init(cores[1]));
    mCoreInitConfig(cores[1], "soul-link-protocol-tests");
    assert(mCoreLoadFile(cores[1], romPath));
    cores[1]->reset(cores[1]);
    pair.pairId = 1; pair.areaId = 8; pair.state = SOUL_LINK_PAIR_ALIVE;
    for (unsigned player = 0; player < 2; player++)
    {
        pair.encounterStates[player] = SOUL_LINK_ENCOUNTER_CAPTURED;
        pair.mons[player].otId = participants[player];
        pair.mons[player].personality = 99 + player;
        pair.mons[player].species = SPECIES_BULBASAUR;
        pair.mons[player].encounterOrdinal = 1;
    }
    for (unsigned player = 0; player < 2; player++)
    {
        sCore = cores[player];
        sCore->busWrite32(sCore, Symbol("gSaveBlock1Ptr"), Symbol("gSaveBlock1"));
        sCore->busWrite32(sCore, Symbol("gSaveBlock2Ptr"), Symbol("gSaveBlock2"));
        sCore->busWrite32(sCore, Symbol("gPokemonStoragePtr"), Symbol("gPokemonStorage"));
        CALL0("SoulLink_ResetSaveData");
        Write(SCRATCH, &run, sizeof(run)); Write(SCRATCH + 16, participants, sizeof(participants));
        Write(SCRATCH + 32, &config, sizeof(config)); Write(SCRATCH + 128, &pair, sizeof(pair));
        assert(CALL("SoulLink_BeginRun", SCRATCH, SCRATCH + 16, player) == 1);
        assert(CALL("SoulLink_SetConfig", SCRATCH + 32) == 1);
        assert(CALL("SoulLink_StorePair", SCRATCH + 128) == 1);
        if (player == 0)
        {
            Write(SCRATCH + 64, &pair.mons[0], sizeof(pair.mons[0]));
            assert(CALL("SoulLink_MarkLocalDeath", SCRATCH + 64) == 1);
        }
        assert(CALL("SoulLink_ProtocolInit", protocol) == 1);
    }
    bool interrupted = false;
    for (unsigned round = 0; round < 7; round++)
    {
        for (unsigned player = 0; player < 2; player++)
        {
            sCore = cores[player];
            assert(CALL("SoulLink_ProtocolBuild", protocol) == 1);
            Read(protocol + offsetof(struct SoulLinkProtocol, send), &packets[player], sizeof(packets[player]));
        }
        for (unsigned player = 0; player < 2; player++)
        {
            sCore = cores[player];
            Write(SCRATCH + 256, &packets[player ^ 1], sizeof(packets[0]));
            assert(CALL("SoulLink_ProtocolReceive", protocol, SCRATCH + 256, sizeof(packets[0])) == 1);
            assert(CALL("SoulLink_ReadPair", 0, SCRATCH + 128) == 1);
            Read(SCRATCH + 128, &result, sizeof(result));
            if (player == 1 && round < 5) assert(result.state == SOUL_LINK_PAIR_ALIVE);
            if (round >= 5)
            {
                assert(result.state == SOUL_LINK_PAIR_DEAD);
                assert(result.encounterStates[1] == SOUL_LINK_ENCOUNTER_DEAD);
                assert(result.cleanupFlags == (player == 0 ? 1 : 2));
            }
        }
        if (round == 4 && !interrupted)
        {
            // Deliver READY only to A, then discard both transport sessions.
            for (unsigned player = 0; player < 2; player++)
            {
                sCore = cores[player]; assert(CALL("SoulLink_ProtocolBuild", protocol) == 1);
                Read(protocol + offsetof(struct SoulLinkProtocol, send), &packets[player], sizeof(packets[player]));
            }
            sCore = cores[0]; Write(SCRATCH + 256, &packets[1], sizeof(packets[1]));
            assert(CALL("SoulLink_ProtocolReceive", protocol, SCRATCH + 256, sizeof(packets[1])) == 1);
            sCore = cores[1]; assert(CALL("SoulLink_ReadPair", 0, SCRATCH + 128) == 1);
            Read(SCRATCH + 128, &result, sizeof(result)); assert(result.state == SOUL_LINK_PAIR_ALIVE);
            for (unsigned player = 0; player < 2; player++)
            {
                sCore = cores[player]; assert(CALL0("SoulLink_LoadSaveData") == 1);
                assert(CALL("SoulLink_ProtocolInit", protocol) == 1);
            }
            interrupted = true; round = (unsigned)-1;
        }
    }
    for (unsigned player = 0; player < 2; player++)
    {
        sCore = cores[player];
        assert(sCore->busRead8(sCore, protocol + offsetof(struct SoulLinkProtocol, status)) == SOUL_LINK_SESSION_COMPLETE);
    }
    // A retains B's death while B has restored an older local snapshot.
    sCore = cores[0];
    assert(CALL("SoulLink_ReadPair", 0, SCRATCH + 128) == 1);
    Read(SCRATCH + 128, &result, sizeof(result));
    result.encounterStates[1] = SOUL_LINK_ENCOUNTER_DEAD;
    Write(SCRATCH + 128, &result, sizeof(result));
    assert(CALL("SoulLink_StorePair", SCRATCH + 128) == 1);
    sCore = cores[1];
    result.encounterStates[1] = SOUL_LINK_ENCOUNTER_CAPTURED;
    result.cleanupFlags = 0;
    // Restore the older persisted bytes, bypassing the no-resurrection API.
    Write(sCore->busRead32(sCore, Symbol("gSaveBlock2Ptr")) + 0xB20 + sizeof(struct SoulLinkSaveHeader), &result, sizeof(result));
    assert(CALL0("SoulLink_LoadSaveData") == 1);
    for (unsigned player = 0; player < 2; player++)
    {
        sCore = cores[player];
        assert(CALL("SoulLink_ProtocolInit", protocol) == 1);
    }
    for (unsigned round = 0; round < 7; round++)
    {
        for (unsigned player = 0; player < 2; player++)
        {
            sCore = cores[player];
            assert(CALL("SoulLink_ProtocolBuild", protocol) == 1);
            Read(protocol + offsetof(struct SoulLinkProtocol, send), &packets[player], sizeof(packets[player]));
        }
        for (unsigned player = 0; player < 2; player++)
        {
            sCore = cores[player];
            Write(SCRATCH + 256, &packets[player ^ 1], sizeof(packets[0]));
            assert(CALL("SoulLink_ProtocolReceive", protocol, SCRATCH + 256, sizeof(packets[0])) == 1);
        }
    }
    sCore = cores[1];
    assert(CALL("SoulLink_ReadPair", 0, SCRATCH + 128) == 1);
    Read(SCRATCH + 128, &result, sizeof(result));
    assert(result.encounterStates[1] == SOUL_LINK_ENCOUNTER_DEAD && result.cleanupFlags == 2);
    for (unsigned scenario = 0; scenario < 2; scenario++)
    {
        for (unsigned player = 0; player < 2; player++)
        {
            sCore = cores[player];
            uint32_t party = Symbol("gPlayerParty");
            struct SoulLinkMonIdentity captured;
            CALL0("SoulLink_ResetSaveData");
            CALL0("ZeroPlayerPartyMons");
            CALL0("ResetPokemonStorageSystem");
            Write(SCRATCH, &run, sizeof(run)); Write(SCRATCH + 16, participants, sizeof(participants));
            Write(SCRATCH + 32, &config, sizeof(config));
            assert(CALL("SoulLink_BeginRun", SCRATCH, SCRATCH + 16, player) == 1);
            assert(CALL("SoulLink_SetConfig", SCRATCH + 32) == 1);
            CALL("CreateMon", party, player == 0 ? SPECIES_BULBASAUR : SPECIES_CHARMANDER,
                 5, 31, 1, 501 + player * 100, OT_ID_PRESET, participants[player]);
            sCore->busWrite8(sCore, Symbol("gPlayerPartyCount"), 1);
            captured.otId = CALL("GetMonData", party, MON_DATA_OT_ID, 0);
            captured.personality = CALL("GetMonData", party, MON_DATA_PERSONALITY, 0);
            captured.species = CALL("GetMonData", party, MON_DATA_SPECIES, 0);
            captured.encounterOrdinal = 0;
            Write(SCRATCH + 64, &captured, sizeof(captured));
            assert(CALL("SoulLink_RecordLocalEncounter", scenario == 0 ? 8 : 9 + player,
                        SCRATCH + 64, SOUL_LINK_ENCOUNTER_CAPTURED) == 1);
            assert(CALL("SoulLink_ProtocolInit", protocol) == 1);
        }
        unsigned rounds = 0;
        while (cores[0]->busRead8(cores[0], protocol + offsetof(struct SoulLinkProtocol, status)) == SOUL_LINK_SESSION_SYNCING)
        {
            assert(++rounds <= SOUL_LINK_PAIR_CAPACITY * 2 + 5);
            for (unsigned player = 0; player < 2; player++)
            {
                sCore = cores[player];
                assert(CALL("SoulLink_ProtocolBuild", protocol) == 1);
                Read(protocol + offsetof(struct SoulLinkProtocol, send), &packets[player], sizeof(packets[player]));
            }
            for (unsigned player = 0; player < 2; player++)
            {
                sCore = cores[player];
                Write(SCRATCH + 256, &packets[player ^ 1], sizeof(packets[0]));
                assert(CALL("SoulLink_ProtocolReceive", protocol, SCRATCH + 256, sizeof(packets[0])) == 1);
            }
        }
        for (unsigned player = 0; player < 2; player++)
        {
            sCore = cores[player];
            assert(CALL0("SoulLink_GetPairCount") == (scenario == 0 ? 1 : 2));
            assert(CALL("SoulLink_ReadPair", 0, SCRATCH + 128) == 1);
            Read(SCRATCH + 128, &result, sizeof(result));
            assert(result.pairId == 1 && result.state == (scenario == 0 ? SOUL_LINK_PAIR_ALIVE : SOUL_LINK_PAIR_PENDING));
            if (scenario == 0) assert(result.mons[0].personality == 501 && result.mons[1].personality == 601);
            assert(CALL("GetMonData", Symbol("gPlayerParty"), MON_DATA_HP, 0) > 0);
            assert(sCore->busRead8(sCore, protocol + offsetof(struct SoulLinkProtocol, status)) == SOUL_LINK_SESSION_COMPLETE);
        }
    }
    // Use the living route-separated fixture: bind it afresh on a shared route.
    for (unsigned player = 0; player < 2; player++)
    {
        sCore = cores[player];
        CALL0("SoulLink_ResetSaveData");
        Write(SCRATCH, &run, sizeof(run)); Write(SCRATCH + 16, participants, sizeof(participants));
        Write(SCRATCH + 32, &config, sizeof(config));
        assert(CALL("SoulLink_BeginRun", SCRATCH, SCRATCH + 16, player) == 1);
        assert(CALL("SoulLink_SetConfig", SCRATCH + 32) == 1);
        memset(&result, 0, sizeof(result));
        result.pairId = 1; result.areaId = 8; result.state = SOUL_LINK_PAIR_ALIVE;
        result.encounterStates[0] = result.encounterStates[1] = SOUL_LINK_ENCOUNTER_CAPTURED;
        result.mons[0] = (struct SoulLinkMonIdentity){10, 501, SPECIES_BULBASAUR, 1};
        result.mons[1] = (struct SoulLinkMonIdentity){20, 601, SPECIES_CHARMANDER, 1};
        Write(SCRATCH + 128, &result, sizeof(result));
        assert(CALL("SoulLink_StorePair", SCRATCH + 128) == 1);
    }
    sCore = cores[1];
    uint32_t party = Symbol("gPlayerParty");
    CALL("CreateMon", party + 100, SPECIES_SQUIRTLE, 5, 31, 1, 602, OT_ID_PRESET, 20);
    sCore->busWrite8(sCore, Symbol("gPlayerPartyCount"), 2);
    uint16_t item = ITEM_ORAN_BERRY, hp = 20;
    Write(SCRATCH + 64, &item, sizeof(item)); CALL("SetMonData", party, MON_DATA_HELD_ITEM, SCRATCH + 64);
    sCore = cores[0];
    Write(SCRATCH + 64, &result.mons[0], sizeof(result.mons[0]));
    assert(CALL("SoulLink_MarkLocalDeath", SCRATCH + 64) == 1);
    for (unsigned player = 0; player < 2; player++)
    {
        sCore = cores[player]; assert(CALL("SoulLink_ProtocolInit", protocol) == 1);
    }
    for (unsigned round = 0; round < 7; round++)
    {
        for (unsigned player = 0; player < 2; player++)
        {
            sCore = cores[player]; assert(CALL("SoulLink_ProtocolBuild", protocol) == 1);
            Read(protocol + offsetof(struct SoulLinkProtocol, send), &packets[player], sizeof(packets[player]));
        }
        for (unsigned player = 0; player < 2; player++)
        {
            sCore = cores[player]; Write(SCRATCH + 256, &packets[player ^ 1], sizeof(packets[0]));
            assert(CALL("SoulLink_ProtocolReceive", protocol, SCRATCH + 256, sizeof(packets[0])) == 1);
        }
    }
    sCore = cores[1];
    uint32_t battleFlags = Symbol("gMain") + 0x439;
    uint8_t savedFlags = sCore->busRead8(sCore, battleFlags);
    sCore->busWrite8(sCore, battleFlags, savedFlags | 2);
    Write(SCRATCH + 64, &hp, sizeof(hp)); CALL("SetMonData", party, MON_DATA_HP, SCRATCH + 64);
    assert(CALL("GetMonData", party, MON_DATA_HP, 0) == 0);
    CALL0("SoulLink_ProcessField");
    assert(sCore->busRead8(sCore, Symbol("gPlayerPartyCount")) == 2);
    assert(CALL("GetMonData", party, MON_DATA_PERSONALITY, 0) == 601);
    sCore->busWrite8(sCore, battleFlags, savedFlags & ~2);
    for (unsigned tick = 0; tick < 128; tick++) CALL0("SoulLink_ProcessField");
    assert(sCore->busRead8(sCore, Symbol("gPlayerPartyCount")) == 1);
    assert(CALL("GetMonData", party, MON_DATA_PERSONALITY, 0) == 602);
    uint32_t storage = sCore->busRead32(sCore, Symbol("gPokemonStoragePtr"));
    uint32_t grave = storage + 4 + 13 * 30 * 80;
    assert(CALL("GetBoxMonData", grave, MON_DATA_PERSONALITY, 0) == 601);
    assert(CALL("GetBoxMonData", grave, MON_DATA_HELD_ITEM, 0) == ITEM_ORAN_BERRY);
    assert(CALL("GetBoxMonData", grave, MON_DATA_BOX_HP, 0) == 0);
    assert(CALL("SoulLink_ReadPair", 0, SCRATCH + 128) == 1);
    Read(SCRATCH + 128, &result, sizeof(result));
    assert(result.cleanupFlags == 0 && result.encounterStates[1] == SOUL_LINK_ENCOUNTER_DEAD);
    config.enabled = 0; Write(SCRATCH + 32, &config, sizeof(config));
    assert(CALL("SoulLink_SetConfig", SCRATCH + 32) == 1);
    assert(CALL("GetBoxMonData", grave, MON_DATA_BOX_HP, 0) == 0);
    puts("mGBA target: remote death prevents battle healing, defers party removal and preserves encrypted body/item in Soul Grave");
    mCoreConfigDeinit(&cores[1]->config);
    cores[1]->deinit(cores[1]);
    sCore = cores[0];
    puts("mGBA target: two compiled Thumb endpoints, HELLO/PING, staged snapshot, death-state resync and older-save recovery passed (blocks forwarded by harness)");
    puts("mGBA target: asymmetric READY interruption followed by reload and fresh-session reconciliation passed");
    puts("mGBA target: encrypted captures paired on the same route; distinct routes stay pending and both Pokemon stay alive");
}

static void MenuKey(uint16_t key)
{
    sCore->busWrite16(sCore, Symbol("gMain") + 0x2E, key);
    CALL0("SoulLinkMenu_HandleInput");
    sCore->busWrite16(sCore, Symbol("gMain") + 0x2E, 0);
    CALL0("ProcessDma3Requests");
    sCore->runFrame(sCore);
    sCore->runFrame(sCore);
}

static void MenuScreenshot(const char *base, const char *name)
{
    char path[512];
    const char *slash = strrchr(base, '/');
    assert(slash && (size_t)(slash - base) < sizeof(path) - 64);
    snprintf(path, sizeof(path), "%.*s/%s", (int)(slash - base), base, name);
    CALL0("VBlankCB_SoulLinkMenu");
    CALL0("CopyBufferedValuesToGpuRegs");
    CALL0("ProcessDma3Requests");
    sCore->runFrame(sCore);
    sCore->runFrame(sCore); // Capture a complete frame after visible-time test DMA.
    Screenshot(path);
}

static void TestTargetMenu(const char *screenshots)
{
    enum { A = 1, B = 2, SELECT = 4, RIGHT = 16, LEFT = 32, UP = 64, DOWN = 128 };
    sCore->busWrite32(sCore, Symbol("gSaveBlock1Ptr"), Symbol("gSaveBlock1"));
    sCore->busWrite32(sCore, Symbol("gSaveBlock2Ptr"), Symbol("gSaveBlock2"));
    sCore->busWrite32(sCore, Symbol("gPokemonStoragePtr"), Symbol("gPokemonStorage"));
    uint32_t sb2 = sCore->busRead32(sCore, Symbol("gSaveBlock2Ptr"));
    struct SoulLinkSaveHeader header;
    struct SoulLinkPair pair = {0};
    uint32_t own = 10;
    // Earlier isolated fixtures used raw heap memory as scratch. Start a clean
    // heap for this UI fixture and allocate its scratch without overlap.
    CALL("InitHeap", Symbol("gHeap"), 0x1C000);
    sScratch = CALL("AllocZeroed", 512); assert(sScratch != 0);
    uint32_t clearedBuffers[4] = {0};
    Write(Symbol("gWindowBgTilemapBuffers"), clearedBuffers, sizeof(clearedBuffers));
    CALL0("SoulLink_ResetSaveData");
    Write(sb2 + 0xA, &own, sizeof(own));
    CALL0("CB2_SoulLinkMenu");
    assert(sCore->busRead32(sCore, Symbol("sSoulLinkMenu")) != 0);
    for (unsigned tick = 0; tick < 32; tick++)
    {
        CALL0("CB2_SoulLinkMenuMain"); CALL0("VBlankCB_SoulLinkMenu");
    }
    MenuScreenshot(screenshots, "stage8-menu-home.png");
    MenuKey(DOWN); MenuKey(DOWN); MenuKey(A); MenuKey(RIGHT);
    MenuScreenshot(screenshots, "stage8-menu-empty.png"); MenuKey(B);
    // Enabled requires a run, and B discards the draft.
    MenuKey(DOWN); MenuKey(A); MenuKey(A);
    for (unsigned row = 0; row < 7; row++) MenuKey(DOWN);
    MenuKey(A); assert(CALL0("SoulLink_IsEnabled") == 0);
    MenuKey(B); MenuKey(B);
    // Run editor: select partner ID and enter 00000014 (decimal 20).
    MenuKey(A); MenuScreenshot(screenshots, "stage8-menu-run.png");
    MenuKey(A); MenuKey(B); // Missing partner rejects creation without changing the run.
    assert(CALL("SoulLink_GetRunId", SCRATCH) == 0);
    MenuKey(SELECT);
    for (unsigned digit = 0; digit < 6; digit++) MenuKey(RIGHT);
    MenuKey(UP); MenuKey(RIGHT);
    for (unsigned n = 0; n < 4; n++) MenuKey(UP);
    MenuKey(A); MenuKey(A);
    Read(sb2 + 0xB20, &header, sizeof(header));
    assert(header.localPlayer == 0 && header.participantIds[0] == 10 && header.participantIds[1] == 20);
    assert(header.runId.words[0] != 0 && header.runId.words[1] == SOUL_LINK_SAVE_MAGIC);
    struct SoulLinkRunId created = header.runId;
    MenuKey(A); // A cannot replace an existing run.
    Read(sb2 + 0xB20, &header, sizeof(header));
    assert(!memcmp(&created, &header.runId, sizeof(created)));
    MenuKey(B); MenuKey(DOWN); MenuKey(A); MenuKey(RIGHT);
    MenuScreenshot(screenshots, "stage8-menu-settings.png");
    for (unsigned row = 0; row < 7; row++) MenuKey(DOWN);
    MenuKey(A); assert(CALL0("SoulLink_IsEnabled") == 1); MenuKey(A);
    // Alter a rule and discard it; persistent configuration remains unchanged.
    MenuKey(DOWN); MenuKey(A); MenuKey(DOWN); MenuKey(LEFT); MenuKey(B);
    Read(sb2 + 0xB20, &header, sizeof(header)); assert(header.config.linkedDeaths == 1);
    for (unsigned i = 0; i < SOUL_LINK_PAIR_CAPACITY; i++)
    {
        pair.pairId = i + 1; pair.areaId = i == 0 ? 8 : 0x22;
        pair.state = i == 1 ? SOUL_LINK_PAIR_DEAD : SOUL_LINK_PAIR_ALIVE;
        pair.encounterStates[0] = pair.encounterStates[1] = i == 1 ? SOUL_LINK_ENCOUNTER_DEAD : SOUL_LINK_ENCOUNTER_CAPTURED;
        pair.cleanupFlags = i == 1 ? 1 : 0;
        pair.mons[0] = (struct SoulLinkMonIdentity){10, 101 + i, SPECIES_BULBASAUR, i + 1};
        pair.mons[1] = (struct SoulLinkMonIdentity){20, 201 + i, SPECIES_CHARMANDER, i + 1};
        Write(SCRATCH + 128, &pair, sizeof(pair)); assert(CALL("SoulLink_StorePair", SCRATCH + 128) == 1);
    }
    MenuKey(DOWN); MenuKey(DOWN); MenuKey(A);
    MenuScreenshot(screenshots, "stage8-menu-pair-alive.png");
    MenuKey(RIGHT); MenuScreenshot(screenshots, "stage8-menu-pair-dead.png");
    // Full table navigation wraps cleanly, without changing any saved rows.
    MenuKey(LEFT); MenuKey(LEFT); MenuKey(RIGHT);
    assert(CALL0("SoulLink_GetPairCount") == SOUL_LINK_PAIR_CAPACITY);
    MenuKey(B); MenuKey(B);
    for (unsigned tick = 0; tick < 32; tick++)
    {
        if (!sCore->busRead32(sCore, Symbol("sSoulLinkMenu"))) break;
        CALL0("CB2_SoulLinkMenuMain");
        CALL0("VBlankCB_SoulLinkMenu");
    }
    assert(sCore->busRead32(sCore, Symbol("sSoulLinkMenu")) == 0);
    assert(sCore->busRead32(sCore, Symbol("gMain") + 4) == (Symbol("CB2_ReturnToFieldWithOpenMenu") | 1));
    uint32_t allocation = CALL("Alloc", 90000); assert(allocation != 0);
    CALL("Free", allocation); CALL("Free", sScratch);
    puts("mGBA UI: run creation, immutable run, settings apply/discard, 88-pair navigation and cleanup passed");
}

int main(int argc, char **argv)
{
    assert(argc == 4);
    sSymbols = argv[2];
    mLogSetDefaultLogger(&sLogger);
    sCore = mCoreFind(argv[1]);
    assert(sCore != NULL && sCore->init(sCore));
    mCoreInitConfig(sCore, "soul-link-tests");
    sCore->setVideoBuffer(sCore, sPixels, 240);
    assert(mCoreLoadFile(sCore, argv[1]));
    sCore->reset(sCore);
    for (unsigned frame = 0; frame < 720; frame++) sCore->runFrame(sCore);
    fprintf(stderr, "Boot callback2=%08x\n", sCore->busRead32(sCore, Symbol("gMain") + 4));
    sCore->setKeys(sCore, 8); // START: skip the intro and reach the title/menu.
    for (unsigned frame = 0; frame < 4; frame++) sCore->runFrame(sCore);
    sCore->setKeys(sCore, 0);
    for (unsigned frame = 0; frame < 600; frame++) sCore->runFrame(sCore);
    fprintf(stderr, "Title callback2=%08x\n", sCore->busRead32(sCore, Symbol("gMain") + 4));
    Screenshot(argv[3]);
    printf("mGBA boot: %u frames, screenshot %s\n", sCore->frameCounter(sCore), argv[3]);
    if (!getenv("SOUL_LINK_UI_ONLY"))
    {
        TestTargetGameplay();
        TestTargetProtocol(argv[1]);
    }
    TestTargetMenu(argv[3]);
    mCoreConfigDeinit(&sCore->config);
    sCore->deinit(sCore);
    return 0;
}
