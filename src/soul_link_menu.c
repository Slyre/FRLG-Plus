#include "global.h"
#include "gflib.h"
#include "malloc.h"
#include "menu.h"
#include "overworld.h"
#include "pokemon.h"
#include "pokemon_icon.h"
#include "soul_link_rules.h"
#include "region_map.h"
#include "scanline_effect.h"
#include "script.h"
#include "task.h"
#include "soul_link.h"
#include "soul_link_menu.h"
#include "soul_link_team.h"
#include "strings.h"
#include "constants/region_map_sections.h"

extern const u8 NuzlockeLUT[MAPSEC_COUNT];
extern const u8 gSpeciesNames[][POKEMON_NAME_LENGTH + 1];

static const u8 sText_Settings[] = _("Rules");
static const u8 sText_Back[] = _("Back");
static const u8 sText_Enabled[] = _("Soul Link enabled");
static const u8 sText_LinkedDeaths[] = _("Linked deaths");
static const u8 sText_SameAreaPairs[] = _("Same area");
static const u8 sText_FirstEncounter[] = _("First encounter");
static const u8 sText_DupesClause[] = _("Dupes clause");
static const u8 sText_TypeClause[] = _("Type clause");
static const u8 sText_ApplyAndBack[] = _("Apply");
static const u8 sText_Empty[] = _("EMPTY");
static const u8 sText_Pending[] = _("WAITING");
static const u8 sText_Alive[] = _("ALIVE");
static const u8 sText_Dead[] = _("DEAD");
static const u8 sText_Failed[] = _("FAILED");
static const u8 sText_NoPairsYet[] = _("No pairs yet.");
static const u8 sText_CaptureThenSynchronizeWithYour[] = _("Captures appear through\nthe companion.");
static const u8 sText_BBack[] = _("B: back");
static const u8 sText_PairId[] = _("Pair:");
static const u8 sText_Slash[] = _("/");
static const u8 sText_Starter[] = _("STARTER");
static const u8 sText_SafariZone[] = _("SAFARI ZONE ");
static const u8 sText_Area[] = _("AREA ");
static const u8 sText_You[] = _("You:");
static const u8 sText_Partner[] = _("Partner:");
static const u8 sText_GraveTransferPending[] = _("PC transfer pending");
static const u8 sText_LeftRightPairBBack[] = _("LEFT/RIGHT: pair   B: back");
static const u8 sText_ABContinue[] = _("A / B: continue");
static const u8 sTypeNone[] = _("NONE"), sTypePrimary[] = _("PRIMARY"), sTypeAny[] = _("ANY");
static const u8 *const sTypes[] = {sTypeNone, sTypePrimary, sTypeAny};

static const u8 sReady[] = _("Team ready.");
static const u8 sChooseLiving[] = _("No living pairs selected.");
static const u8 sInvalidPair[] = _("Pair unavailable.");
static const u8 sMissingMon[] = _("POKéMON not found.");
static const u8 sDuplicateMon[] = _("Duplicate identity.");
static const u8 sInDaycare[] = _("POKéMON is in DAY CARE.");
static const u8 sInGrave[] = _("POKéMON is in SOUL GRAVE.");
static const u8 sEggIssue[] = _("An EGG cannot join this team.");
static const u8 sFaintedIssue[] = _("POKéMON has no HP.");
static const u8 sMailIssue[] = _("Remove held MAIL.");
static const u8 sTypeIssue[] = _("Shared type not allowed.");
static const u8 sPcIssue[] = _("Not enough PC space.");
static const u8 sRevisionIssue[] = _("Team selection blocked.");
static const u8 sUnknownIssue[] = _("Team could not be validated.");
static const u8 *const sIssueNames[] = {sReady, sChooseLiving, sInvalidPair, sMissingMon, sDuplicateMon,
    sInDaycare, sInGrave, sEggIssue, sFaintedIssue, sMailIssue, sTypeIssue, sPcIssue, sRevisionIssue, sUnknownIssue};
static const u8 sReconnectHint[] = _("Check the companion too.");
static const u8 sSelectHint[] = _("Choose living pairs\nin the companion.");
static const u8 sLocateHint[] = _("Check your PC and pairs.");
static const u8 sDuplicateHint[] = _("Check the original POKéMON.");
static const u8 sDaycareHint[] = _("Retrieve it from DAY CARE.");
static const u8 sOtherPairHint[] = _("Choose another living pair.");
static const u8 sMailHint[] = _("Remove its MAIL.");
static const u8 sTypeHint[] = _("Change the pair or type rule.");
static const u8 sPcHint[] = _("Make room in the PC.");
static const u8 *const sIssueHints[] = {sReconnectHint, sSelectHint, sSelectHint, sLocateHint, sDuplicateHint,
    sDaycareHint, sOtherPairHint, sOtherPairHint, sOtherPairHint, sMailHint, sTypeHint, sPcHint, sReconnectHint, sReconnectHint};
static const u8 sCheckTitle[] = _("Diagnostics");
static const u8 sLocalCheck[] = _("Checking your game.");
static const u8 sSlotsNeeded[] = _("PC slots needed:");
static const u8 sSlotsFree[] = _("Free:");
static const u8 sDuoTitle[] = _("Your duo");
static const u8 sDuoYou[] = _("YOUR TEAM");
static const u8 sDuoPartner[] = _("PARTNER");
static const u8 sDuoHelp[] = _("L/R: page SELECT: pairs B: back");
static const u8 sDuoFreshness[] = _("Last sync");
static const u8 sNoPair[] = _("NO PAIR");
static const u8 sHp[] = _("HP");
static const u8 sPairNumber[] = _("Pair: ");

enum { PAGE_HOME, PAGE_SETTINGS, PAGE_PAIRS, PAGE_DUO, PAGE_CHECK };
struct SoulLinkMenu
{
    u16 tilemap[32 * 32];
    struct SoulLinkConfig config;
    u16 pair;
    u8 page, cursor;
    bool8 closing;
    u8 text[96];
    const u8 *message;
    struct SoulLinkTeamDiagnostic diagnostic;
    u8 icons[6];
    u8 duoPage;
};
static EWRAM_DATA struct SoulLinkMenu *sSoulLinkMenu = NULL;
static const struct BgTemplate sBg = { .bg = 0, .charBaseIndex = 0, .mapBaseIndex = 31 };
static const struct WindowTemplate sWindows[] = {
    {.bg = 0, .tilemapLeft = 1, .tilemapTop = 1, .width = 28, .height = 18,
     .paletteNum = 15, .baseBlock = 1}, DUMMY_WIN_TEMPLATE
};
static const u16 sPalette[] = {RGB(2, 4, 8), RGB(29, 30, 31), RGB(3, 7, 12), RGB(17, 20, 23), RGB(31, 20, 4)};
static const u8 sColors[] = {1, 2, 3};
static const u8 sSelected[] = {1, 4, 3};
static const u8 sOn[] = _("ON"), sOff[] = _("OFF"), sNone[] = _("---");
static const u8 sTitle[] = _("SOUL LINK");
static const u8 sHomeHelp[] = _("A: open   B: back");
static const u8 sSaveHelp[] = _("A: apply   B: cancel");
static const u8 sEditHelp[] = _("LEFT/RIGHT: change   B: cancel");
static const u8 sSaved[] = _("Rules updated.\nSave in the game to keep them.");
static const u8 sNeedRun[] = _("Run PLAY.bat to set up\nyour duo.");
static const u8 sBlocked[] = _("This save cannot be changed.");
static const u8 sDuoOverview[] = _("Your duo");
static const u8 *const sHome[] = {sText_Settings, sDuoOverview, sCheckTitle, sText_Back};
static const u8 *const sSettings[] = {sText_Enabled, sText_LinkedDeaths, sText_SameAreaPairs, sText_FirstEncounter, sText_DupesClause, sText_TypeClause, sText_ApplyAndBack};
static const u8 *const sStates[] = {sText_Empty, sText_Pending, sText_Alive, sText_Dead, sText_Failed};

static void Draw(void);
static void Print(const u8 *text, u8 x, u8 y, bool8 selected)
{
    AddTextPrinterParameterized3(0, FONT_SMALL, x, y, selected ? sSelected : sColors, TEXT_SKIP_DRAW, text);
}

static void Number(u16 number, u8 x, u8 y)
{
    ConvertIntToDecimalStringN(sSoulLinkMenu->text, number, STR_CONV_MODE_LEFT_ALIGN, 5);
    Print(sSoulLinkMenu->text, x, y, FALSE);
}

static void DrawPair(void)
{
    struct SoulLinkPair pair;
    u16 section, count = SoulLink_GetPairCount();
    u8 local = gSaveBlock2Ptr->soulLink.header.localPlayer;
    if (!count || !SoulLink_ReadPair(sSoulLinkMenu->pair, &pair))
    {
        Print(sText_NoPairsYet, 4, 30, FALSE);
        Print(sText_CaptureThenSynchronizeWithYour, 4, 52, FALSE);
        Print(sText_BBack, 4, 130, FALSE); return;
    }
    Print(sText_PairId, 4, 20, FALSE); Number(pair.pairId, 48, 20);
    Number(sSoulLinkMenu->pair + 1, 148, 20); Print(sText_Slash, 178, 20, FALSE); Number(count, 186, 20);
    if (pair.areaId == SOUL_LINK_AREA_STARTER) StringCopy(sSoulLinkMenu->text, sText_Starter);
    else
    {
        for (section = 0; section < MAPSEC_COUNT; section++) if (NuzlockeLUT[section] == pair.areaId) break;
        if (section < MAPSEC_COUNT) GetMapName(sSoulLinkMenu->text, section, 0);
        else if (pair.areaId >= 0x4F && pair.areaId <= 0x56)
        {
            StringCopy(sSoulLinkMenu->text, sText_SafariZone);
            ConvertIntToDecimalStringN(sSoulLinkMenu->text + 12, pair.areaId - 0x4E, STR_CONV_MODE_LEFT_ALIGN, 1);
        }
        else { StringCopy(sSoulLinkMenu->text, sText_Area); ConvertIntToDecimalStringN(sSoulLinkMenu->text + 5, pair.areaId, STR_CONV_MODE_LEFT_ALIGN, 5); }
    }
    Print(sSoulLinkMenu->text, 4, 40, FALSE);
    Print(sText_You, 4, 62, FALSE);
    Print(pair.mons[local].species ? gSpeciesNames[pair.mons[local].species] : sNone, 64, 62, FALSE);
    Print(sText_Partner, 4, 82, FALSE);
    Print(pair.mons[local ^ 1].species ? gSpeciesNames[pair.mons[local ^ 1].species] : sNone, 64, 82, FALSE);
    Print(sStates[pair.state], 4, 104, TRUE);
    if (pair.cleanupFlags & (1 << local)) Print(sText_GraveTransferPending, 64, 104, FALSE);
    Print(sText_LeftRightPairBBack, 4, 130, FALSE);
}

static void ClearDuoIcons(void)
{
    u8 i;
    for (i = 0; i < ARRAY_COUNT(sSoulLinkMenu->icons); i++)
        if (sSoulLinkMenu->icons[i] != MAX_SPRITES)
        {
            DestroyMonIcon(&gSprites[sSoulLinkMenu->icons[i]]);
            sSoulLinkMenu->icons[i] = MAX_SPRITES;
        }
}

static bool8 GetPartyPair(u8 slot, struct SoulLinkPair *pair)
{
    struct SoulLinkMonIdentity identity;
    identity.species = GetMonData(&gPlayerParty[slot], MON_DATA_SPECIES, NULL);
    identity.otId = GetMonData(&gPlayerParty[slot], MON_DATA_OT_ID, NULL);
    identity.personality = GetMonData(&gPlayerParty[slot], MON_DATA_PERSONALITY, NULL);
    identity.encounterOrdinal = 0;
    return SoulLink_FindLocalPair(&identity, NULL, pair);
}

static u8 DuoIcon(u16 species, u32 personality, s16 x, s16 y)
{
    u8 id = CreateMonIcon(species, SpriteCB_MonIcon, x, y, 0, personality, FALSE);
    if (id < MAX_SPRITES) gSprites[id].oam.priority = 0;
    return id;
}

static void DrawDuo(void)
{
    u8 i, local = gSaveBlock2Ptr->soulLink.header.localPlayer;
    Print(sDuoTitle, 4, 0, TRUE);
    Number(sSoulLinkMenu->duoPage + 1, 190, 0); Print(sText_Slash, 202, 0, FALSE); Number(2, 212, 0);
    Print(sDuoYou, 4, 15, FALSE); Print(sDuoPartner, 118, 15, FALSE);
    for (i = 0; i < 3; i++)
    {
        struct SoulLinkPair pair;
        u8 slot = sSoulLinkMenu->duoPage * 3 + i, y = 32 + i * 32;
        u16 species = GetMonData(&gPlayerParty[slot], MON_DATA_SPECIES_OR_EGG, NULL);
        bool8 paired = GetPartyPair(slot, &pair);
        if (species != SPECIES_NONE && species < NUM_SPECIES)
        {
            sSoulLinkMenu->icons[i * 2] = DuoIcon(species, GetMonData(&gPlayerParty[slot], MON_DATA_PERSONALITY, NULL), 24, y + 20);
            Print(gSpeciesNames[species], 34, y, FALSE);
            Print(sHp, 34, y + 12, FALSE); Number(GetMonData(&gPlayerParty[slot], MON_DATA_HP, NULL), 48, y + 12);
            Print(sText_Slash, 70, y + 12, FALSE); Number(GetMonData(&gPlayerParty[slot], MON_DATA_MAX_HP, NULL), 78, y + 12);
        }
        else Print(sNone, 34, y, FALSE);
        if (paired && pair.mons[local ^ 1].species != SPECIES_NONE && pair.mons[local ^ 1].species < NUM_SPECIES)
        {
            sSoulLinkMenu->icons[i * 2 + 1] = DuoIcon(pair.mons[local ^ 1].species, pair.mons[local ^ 1].personality, 138, y + 20);
            Print(gSpeciesNames[pair.mons[local ^ 1].species], 148, y, FALSE);
        }
        else Print(sNone, 148, y, FALSE);
        if (paired) { Print(sStates[pair.state], 148, y + 12, pair.state != SOUL_LINK_PAIR_ALIVE); }
        else if (species != SPECIES_NONE) Print(sNoPair, 148, y + 12, TRUE);
    }
    Print(sDuoFreshness, 118, 119, FALSE);
    Print(sDuoHelp, 0, 130, FALSE);
}

static void CheckSelectedTeam(void)
{
    struct SoulLinkSaveHeader *header = &gSaveBlock2Ptr->soulLink.header;
    struct SoulLinkTeamDiagnostic *diagnostic = &sSoulLinkMenu->diagnostic;
    struct SoulLinkPair pair;
    u8 i, count = 0;
    memset(diagnostic, 0, sizeof(*diagnostic));
    if (header->config.enforcePartyPairs)
    {
        SoulLinkPairId ids[6];
        struct SoulLinkTeamPlan *plan;
        u16 j;
        for (i = 0; i < header->team.count; i++)
            for (j = 0; j < SoulLink_GetPairCount(); j++)
                if (SoulLink_ReadPair(j, &pair) && pair.pairId == header->team.pairIds[i] && pair.state == SOUL_LINK_PAIR_ALIVE)
                { ids[count++] = pair.pairId; break; }
        plan = AllocZeroed(sizeof(*plan));
        if (plan == NULL) diagnostic->reason = SOUL_LINK_TEAM_UNKNOWN;
        else
        {
            SoulLink_PrepareTeamChange(plan, ids, count);
            *diagnostic = plan->diagnostic;
            Free(plan);
        }
    }
    else
        for (i = 0; i < PARTY_SIZE; i++)
        {
            u16 species = GetMonData(&gPlayerParty[i], MON_DATA_SPECIES, NULL);
            if (species != SPECIES_NONE && !GetMonData(&gPlayerParty[i], MON_DATA_IS_EGG, NULL)
             && !SoulLink_CanUseSpecies(&gPlayerParty[i].box, species))
            {
                diagnostic->reason = SOUL_LINK_TEAM_TYPES; diagnostic->species = species;
                if (GetPartyPair(i, &pair)) diagnostic->pairId = pair.pairId;
                break;
            }
        }
}

static void DrawTeamCheck(void)
{
    struct SoulLinkTeamDiagnostic *diagnostic = &sSoulLinkMenu->diagnostic;
    u8 reason = diagnostic->reason;
    Print(sCheckTitle, 4, 0, TRUE);
    Print(sLocalCheck, 0, 17, FALSE);
    Print(sIssueNames[reason], 4, 38, reason != SOUL_LINK_TEAM_OK);
    if (diagnostic->pairId) { Print(sPairNumber, 4, 54, FALSE); Number(diagnostic->pairId, 40, 54); }
    if (diagnostic->species) Print(gSpeciesNames[diagnostic->species], 80, 54, FALSE);
    Print(sIssueHints[reason], 4, 74, FALSE);
    if (reason == SOUL_LINK_TEAM_PC_SPACE || reason == SOUL_LINK_TEAM_OK)
    {
        Print(sSlotsNeeded, 4, 96, FALSE); Number(diagnostic->neededSlots, 96, 96);
        if (reason == SOUL_LINK_TEAM_PC_SPACE) { Print(sSlotsFree, 122, 96, FALSE); Number(diagnostic->availableSlots, 186, 96); }
    }
    Print(sText_BBack, 4, 130, FALSE);
}

static void Draw(void)
{
    u8 i;
    ClearDuoIcons();
    FillWindowPixelBuffer(0, PIXEL_FILL(1));
    if (sSoulLinkMenu->page != PAGE_DUO && sSoulLinkMenu->page != PAGE_CHECK) Print(sTitle, 4, 0, TRUE);
    if (sSoulLinkMenu->message)
    {
        Print(sSoulLinkMenu->message, 2, 38, FALSE); Print(sText_ABContinue, 2, 130, FALSE);
    }
    else if (sSoulLinkMenu->page == PAGE_HOME)
    {
        for (i = 0; i < ARRAY_COUNT(sHome); i++) Print(sHome[i], 10, 22 + i * 17, i == sSoulLinkMenu->cursor);
        Print(sHomeHelp, 2, 130, FALSE);
    }
    else if (sSoulLinkMenu->page == PAGE_SETTINGS)
    {
        for (i = 0; i < ARRAY_COUNT(sSettings); i++)
        {
            Print(sSettings[i], 4, 20 + i * 14, i == sSoulLinkMenu->cursor);
            if (i < 5) Print(((u8 *)&sSoulLinkMenu->config)[i] ? sOn : sOff, 152, 20 + i * 14, i == sSoulLinkMenu->cursor);
            else if (i == 5) Print(sTypes[sSoulLinkMenu->config.typeClause], 152, 20 + i * 14, i == sSoulLinkMenu->cursor);
        }
        Print(sSoulLinkMenu->cursor == 6 ? sSaveHelp : sEditHelp, 2, 130, FALSE);
    }
    else if (sSoulLinkMenu->page == PAGE_DUO) DrawDuo();
    else if (sSoulLinkMenu->page == PAGE_CHECK) DrawTeamCheck();
    else DrawPair();
    PutWindowTilemap(0); CopyWindowToVram(0, COPYWIN_FULL);
}

static void VBlankCB_SoulLinkMenu(void) { LoadOam(); ProcessSpriteCopyRequests(); TransferPlttBuffer(); }

static void ExitMenu(void)
{
    sSoulLinkMenu->closing = TRUE;
    BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
}

static void SoulLinkMenu_HandleInput(void)
{
    u16 keys = gMain.newKeys;
    if (sSoulLinkMenu->message)
    {
        if (keys & (A_BUTTON | B_BUTTON)) { sSoulLinkMenu->message = NULL; Draw(); }
        return;
    }
    if (keys & B_BUTTON)
    {
        if (sSoulLinkMenu->page == PAGE_HOME) ExitMenu();
        else { sSoulLinkMenu->page = PAGE_HOME; sSoulLinkMenu->cursor = 0; Draw(); }
        return;
    }
    if (sSoulLinkMenu->page == PAGE_DUO)
    {
        if (keys & (DPAD_LEFT | DPAD_RIGHT)) sSoulLinkMenu->duoPage ^= 1;
        if (keys & SELECT_BUTTON) { sSoulLinkMenu->page = PAGE_PAIRS; sSoulLinkMenu->pair = 0; }
        if (keys) Draw(); return;
    }
    if (sSoulLinkMenu->page == PAGE_CHECK) return;
    if (sSoulLinkMenu->page == PAGE_PAIRS)
    {
        u16 count = SoulLink_GetPairCount();
        if (count && (keys & DPAD_LEFT)) sSoulLinkMenu->pair = (sSoulLinkMenu->pair + count - 1) % count;
        if (count && (keys & DPAD_RIGHT)) sSoulLinkMenu->pair = (sSoulLinkMenu->pair + 1) % count;
        if (keys) Draw(); return;
    }
    if (keys & DPAD_UP) sSoulLinkMenu->cursor = (sSoulLinkMenu->cursor + (sSoulLinkMenu->page == PAGE_HOME ? ARRAY_COUNT(sHome) - 1 : ARRAY_COUNT(sSettings) - 1)) % (sSoulLinkMenu->page == PAGE_HOME ? ARRAY_COUNT(sHome) : ARRAY_COUNT(sSettings));
    if (keys & DPAD_DOWN) sSoulLinkMenu->cursor = (sSoulLinkMenu->cursor + 1) % (sSoulLinkMenu->page == PAGE_HOME ? ARRAY_COUNT(sHome) : ARRAY_COUNT(sSettings));
    if (sSoulLinkMenu->page == PAGE_SETTINGS)
    {
        if (sSoulLinkMenu->cursor < 5 && (keys & (DPAD_LEFT | DPAD_RIGHT | A_BUTTON))) ((u8 *)&sSoulLinkMenu->config)[sSoulLinkMenu->cursor] ^= 1;
        else if (sSoulLinkMenu->cursor == 5 && (keys & (DPAD_LEFT | DPAD_RIGHT | A_BUTTON)))
            sSoulLinkMenu->config.typeClause = (sSoulLinkMenu->config.typeClause + ((keys & DPAD_LEFT) ? SOUL_LINK_TYPE_COUNT - 1 : 1)) % SOUL_LINK_TYPE_COUNT;
        else if ((keys & A_BUTTON) && sSoulLinkMenu->cursor == 6)
        {
            struct SoulLinkRunId run;
            if (sSoulLinkMenu->config.enabled && !SoulLink_GetRunId(&run)) sSoulLinkMenu->message = sNeedRun;
            else if (!SoulLink_SetConfig(&sSoulLinkMenu->config)) sSoulLinkMenu->message = sBlocked;
            else { sSoulLinkMenu->message = sSaved; sSoulLinkMenu->page = PAGE_HOME; sSoulLinkMenu->cursor = 0; }
        }
    }
    else if (keys & A_BUTTON)
    {
        if (sSoulLinkMenu->cursor == 0)
        {
            if (!SoulLink_GetConfig(&sSoulLinkMenu->config)) sSoulLinkMenu->message = sBlocked;
            else { sSoulLinkMenu->page = PAGE_SETTINGS; sSoulLinkMenu->cursor = 0; }
        }
        else if (sSoulLinkMenu->cursor == 1) { sSoulLinkMenu->page = PAGE_DUO; sSoulLinkMenu->duoPage = 0; }
        else if (sSoulLinkMenu->cursor == 2) { CheckSelectedTeam(); sSoulLinkMenu->page = PAGE_CHECK; }
        else ExitMenu();
    }
    if (keys && !sSoulLinkMenu->closing) Draw();
}

static void CB2_SoulLinkMenuMain(void)
{
    if (!gPaletteFade.active)
    {
        if (sSoulLinkMenu->closing)
        {
            ClearDuoIcons(); FreeMonIconPalettes();
            SetVBlankCallback(NULL); FreeAllWindowBuffers(); UnsetBgTilemapBuffer(0);
            Free(sSoulLinkMenu); sSoulLinkMenu = NULL;
            SetMainCallback2(CB2_ReturnToFieldWithOpenMenu);
            return;
        }
        SoulLinkMenu_HandleInput();
    }
    RunTasks(); AnimateSprites(); BuildOamBuffer(); UpdatePaletteFade();
}

void CB2_SoulLinkMenu(void)
{
    u8 i;
    sSoulLinkMenu = AllocZeroed(sizeof(*sSoulLinkMenu));
    if (!sSoulLinkMenu) { SetMainCallback2(CB2_ReturnToFieldWithOpenMenu); return; }
    for (i = 0; i < ARRAY_COUNT(sSoulLinkMenu->icons); i++) sSoulLinkMenu->icons[i] = MAX_SPRITES;
    SetVBlankCallback(NULL); SetHBlankCallback(NULL);
    DmaClearLarge16(3, (void *)VRAM, VRAM_SIZE, 0x1000);
    DmaClear32(3, (void *)OAM, OAM_SIZE);
    ResetTasks(); ResetSpriteData(); FreeAllSpritePalettes(); ResetPaletteFade(); ScanlineEffect_Stop();
    SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_MODE_0 | DISPCNT_OBJ_ON | DISPCNT_OBJ_1D_MAP);
    SetGpuReg(REG_OFFSET_BLDCNT, 0); SetGpuReg(REG_OFFSET_WININ, 0); SetGpuReg(REG_OFFSET_WINOUT, 0);
    ResetBgsAndClearDma3BusyFlags(0); InitBgsFromTemplates(0, &sBg, 1);
    SetBgTilemapBuffer(0, sSoulLinkMenu->tilemap); ChangeBgX(0, 0, BG_COORD_SET); ChangeBgY(0, 0, BG_COORD_SET);
    if (!InitWindows(sWindows))
    {
        FreeAllWindowBuffers(); UnsetBgTilemapBuffer(0); Free(sSoulLinkMenu); sSoulLinkMenu = NULL;
        SetMainCallback2(CB2_ReturnToFieldWithOpenMenu); return;
    }
    LoadMonIconPalettes();
    DeactivateAllTextPrinters();
    LoadPalette(sPalette, 15 * 16, sizeof(sPalette));
    LoadPalette(sPalette, 0, sizeof(u16));
    Draw(); ShowBg(0); CopyBgTilemapBufferToVram(0);
    BlendPalettes(PALETTES_ALL, 16, RGB_BLACK);
    BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
    SetVBlankCallback(VBlankCB_SoulLinkMenu); SetMainCallback2(CB2_SoulLinkMenuMain);
}
