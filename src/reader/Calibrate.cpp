#include "Calibrate.h"
#include "../../companion/MainDataOffsets.h"
#include "../../companion/SceneOffsets.h"
#include "../../companion/Signatures.h"
#include "../../companion/OpPrintsTable.h"

#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <fstream>
#include <sstream>

#pragma comment(lib, "version.lib")

namespace rtx::calib {
namespace {

// A rule: in the handler of `op`, the first instruction of the given shape whose displacement is
// in [lo, hi) is the offset, as long as no later one reads a different displacement. `head` is the
// instruction's bytes up to the displacement (opcode and ModRM with the root register as base and a
// 32-bit displacement); `nth` picks a later match when the first is a different field. `compiled`
// is set for a reference rule: an offset a compiled table carries, reported against that value and
// never applied (the reader's own offsets take theirs through Use()).
// `wrap`: the handler is a component setter wrapper and the shape sits in the callback it hands on.
// `span`: how far into the handler the instruction is looked for, for a handler that reads its field late.
// `call`: the shape sits in the routine the handler's n-th direct call names (0-based, in address
// order; only calls to a function the exe's unwind data knows count), for a field the handler reads
// through a helper. -1 reads the handler itself.
// A name may appear more than once: other operations that read the same field. The first rule that
// finds a value stands, so a build where one handler changed still calibrates; two rules that find
// different values cancel each other, since one of them reads some other field.
// `d8`: the displacement is one byte (a field below +0x80, which the compiler never encodes in four;
// also an immediate such as a stride). The range and the ambiguity rule apply as for four bytes.
// rel: lo and hi are offsets from the value the rule of that name found (a name earlier in the
// table); a rule whose anchor found nothing finds nothing.
struct Rule { const char* name; const char* op; const char* head; std::uint32_t lo, hi; int nth; std::uint32_t compiled = 0; bool wrap = false; const char* what = nullptr; std::uint32_t span = 0x140; int call = -1; bool d8 = false; const char* rel = nullptr; };
constexpr std::uint32_t kMdLo = 0x18000, kMdHi = 0x60000, kInLo = 0x1000, kInHi = 0x10000;
// The varp hash map sits inside the player var store: derived from md::kVarpMgr, never pinned alone.
constexpr std::uint32_t kVarpHashFromMgr = rtx::md::kVarpHashFromMgr;

// Root register rcx is the first argument of every handler; rax after `mov rax,[rcx+disp]` holds
// the object the offset points at, which the inner rules read through.
const Rule kRules[] = {
    // MainData fields
    { "kOffClientClock",   "CLIENTCLOCK",             "8B 91",    0x100,   0x2000,  0 },   // mov edx,[rcx+disp]
    { "kOffWorld",         "MAP_WORLD",               "48 8B 81", 0x18000, 0x60000, 0 },   // mov rax,[rcx+disp]
    { "kOffStatus",        "WORLDLIST_FETCH",         "8B 81",    0x18000, 0x60000, 0 },   // mov eax,[rcx+disp]
    { "kOffStats",         "STAT_BASE",               "48 8B 81", 0x18000, 0x60000, 0 },
    { "kStatsInner",       "STAT_BASE",               "48 8B 90", 0x1000,  0x10000, 0 },   // mov rdx,[rax+disp]
    { "kOffGE",            "STOCKMARKET_GETOFFERITEM","49 8B 82", 0x18000, 0x60000, 0 },   // mov rax,[r10+disp]
    { "kOffAccount",       "PLAYERMEMBER",            "48 8B 81", 0x18000, 0x60000, 0 },
    { "kOffVarcStore",     "GET_MOUSEX",              "48 8B 81", 0x18000, 0x60000, 0 },   // the store the varcs and the mouse share
    // references: MainDataOffsets.h
    { "md::kTracker",      "TELEMETRY_GET_GRID_VALUE",         "48 8B 81", kMdLo, kMdHi, 0, rtx::md::kTracker },
    { "md::kChatStore",    "CHAT_LASTUID",                     "48 8B 81", kMdLo, kMdHi, 0, rtx::md::kChatStore },
    { "md::kClanSettings", "ACTIVECLANSETTINGS_FIND_LISTENED", "48 8B 91", kMdLo, kMdHi, 0, rtx::md::kClanSettings },
    { "md::kMapMgr",       "MAP_LOADEDPERCENT",                "48 8B 81", kMdLo, kMdHi, 0, rtx::md::kMapMgr },
    { "md::kMapMgr",       "MAP_PRELOAD",                      "48 8B 89", kMdLo, kMdHi, 0, rtx::md::kMapMgr },        // mov rcx,[rcx+disp]; the loaded-percent handler changed on the plugin client
    { "md::kInputReport",  "AUTOSETUP_SETCUSTOM",              "48 8B 83", kMdLo, kMdHi, 0, rtx::md::kInputReport },   // mov rax,[rbx+disp]
    { "md::kIfaceOwner",   "IF_CLOSE",                         "48 8B 89", kMdLo, kMdHi, 0, rtx::md::kIfaceOwner },
    { "md::kInputProc",    "MINIMENUOPEN",                     "48 8B 89", kMdLo, kMdHi, 0, rtx::md::kInputProc },
    { "md::kPlayerGroup",  "PLAYER_GROUP_MEMBER_COUNT",        "48 8B 81", kMdLo, kMdHi, 0, rtx::md::kPlayerGroup },
    { "md::kPlayers",      "COORD",                            "48 8B 88", kMdLo, kMdHi, 0, rtx::md::kPlayers },        // mov rcx,[rax+disp]
    { "md::kFriends",      "FRIEND_COUNT",                     "4C 8B 89", kMdLo, kMdHi, 0, rtx::md::kFriends },
    { "md::kContainers",   "INV_TOTALCAT",                     "48 8B 89", kMdLo, kMdHi, 0, rtx::md::kContainers },
    { "md::kContainers",   "INV_FREESPACE",                    "48 8B 8D", kMdLo, kMdHi, 0, rtx::md::kContainers },    // mov rcx,[rbp+disp]; INV_TOTALCAT is not named by its code on the plugin client
    { "md::kSceneViews",   "UNKNOWN_COMMAND_500",              "48 8B 81", kMdLo, kMdHi, 0, rtx::md::kSceneViews },
    { "md::kCutscene",     "CUTSCENE2D_STOP",                  "48 8B 89", kMdLo, kMdHi, 0, rtx::md::kCutscene },
    { "md::kSoundCtx",     "SOUND_SYNTH",                      "48 8B 89", kMdLo, kMdHi, 0, rtx::md::kSoundCtx },
    { "md::kLanguage",     "MAP_LANG",                         "48 8B 81", kMdLo, kMdHi, 0, rtx::md::kLanguage },
    { "md::kVarpMgr",      "QUEST_FINISHED",                   "48 8D 97", kMdLo, kMdHi, 0, rtx::md::kVarpMgr },        // lea rdx,[rdi+disp]
    { "md::kOptions",      "CLIENTOPTION_GET",                 "48 8B 81", kMdLo, kMdHi, 0, rtx::md::kOptions },
    { "md::kConfigs",      "QUEST_GETDIFFICULTY",              "4C 8B 81", kMdLo, kMdHi, 0, rtx::md::kConfigs },        // mov r8,[rcx+disp]
    { "md::kDisplay",      "WINDOW_GETINSETS",                 "48 8B 89", kMdLo, kMdHi, 0, rtx::md::kDisplay },
    { "md::kConnection",   "ABORT_DIALOG",                     "48 8B 91", kMdLo, kMdHi, 0, rtx::md::kConnection },
    { "md::kFriendsChat",  "CLAN_GETCHATCOUNT",                "48 8B 81", kMdLo, kMdHi, 0, rtx::md::kFriendsChat },
    { "md::kLoginMgr",     "LOGIN_REPLY",                      "48 8B 81", kMdLo, kMdHi, 0, rtx::md::kLoginMgr },
    { "md::kWorldMapMgr",  "WORLDMAP_GETCURRENTMAP",           "48 8B 81", kMdLo, kMdHi, 0, rtx::md::kWorldMapMgr },
    { "md::kServerTimeOffset", "DATE_RUNEDAY",                 "48 F7 A9", kMdLo, kMdHi, 0, rtx::md::kServerTimeOffset },   // imul [rcx+disp]
    { "md::kCountry",      "PLAYERCOUNTRY",                    "8B 91",    kMdLo, kMdHi, 0, rtx::md::kCountry },        // mov edx,[rcx+disp]
    { "md::kWorldInfo",    "REBOOTTIMER",                      "48 8B 81", kMdLo, kMdHi, 0, rtx::md::kWorldInfo },
    { "md::kFps",          "FPS_STATS",                        "F3 44 0F 2C 89", 0x100, 0x2000, 0, rtx::md::kFps },   // cvttss2si r9d,[rcx+disp]
    { "md::kVarWatch",     "popvar",                           "49 8B 9E", kMdLo, kMdHi, 0, rtx::md::kVarWatch, false, nullptr, 0x280 },   // mov rbx,[r14+disp], after the var is looked up
    // references: offsets written out in the reader and the companion
    { "dbMgr",             "DB_FIND_GET",                      "48 8B B1", kMdLo, kMdHi, 0, 0x19998, false, "ScriptRunner" },
    { "dbOther",           "DB_LISTALL",                       "48 8B 89", kMdLo, kMdHi, 0, 0x198C8, false, "DbDatabase" },
    // the script string stack: cc_settext's callback adds the stack base to the state and counts the pointer down
    { "stringStack",       "CC_SETTEXT",                       "49 81 C1", kInLo, kInHi, 0, 0x10A8, true, "string stack base in the script state" },
    { "stringSp",          "CC_SETTEXT",                       "41 FF 89", kInLo, kInHi, 0, 0x8DA8, true, "string stack pointer in the script state" },
    { "varcMouseX",        "GET_MOUSEX",                       "44 0F 2C 80", kInLo, kInHi, 0, 0x43A8 },   // cvttss2si r8d,[rax+disp], in the store
    { "varcMouseY",        "GET_MOUSEY",                       "44 0F 2C 80", kInLo, kInHi, 0, 0x43AC },
    { "varcButtons",       "GET_MOUSEBUTTONS",                 "44 38 80", kInLo, kInHi, 0, 0x43B8 },
    { "varcClan",          "CLANPROFILE_FIND",                 "48 39 90", kInLo, kInHi, 0, 0x7480 },
    { "stateEntityRef",    "GET_ENTITY_BOUNDING_BOX",          "48 8B 9A", kInLo, kInHi, 0, 0xC3B8 },      // the script state (rdx)
    { "stateEntityObj",    "GET_ENTITY_OVERLAY_HEIGHT",        "48 8B 8A", kInLo, kInHi, 0, 0xC3C0 },
    { "scn::kCombat",      "COMLEVEL_ACTIVE",                  "8B 91",    kInLo, kInHi, 0, (std::uint32_t)rtx::scn::kCombat },   // in the player's actor
    // references: MainData fields the reader and the companion read with no rule until now. Every
    // operation below is named by its handler's own code on the builds checked (unk<id> is the
    // export's name for an operation the official list leaves unnamed; the id is the cache's own
    // and does not change between builds).
    { "md::kMiniMap",      "unk11190",                         "48 8B 81", kMdLo, kMdHi, 0, rtx::md::kMiniMap, false, "minimap: mask shape +0x38, walk destination +0x14/+0x18/+0x1C" },
    { "md::kWalkMgr",      "unk11014",                         "48 8B 89", kMdLo, kMdHi, 0, rtx::md::kWalkMgr, false, "walk marker appearance: ids +0x68/+0x6C, sprite +0x70, model +0x80" },
    { "md::kEntityLookup", "unk11164",                         "48 8B 89", kMdLo, kMdHi, 0, rtx::md::kEntityLookup, false, "entity lookup by uid (the zone event registry)" },   // the handler's one mov rcx,[rcx+disp]: the lookup
    { "md::kOptionsArray", "CLIENTOPTION_GET",                 "48 8B 8C F8", kInLo, kInHi, 0, 0x2DA8, false, "client option objects in the options object, 8 bytes per id" },
    { "md::kOptionsArray", "unk11116",                         "48 8B 8C C8", kInLo, kInHi, 0, 0x2DA8 },
    { "map.loadA",         "MAP_LOADEDPERCENT",                "44 8B 80", 0x700, 0x800, 0, 0x740, false, "map manager loading counter (reader data.input2)" },
    { "map.loadB",         "MAP_LOADEDPERCENT",                "8B 90",    0x700, 0x800, 0, 0x744, false, "map manager loading counter (reader data.input2)" },
    { "map.loadScreen",    "MAP_LOADINGSCREEN_ISOPEN",         "0F B6 90", 0x700, 0x800, 0, 0x74C, false, "map manager loading screen flag (reader data.input2)" },
    // the actor (scene entity object): what the NPC ops read through the state's entity slot
    { "scn::kConfig",      "unk11134",                         "8B 90",    kInLo, kInHi, 0, (std::uint32_t)rtx::scn::kConfig, false, "NPC base type id in the actor" },
    { "npc.visLevel",      "CHATCAT_UNK11187",                 "8B 90",    kInLo, kInHi, 0, 0x1188, false, "NPC visible combat level in the actor (the reader's kNpcVisLevel)" },
    { "npc.statCur",       "unk11135",                         "8B 8C 81", kInLo, kInHi, 0, 0x1150, false, "NPC current stats in the actor, 4 bytes per stat; life points at +0xC (scn::kLpCur)" },
    { "npc.statBase",      "unk11135",                         "44 8B 84 81", kInLo, kInHi, 0, 0x116C, false, "NPC base stats in the actor; life points at +0xC (scn::kLpMax)" },
    { "actor.npcType",     "unk11136",                         "48 8B 88", kInLo, kInHi, 0, 0x10B8, false, "NPC type object in the actor (the reader's overhead height chain)" },   // 0x10A8 on 950-1; between scn::kNpcCur and scn::kCombat, both moved +0x10
    { "actor.nameString",  "CHAT_PLAYERNAME",                  "48 8D 91", 0x80, 0x100, 0, 0x90, false, "the actor's name string object; the reader's kName 0xB8 is its inline buffer", 0x140, 0 },
    // the game world (worldView): the screen-position helper's matrix reads and the area box
    { "wv.projDepth",      "GET_OBJ_SCREEN_POSITION",          "F3 41 0F 59 A8", 0x13000, 0x14000, 0, 0x13808, false, "view-projection depth column in the worldView (kMatrixOffDefault + 0x98)", 0x140, 1 },
    { "wv.areaEntries",    "unk10994",                         "4C 2B 81", 0xE0000, 0xF0000, 0, (std::uint32_t)rtx::scn::kWvBox + 0x28, false, "area box: entries vector (kWvBox + 0x28)" },
    { "wv.areaEntriesEnd", "unk10994",                         "4C 8B 81", 0xE0000, 0xF0000, 0, (std::uint32_t)rtx::scn::kWvBox + 0x30, false, "area box: entries vector end (kWvBox + 0x30)" },
    { "wv.areaSpawnFlag",  "TEXTINPUT_BLUR",                   "80 B9",    0xE8000, 0xE9000, 0, (std::uint32_t)rtx::scn::kWvBox + 0x40, false, "area box: spawn point set flag (kWvBox + 0x40)" },
    { "wv.areaSpawn",      "TEXTINPUT_BLUR",                   "F2 0F 11 81", 0xE8000, 0xE9000, 0, (std::uint32_t)rtx::scn::kWvBox + 0x4C, false, "area box: spawn point (kWvBox + 0x4C)" },
    // both corners are read with the same head, six compares in the order min x, max x, min y, max y,
    // min z, max z: each window sits on the spawn flag the row above found (flag + 0x30 / + 0x3C)
    { "wv.areaMin",        "unk10993",                         "3B 91",    0x30, 0x3C, 0, (std::uint32_t)rtx::scn::kWvBox + 0x70, false, "area box: min corner (kWvBox + 0x70, spawn flag + 0x30)", 0x200, -1, false, "wv.areaSpawnFlag" },
    { "wv.areaMax",        "unk10993",                         "3B 91",    0x3C, 0x48, 0, (std::uint32_t)rtx::scn::kWvBox + 0x7C, false, "area box: max corner (kWvBox + 0x7C, spawn flag + 0x3C)", 0x200, -1, false, "wv.areaSpawnFlag" },
    // the script state and the interface manager
    { "state.settingsCtx", "ACTIVECLANSETTINGS_GETAFFINEDCOUNT", "48 8B 82", 0xC000, 0xD000, 0, 0xC830, false, "clan settings context in the script state (the companion's settings context)" },
    { "state.settingsCtx", "ACTIVECLANSETTINGS_GETCLANNAME",   "48 8B 92", 0xC000, 0xD000, 0, 0xC830 },
    { "state.settingsCtx", "ACTIVECLANSETTINGS_FIND_LISTENED", "48 8B 9F", 0xC000, 0xD000, 0, 0xC830 },
    { "iface.subBuckets",  "IF_HASSUB",                        "48 8B 99", 0x80, 0x200, 0, 0x118, false, "interface owner: sub table buckets (the reader's kIfaceSubBuckets)", 0x140, 0 },
    { "iface.subCount",    "IF_HASSUB",                        "44 8B 89", 0x80, 0x200, 0, 0x120, false, "interface owner: sub table bucket count", 0x140, 0 },
    // the component object (the reader's interface node + 0x20): the getter and setter callbacks
    // the cc_ and if_ wrappers hand on
    { "iface.compWidth",   "CC_GETWIDTH",                      "8B 90",    0x80, 0x84, 0, 0x80, true, "component width; the reader's node offset is this + 0x20" },
    { "iface.compWidth",   "IF_GETWIDTH",                      "8B 90",    0x80, 0x84, 0, 0x80, true },
    { "iface.compHeight",  "CC_GETHEIGHT",                     "8B 90",    0x84, 0x88, 0, 0x84, true, "component height (node offset = this + 0x20)" },
    { "iface.compHeight",  "IF_GETHEIGHT",                     "8B 90",    0x84, 0x88, 0, 0x84, true },
    { "iface.compColour",  "CC_GETCOLOUR",                     "8B 90",    0x80, 0x100, 0, 0x88, true, "component colour (node offset = this + 0x20); the transparency byte follows it" },
    { "iface.compColour",  "CC_SETCOLOUR",                     "44 89 88", 0x80, 0x100, 0, 0x88, true },
    { "iface.compSprite",  "IF_GETGRAPHIC",                    "48 8D 81", 0x188, 0x190, 0, 0x188, true, "graphic sub-object of component kinds 3 and 6 (node offset = this + 0x20); its first dword is the sprite id" },
    { "iface.compKey",     "IF_GETGRAPHIC",                    "48 8D 81", 0x190, 0x198, 0, 0x190, true, "graphic sub-object of the text kind (node offset = this + 0x20)" },
    // the minimenu: its entry vector and the left-click record slots
    { "menu.entriesBegin", "unk11099",                         "48 2B 90", 0x80, 0x100, 0, 0x90, false, "minimenu entry vector begin (MenuProbe kEntriesBegin)" },
    { "menu.entriesEnd",   "unk11099",                         "48 8B 90", 0x80, 0x100, 0, 0x98, false, "minimenu entry vector end (MenuProbe kEntriesEnd)" },
    { "menu.slotLeft",     "MINIMENU_CLOSE",                   "48 8B BB", 0x13E0, 0x13F0, 0, rtx::sig::kMenuLeftClickSlot, false, "minimenu left-click record slot (the lift's slot)", 0x140, 0 },
    { "menu.slotLeft",     "FORMATMINIMENU",                   "48 8B BB", 0x13E0, 0x13F0, 0, rtx::sig::kMenuLeftClickSlot, false, nullptr, 0x140, 0 },
    { "menu.slotSecond",   "MINIMENU_CLOSE",                   "48 8B BB", 0x1400, 0x1410, 0, 0x1408, false, "minimenu second option record slot", 0x140, 0 },
    { "menu.slotSecond",   "FORMATMINIMENU",                   "48 8B BB", 0x1400, 0x1410, 0, 0x1408, false, nullptr, 0x140, 0 },
    // the chat store
    { "chat.buckets",      "unk11195",                         "49 8B 9B", 0x800, 0x900, 0, 0x8C0, false, "chat store: record hash buckets (reader chat_store_json, companion Unlink)" },
    { "chat.buckets",      "printmessage",                     "4D 8B 96", 0x800, 0x900, 0, 0x8C0, false, nullptr, 0x200, 4 },
    { "chat.bucketCount",  "unk11195",                         "41 8B 8B", 0x800, 0x900, 0, 0x8C8, false, "chat store: bucket count" },
    { "chat.bucketCount",  "printmessage",                     "41 8B 8E", 0x800, 0x900, 0, 0x8C8, false, nullptr, 0x200, 4 },
    { "chat.recordCount",  "printmessage",                     "48 FF 8E", 0x800, 0x900, 0, 0x8D0, false, "chat store: record count", 0x300, 5 },
    { "chat.recordNext",   "unk11195",                         "48 8B 80", 0x80, 0x100, 0, 0xB8, false, "chat record: next in the bucket chain" },
    { "chat.recordNext",   "printmessage",                     "48 8B 80", 0x80, 0x100, 0, 0xB8, false, nullptr, 0x200, 4 },
    { "chat.recordText",   "printmessage",                     "0F 10 9A", 0x80, 0x100, 0, 0x90, false, "chat record: message text", 0x400, 4 },
    { "chat.recordText",   "printmessage",                     "0F 10 99", 0x80, 0x100, 0, 0x90, false, nullptr, 0x400, 4 },   // 951-1 fills the record through rcx
    // One-byte fields (d8). The game world list, the account and the players registry: the chain
    // the scene root and the local player are read through.
    { "scn::kActiveIdx",   "unk10994",                   "49 63 41", 0x60, 0x80, 0, (std::uint32_t)rtx::scn::kActiveIdx, false, "game world list: i32 current world index", 0x140, -1, true },
    { "scn::kActiveIdx",   "unk11253",                   "48 63 42", 0x60, 0x80, 0, (std::uint32_t)rtx::scn::kActiveIdx, false, nullptr, 0x140, -1, true },
    { "scn::kActiveIdx",   "TEXTINPUT_BLUR",             "48 63 42", 0x60, 0x80, 0, (std::uint32_t)rtx::scn::kActiveIdx, false, nullptr, 0x140, -1, true },
    { "scn::kEntryArr",    "unk10994",                   "49 03 41", 0x50, 0x60, 0, (std::uint32_t)rtx::scn::kEntryArr, false, "game world list: vector of 0x10-byte world handles", 0x140, -1, true },
    { "scn::kEntryArr",    "unk11253",                   "48 03 42", 0x50, 0x60, 0, (std::uint32_t)rtx::scn::kEntryArr, false, nullptr, 0x140, -1, true },
    { "scn::kEntryArr",    "TEXTINPUT_BLUR",             "48 03 42", 0x50, 0x60, 0, (std::uint32_t)rtx::scn::kEntryArr, false, nullptr, 0x140, -1, true },
    { "scn::kEntryWv",     "unk10994",                   "48 8B 48", 0x8, 0x10, 0, (std::uint32_t)rtx::scn::kEntryWv, false, "world handle: the game world (worldView)", 0x140, -1, true },
    { "scn::kEntryWv",     "unk11253",                   "48 8B 40", 0x8, 0x10, 0, (std::uint32_t)rtx::scn::kEntryWv, false, nullptr, 0x140, -1, true },
    { "scn::kEntryWv",     "TEXTINPUT_BLUR",             "48 8B 48", 0x8, 0x10, 0, (std::uint32_t)rtx::scn::kEntryWv, false, nullptr, 0x140, -1, true },
    { "scn::kLocalUid",    "COMLEVEL_ACTIVE",            "48 63 48", 0x40, 0x50, 0, (std::uint32_t)rtx::scn::kLocalUid, false, "account: i32 local player index", 0x140, -1, true },
    { "scn::kLocalUid",    "COORD",                      "48 63 48", 0x40, 0x50, 0, (std::uint32_t)rtx::scn::kLocalUid, false, nullptr, 0x140, -1, true },
    { "scn::kLocalUid",    "CHAT_PLAYERNAME_UNFILTERED", "49 63 40", 0x40, 0x50, 0, (std::uint32_t)rtx::scn::kLocalUid, false, nullptr, 0x140, -1, true },
    { "scn::kLocalUid",    "GENDER",                     "49 63 40", 0x40, 0x50, 0, (std::uint32_t)rtx::scn::kLocalUid, false, nullptr, 0x140, -1, true },
    { "players.table",     "COMLEVEL_ACTIVE",            "48 8B 41", 0x10, 0x18, 0, 0x10, false, "players registry: entry table, entry = [table + index * 8]", 0x140, -1, true },
    { "players.table",     "COORD",                      "48 8B 41", 0x10, 0x18, 0, 0x10, false, nullptr, 0x140, -1, true },
    { "players.table",     "CHAT_PLAYERNAME_UNFILTERED", "48 8B 41", 0x10, 0x18, 0, 0x10, false, nullptr, 0x140, -1, true },
    { "actor.posSlot",     "COORD",                            "FF 90",    0x40, 0x400, 0, 0xB0, false, "actor vtable slot of the position getter (lea rax,[rcx+position]; ret)" },
    { "players.entryActor", "CHAT_PLAYERNAME_UNFILTERED", "48 83 C0", 0x38, 0x40, 0, 0x38, false, "player entry: the actor (counted pair at +0x30, object at +8)", 0x140, -1, true },
    { "players.entryActor", "CHAT_PLAYERNAME",           "48 83 C0", 0x38, 0x40, 0, 0x38, false, nullptr, 0x140, -1, true },
    { "players.entryActor", "CLAN_ISSELF",               "48 83 C0", 0x38, 0x40, 0, 0x38, false, nullptr, 0x140, -1, true },
    { "account.member",    "PLAYERMEMBER",               "80 78",    0x20, 0x30, 0, 0x28, false, "account: u8 members flag (the reader's kOffAcctIsMember)", 0x140, -1, true },
    { "account.member",    "FRIEND_ADD",                 "80 78",    0x20, 0x30, 0, 0x28, false, nullptr, 0x140, -1, true },
    { "account.expiry",    "USERDETAIL_LOBBY_MEMBERSHIP", "4C 8B 47", 0x30, 0x38, 0, 0x30, false, "account: u64 membership end in ms (the reader's kOffAcctExpiry)", 0x140, -1, true },
    { "account.name",      "CHAT_PLAYERNAME",            "4D 8D 48", 0x60, 0x70, 0, 0x68, false, "account: display name string", 0x140, -1, true },
    { "account.name",      "CHAT_PLAYERNAME_UNFILTERED", "4D 8D 48", 0x60, 0x70, 0, 0x68, false, nullptr, 0x140, -1, true },
    { "account.name",      "CLAN_ISSELF",                "4C 8D 49", 0x60, 0x70, 0, 0x68, false, nullptr, 0x140, -1, true },
    { "scn::kWalkResX",    "unk11014",                   "39 51",    0x60, 0x70, 0, (std::uint32_t)rtx::scn::kWalkResX, false, "walk marker manager: i32 custom sprite id", 0x140, 0, true },
    { "scn::kWalkResX",    "unk11014",                   "89 51",    0x60, 0x70, 0, (std::uint32_t)rtx::scn::kWalkResX, false, nullptr, 0x140, 0, true },
    { "scn::kWalkResY",    "unk11014",                   "39 7D",    0x60, 0x70, 0, (std::uint32_t)rtx::scn::kWalkResY, false, "walk marker manager: i32 custom model id", 0x140, 0, true },
    { "scn::kWalkResY",    "unk11014",                   "89 7D",    0x60, 0x70, 0, (std::uint32_t)rtx::scn::kWalkResY, false, nullptr, 0x140, 0, true },
    // the component object, its group and the interface owner
    { "iface.compX",       "IF_GETX",                    "8B 50",    0x78, 0x7C, 0, 0x78, true, "component x (node offset = this + 0x20)", 0x140, -1, true },
    { "iface.compX",       "CC_GETX",                    "8B 50",    0x78, 0x7C, 0, 0x78, true, nullptr, 0x140, -1, true },
    { "iface.compY",       "IF_GETY",                    "8B 50",    0x7C, 0x80, 0, 0x7C, true, "component y (node offset = this + 0x20)", 0x140, -1, true },
    { "iface.compY",       "CC_GETY",                    "8B 50",    0x7C, 0x80, 0, 0x7C, true, nullptr, 0x140, -1, true },
    { "iface.compGroup",   "IF_GETLAYER",                "0F B7 48", 0x18, 0x1A, 0, 0x18, false, "component: u16 interface id (node offset = this + 0x20)", 0x140, -1, true },
    { "iface.compGroup",   "CC_GETLAYER",                "0F B7 41", 0x18, 0x1A, 0, 0x18, false, nullptr, 0x140, -1, true },
    { "iface.compGroup",   "UNKNOWN_COMMAND_120",        "41 0F B7 48", 0x18, 0x1A, 0, 0x18, false, nullptr, 0x140, -1, true },
    { "iface.compId",      "UNKNOWN_COMMAND_120",        "41 0F B7 48", 0x1A, 0x1C, 0, 0x1A, false, "component: u16 component id", 0x140, -1, true },
    { "iface.compId",      "IF_SETMODEL",                "0F B7 51", 0x1A, 0x1C, 0, 0x1A, true, nullptr, 0x140, -1, true },
    { "iface.compId",      "CC_SETMODELANIM",            "0F B7 51", 0x1A, 0x1C, 0, 0x1A, true, nullptr, 0x140, -1, true },
    { "iface.compId",      "IF_SETTEXTFONT",             "0F B7 51", 0x1A, 0x1C, 0, 0x1A, true, nullptr, 0x140, -1, true },
    { "iface.compId",      "IF_GET_GAMESCREEN",          "0F B7 49", 0x1A, 0x1C, 0, 0x1A, false, nullptr, 0x140, -1, true },
    { "iface.compSub",     "CC_GETID",                   "0F B7 48", 0x1C, 0x1E, 0, 0x1C, false, "component: u16 dynamic sub id (0xFFFF for a static one)", 0x140, -1, true },
    { "iface.compSub",     "CC_DELETE",                  "41 0F B7 4E", 0x1C, 0x1E, 0, 0x1C, false, nullptr, 0x140, -1, true },
    { "iface.compSub",     "CC_FINDBYCATEGORY",          "0F B7 48", 0x1C, 0x1E, 0, 0x1C, false, nullptr, 0x140, 1, true },
    { "iface.compParent",  "IF_GETLAYER",                "0F B7 48", 0x1E, 0x20, 0, 0x1E, false, "component: u16 parent layer id", 0x140, -1, true },
    { "iface.compParent",  "CC_GETLAYER",                "0F B7 41", 0x1E, 0x20, 0, 0x1E, false, nullptr, 0x140, -1, true },
    { "iface.compTrans",   "CC_SETTRANS",                "88 8A",    0x80, 0x100, 0, 0x8C, true, "component transparency byte, kept as 255 - trans (node offset = this + 0x20)" },
    { "iface.compTrans",   "IF_SETTRANS",                "88 8A",    0x80, 0x100, 0, 0x8C, true },
    { "iface.compTrans",   "CC_GETTRANS",                "0F B6 88", 0x80, 0x100, 0, 0x8C, true },
    { "iface.compTrans",   "IF_GETTRANS",                "0F B6 88", 0x80, 0x100, 0, 0x8C, true },
    { "iface.ownerMap",    "IF_GETLAYER",                "48 83 C1", 0x38, 0x3C, 0, 0x38, false, "interface owner: map of the open interfaces by id", 0x140, -1, true },
    { "iface.ownerMap",    "CC_FINDBYCATEGORY",          "48 83 C1", 0x38, 0x3C, 0, 0x38, false, nullptr, 0x140, -1, true },
    { "iface.groupCompsBegin", "IF_GETLAYER",            "4C 8B 49", 0x8, 0xC, 0, 0x8, false, "interface: component entry vector begin, one entry per component id", 0x140, -1, true },
    { "iface.groupCompsBegin", "CC_FINDBYCATEGORY",      "4C 8B 49", 0x8, 0xC, 0, 0x8, false, nullptr, 0x140, -1, true },
    { "iface.groupCompsEnd", "IF_GETLAYER",              "48 8B 49", 0x10, 0x14, 0, 0x10, false, "interface: component entry vector end", 0x140, -1, true },
    { "iface.groupCompsEnd", "CC_FINDBYCATEGORY",        "48 8B 49", 0x10, 0x14, 0, 0x10, false, nullptr, 0x140, -1, true },
    { "iface.entryStride", "CC_FINDBYCATEGORY",          "4D 8D 42", 0x18, 0x20, 0, 0x18, false, "component entry size: hidden byte +0, control block (the reader's node) +8, component +0x10", 0x140, 1, true },
    { "iface.entryStride", "IF_GETNEXTSUBID",            "49 8D 71", 0x18, 0x20, 0, 0x18, false, nullptr, 0x140, 1, true },
    { "iface.entryStride", "IF_GETNEXTSUBID",            "48 83 C1", 0x18, 0x20, 0, 0x18, false, nullptr, 0x400, 1, true },
    { "iface.entryComp",   "CC_FINDBYCATEGORY",          "49 8B 40", 0x10, 0x14, 0, 0x10, false, "component entry: the component object", 0x140, 1, true },
    { "iface.entryComp",   "IF_GETNEXTSUBID",            "48 8B 41", 0x10, 0x14, 0, 0x10, false, nullptr, 0x400, 1, true },
    { "iface.entryRef",    "CC_FINDBYCATEGORY",          "49 8D 40", 0x8, 0xC, 0, 0x8, false, "component entry: counted pair {control block, component}", 0x140, 1, true },
    // the minimenu: open flag, row lanes, entry records and their display objects
    { "menu.open",         "UNKNOWN_COMMAND_173",        "80 7B",    0x60, 0x70, 0, 0x68, false, "minimenu: open flag byte", 0x140, -1, true },
    { "menu.open",         "MINIMENUOPEN",               "80 79",    0x60, 0x70, 0, 0x68, false, nullptr, 0x140, 0, true },
    { "menu.open",         "MINIMENU_CLOSE",             "C6 43",    0x60, 0x70, 0, 0x68, false, nullptr, 0x140, 0, true },
    { "menu.open",         "FORMATMINIMENU",             "C6 43",    0x60, 0x70, 0, 0x68, false, nullptr, 0x140, 0, true },
    { "menu.open",         "unk11164",                   "80 7A",    0x60, 0x70, 0, 0x68, false, nullptr, 0x140, -1, true },
    { "menu.recentBegin",  "MINIMENU_CLOSE",             "48 8B 8B", 0x1400, 0x1500, 0, 0x1470, false, "minimenu: recent rows lane begin (16-byte records)", 0x200, 0 },
    { "menu.recentBegin",  "FORMATMINIMENU",             "48 8B 8B", 0x1400, 0x1500, 0, 0x1470, false, nullptr, 0x200, 0 },
    { "menu.recentEnd",    "MINIMENU_CLOSE",             "48 8B 93", 0x1400, 0x1500, 0, 0x1478, false, "minimenu: recent rows lane end", 0x200, 0 },
    { "menu.recentEnd",    "FORMATMINIMENU",             "48 8B 93", 0x1400, 0x1500, 0, 0x1478, false, nullptr, 0x200, 0 },
    { "menu.groupsBegin",  "UNKNOWN_COMMAND_174",        "48 8B 90", 0x1700, 0x1800, 0, 0x1798, false, "minimenu: groups lane begin (16-byte records)" },
    { "menu.groupsBegin",  "MINIMENU_CLOSE",             "48 8B 8B", 0x1700, 0x1800, 0, 0x1798, false, nullptr, 0x200, 0 },
    { "menu.groupsEnd",    "UNKNOWN_COMMAND_174",        "48 8B 88", 0x1700, 0x1800, 0, 0x17A0, false, "minimenu: groups lane end" },
    { "menu.groupsEnd",    "MINIMENU_CLOSE",             "48 8B 93", 0x1700, 0x1800, 0, 0x17A0, false, nullptr, 0x200, 0 },
    { "menu.entryDisplay", "UNKNOWN_COMMAND_180",        "48 8B 50", 0x8, 0x10, 0, 0x8, false, "minimenu entry record (16 bytes): the display object at +8, its control block at +0", 0x140, -1, true },
    { "menu.entryDisplay", "UNKNOWN_COMMAND_181",        "48 8B 48", 0x8, 0x10, 0, 0x8, false, nullptr, 0x140, -1, true },
    { "menu.entryDisplay", "MINIMENUOPEN",               "48 8B 48", 0x8, 0x10, 0, 0x8, false, nullptr, 0x140, 0, true },
    { "menu.entryDisplay", "UNKNOWN_COMMAND_175",        "48 8B 68", 0x8, 0x10, 0, 0x8, false, nullptr, 0x140, -1, true },
    { "menu.classTag",     "UNKNOWN_COMMAND_180",        "48 8B 42", 0x30, 0x40, 0, 0x38, false, "minimenu display object: class tag (MenuProbe kDispTag; the verb string is at +0x18)", 0x140, -1, true },
    { "menu.classTag",     "UNKNOWN_COMMAND_181",        "48 39 41", 0x30, 0x40, 0, 0x38, false, nullptr, 0x140, -1, true },
    { "menu.classTag",     "MINIMENUOPEN",               "48 8B 41", 0x30, 0x40, 0, 0x38, false, nullptr, 0x140, 0, true },
    { "menu.classTag",     "UNKNOWN_COMMAND_175",        "48 8B 45", 0x30, 0x40, 0, 0x38, false, nullptr, 0x140, -1, true },
    { "menu.tagKind",      "UNKNOWN_COMMAND_180",        "83 78",    0x40, 0x48, 0, 0x44, false, "minimenu class tag: row kind (1 interface item, 2 ground item, 3 loc, 4 NPC, 7 player)", 0x140, -1, true },
    { "menu.tagKind",      "MINIMENUOPEN",               "83 78",    0x40, 0x48, 0, 0x44, false, nullptr, 0x140, 0, true },
    { "menu.tagKind",      "UNKNOWN_COMMAND_175",        "8B 48",    0x40, 0x48, 0, 0x44, false, nullptr, 0x140, -1, true },
    { "menu.rowX",         "UNKNOWN_COMMAND_180",        "44 8B 4A", 0x48, 0x50, 0, 0x4C, false, "minimenu display object: tile x of a loc, walk or ground row", 0x140, -1, true },
    { "menu.rowX",         "MINIMENUOPEN",               "39 5A",    0x48, 0x50, 0, 0x4C, false, nullptr, 0x400, 0, true },
    { "menu.rowZ",         "UNKNOWN_COMMAND_180",        "44 8B 42", 0x50, 0x58, 0, 0x50, false, "minimenu display object: tile z of a loc, walk or ground row", 0x140, -1, true },
    { "menu.rowZ",         "MINIMENUOPEN",               "8B 59",    0x50, 0x58, 0, 0x50, false, nullptr, 0x140, 0, true },
    // stats, friends, the Grand Exchange box, containers, clan, group and chat records. These ranges
    // hold the compiled value alone: a move reads GONE, not the new value.
    { "stats.array",       "STAT_BASE",                  "48 8B 42", 0x10, 0x11, 0, 0x10, false, "stats object (at kStatsInner): skill entry array", 0x140, -1, true },
    { "stats.array",       "STAT",                       "49 8B 40", 0x10, 0x11, 0, 0x10, false, nullptr, 0x140, -1, true },
    { "stats.xp",          "STAT_VISIBLE_XP_ACTUAL",     "8B 54 C8", 0xC, 0xD, 0, 0xC, false, "skill entry (0x18 bytes): experience", 0x140, -1, true },
    { "stats.xp",          "STAT_VISIBLE_XP",            "41 8B 51", 0xC, 0xD, 0, 0xC, false, nullptr, 0x140, -1, true },
    { "stats.level",       "STAT_BASE_ACTUAL",           "8B 54 C8", 0x10, 0x11, 0, 0x10, false, "skill entry: real level", 0x140, -1, true },
    { "stats.level",       "STAT_BASE",                  "8B 4A",    0x10, 0x11, 0, 0x10, false, nullptr, 0x140, -1, true },
    { "stats.boosted",     "STAT",                       "8B 54 C8", 0x14, 0x15, 0, 0x14, false, "skill entry: boosted level", 0x140, -1, true },
    { "stats.runEnergy",   "RUNENERGY_VISIBLE",          "8B 51",    0x18, 0x19, 0, 0x18, false, "stats object: run energy", 0x140, -1, true },
    { "stats.runWeight",   "RUNWEIGHT_VISIBLE",          "8B 51",    0x1C, 0x1D, 0, 0x1C, false, "stats object: run weight", 0x140, -1, true },
    { "friends.state",     "FRIEND_GETWORLD",            "83 79",    0x10, 0x11, 0, 0x10, false, "friends list: state, 2 once loaded", 0x140, -1, true },
    { "friends.state",     "FRIEND_COUNT",               "41 8B 41", 0x10, 0x11, 0, 0x10, false, nullptr, 0x140, -1, true },
    { "friends.listBegin", "FRIEND_GETWORLD",            "4C 8B 59", 0x18, 0x19, 0, 0x18, false, "friends list: entry vector begin", 0x140, -1, true },
    { "friends.listBegin", "FRIEND_GETNAME",             "4C 8B 49", 0x18, 0x19, 0, 0x18, false, nullptr, 0x140, -1, true },
    { "friends.listEnd",   "FRIEND_GETWORLD",            "48 8B 49", 0x20, 0x21, 0, 0x20, false, "friends list: entry vector end", 0x140, -1, true },
    { "friends.listEnd",   "FRIEND_COUNT",               "49 8B 49", 0x20, 0x21, 0, 0x20, false, nullptr, 0x140, -1, true },
    { "friends.stride",    "FRIEND_GETWORLD",            "48 6B C8", 0x78, 0x79, 0, 0x78, false, "friend entry size", 0x140, -1, true },
    { "friends.stride",    "FRIEND_GETNAME",             "49 6B DA", 0x78, 0x79, 0, 0x78, false, nullptr, 0x140, -1, true },
    { "friends.world",     "FRIEND_GETWORLD",            "42 8B 44 19", 0x30, 0x31, 0, 0x30, false, "friend entry: world", 0x140, -1, true },
    { "friends.rank",      "FRIEND_GETRANK",             "42 8B 44 19", 0x50, 0x51, 0, 0x50, false, "friend entry: rank", 0x140, -1, true },
    { "ge.status",         "STOCKMARKET_ISOFFEREMPTY",   "39 54 C8", 0x10, 0x11, 0, 0x10, false, "GE box: first slot's status, so the slot array starts here (kGEArrayPad); slot fields = value - 0x10", 0x140, -1, true },
    { "ge.status",         "STOCKMARKET_ISOFFERFINISHED", "83 7C C8", 0x10, 0x11, 0, 0x10, false, nullptr, 0x140, -1, true },
    { "ge.type",           "STOCKMARKET_GETOFFERTYPE",   "8B 54 C8", 0x14, 0x15, 0, 0x14, false, "GE box: first slot's type (slot +4)", 0x140, -1, true },
    { "ge.item",           "STOCKMARKET_GETOFFERITEM",   "8B 54 C8", 0x18, 0x19, 0, 0x18, false, "GE box: first slot's item (slot +8)", 0x140, -1, true },
    { "ge.price",          "unk11086",                   "4A 8B 54 C0", 0x20, 0x21, 0, 0x20, false, "GE box: first slot's price (slot +0x10)", 0x140, -1, true },
    { "ge.filled",         "STOCKMARKET_GETOFFERCOMPLETEDCOUNT", "8B 54 C8", 0x2C, 0x2D, 0, 0x2C, false, "GE box: first slot's filled count (slot +0x1C)", 0x140, -1, true },
    { "ge.value",          "STOCKMARKET_GETOFFERCOMPLETEDGOLD", "4A 8B 54 C0", 0x30, 0x31, 0, 0x30, false, "GE box: first slot's filled value (slot +0x20)", 0x140, -1, true },
    { "containers.begin",  "INV_FREESPACE",              "4C 8B 41", 0x8, 0x9, 0, 0x8, false, "container manager: entry vector begin", 0x140, 0, true },
    { "containers.end",    "INV_FREESPACE",              "4C 8B 59", 0x10, 0x11, 0, 0x10, false, "container manager: entry vector end", 0x140, 0, true },
    { "containers.stride", "INV_FREESPACE",              "4D 8D 41", 0x48, 0x49, 0, 0x48, false, "container entry size (kContainerStride)", 0x140, 0, true },
    { "containers.stride", "INV_FREESPACE",              "49 8D 40", 0x48, 0x49, 0, 0x48, false, nullptr, 0x140, 0, true },
    { "containers.handle", "INV_FREESPACE",              "49 8D 48", 0x8, 0x9, 0, 0x8, false, "container entry: the handle the find returns (the reader's entry + 0x18 / 0x20 = handle + 0x10 / 0x18)", 0x140, 0, true },
    { "containers.itemsBegin", "INV_GETOBJ",             "48 8B 50", 0x10, 0x11, 0, 0x10, false, "container handle: item vector begin, 8 bytes per item", 0x140, 0, true },
    { "containers.itemsBegin", "INV_GETNUM",             "48 8B 50", 0x10, 0x11, 0, 0x10, false, nullptr, 0x140, 0, true },
    { "containers.itemsEnd", "INV_GETOBJ",               "48 8B 40", 0x18, 0x19, 0, 0x18, false, "container handle: item vector end", 0x140, 0, true },
    { "containers.itemsEnd", "INV_GETNUM",               "48 8B 40", 0x18, 0x19, 0, 0x18, false, nullptr, 0x140, 0, true },
    { "containers.itemStride", "INV_TOTAL",              "48 83 C0", 0x8, 0x9, 0, 0x8, false, "container item size: i32 id, i32 count", 0x140, 0, true },
    { "containers.itemStride", "INVOTHER_TOTAL",         "48 83 C0", 0x8, 0x9, 0, 0x8, false, nullptr, 0x140, 0, true },
    { "clan.listenedRef",  "ACTIVECLANSETTINGS_FIND_LISTENED", "48 83 C2", 0x60, 0x61, 0, 0x60, false, "clan manager: counted reference to the listened settings store", 0x140, -1, true },
    { "clan.listenedObj",  "ACTIVECLANSETTINGS_FIND_LISTENED", "48 83 7A", 0x8, 0x9, 0, 0x8, false, "clan listened reference: the settings store object", 0x140, -1, true },
    { "group.object",      "PLAYER_GROUP_MEMBER_COUNT",  "48 8B 50", 0x8, 0x9, 0, 0x8, false, "player group manager: the group", 0x140, -1, true },
    { "group.object",      "PLAYER_GROUP_FIND",          "48 39 50", 0x8, 0x9, 0, 0x8, false, nullptr, 0x140, -1, true },
    { "group.membersBegin", "PLAYER_GROUP_MEMBER_COUNT", "48 2B 4A", 0x60, 0x61, 0, 0x60, false, "player group: member vector begin", 0x140, -1, true },
    { "group.membersBegin", "PLAYER_GROUP_MEMBER_GET_RANK", "49 8B 40", 0x60, 0x61, 0, 0x60, false, nullptr, 0x140, -1, true },
    { "group.membersEnd",  "PLAYER_GROUP_MEMBER_COUNT",  "48 8B 4A", 0x68, 0x69, 0, 0x68, false, "player group: member vector end", 0x140, -1, true },
    { "group.memberStride", "PLAYER_GROUP_MEMBER_GET_RANK", "48 69 C8", 0xB0, 0xB1, 0, 0xB0, false, "player group member size" },
    { "group.memberStride", "PLAYER_GROUP_MEMBER_IS_ONLINE", "48 69 D0", 0xB0, 0xB1, 0, 0xB0 },
    { "group.maxSize",     "PLAYER_GROUP_GET_MAX_SIZE",  "0F BF 51", 0x22, 0x23, 0, 0x22, false, "player group: i16 max size", 0x140, -1, true },
    { "chat.recordId",     "printmessage",               "8B 48",    0x10, 0x11, 0, 0x10, false, "chat record: record id", 0x300, 4, true },
    { "chat.recordId",     "printmessage",               "89 42",    0x10, 0x11, 0, 0x10, false, nullptr, 0x300, 4, true },
    { "chat.recordId",     "printmessage",               "89 41",    0x10, 0x11, 0, 0x10, false, nullptr, 0x300, 4, true },   // the plugin client fills the record through rcx
    { "chat.recordTick",   "printmessage",               "89 42",    0x14, 0x15, 0, 0x14, false, "chat record: client tick at insert", 0x300, 4, true },
    { "chat.recordTick",   "printmessage",               "89 41",    0x14, 0x15, 0, 0x14, false, nullptr, 0x300, 4, true },
    { "chat.recordType",   "printmessage",               "89 42",    0x20, 0x21, 0, 0x20, false, "chat record: message type", 0x300, 4, true },
    { "chat.recordType",   "printmessage",               "89 41",    0x20, 0x21, 0, 0x20, false, nullptr, 0x300, 4, true },
    { "chat.recordType",   "unk11108",                   "8B 53",    0x20, 0x21, 0, 0x20, false, nullptr, 0x300, -1, true },
    { "chat.nextId",       "CHAT_LASTUID",               "8B 50",    0x18, 0x19, 0, 0x18, false, "chat store: next record id", 0x140, -1, true },
    { "chat.nextId",       "printmessage",               "4C 63 79", 0x18, 0x19, 0, 0x18, false, nullptr, 0x300, 4, true },
};

// Handlers that name themselves in their error text: the string, and the table name whose number
// must be one of the handlers taking its address. One that disagrees means the table numbers
// another build's handlers.
struct Spot { const char* text; const char* op; };
const Spot kSpots[] = {
    { "db_getfield",                    "dbrow_getfield" },
    { "array_sort",                     "ARRAY_SORT" },
    { "_minimenuopen",                  "MINIMENUOPEN" },
    { "_deeplink_get",                  "DEEPLINK_GET" },
    { "_shop_applypendingtransactions", "SHOP_APPLYPENDINGTRANSACTIONS" },
};

struct Cache {
    std::wstring key; std::vector<Found> found; std::string report, why;
    TableState table; std::vector<SpotCheck> spots;
    std::map<std::string, std::uint32_t> ops;   // every name in use: by the handlers' code, and the export's where one is used
    struct Differ { std::string name; std::uint32_t code, exported; std::string exportOnCode; };
    std::vector<Differ> differs;                 // names the code and the export give to different handlers
};
std::mutex g_mu;
Cache g_cache;

// The build in the exe's version resource ("950.1.0.0"), the form the launcher writes beside the
// operation table when it extracts it.
std::string ExeBuildImpl(const std::wstring& path) {
    DWORD ignored = 0;
    const DWORD sz = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (!sz) return {};
    std::vector<std::uint8_t> buf(sz);
    if (!GetFileVersionInfoW(path.c_str(), 0, sz, buf.data())) return {};
    VS_FIXEDFILEINFO* ffi = nullptr; UINT n = 0;
    if (!VerQueryValueW(buf.data(), L"\\", reinterpret_cast<LPVOID*>(&ffi), &n) || !ffi || n < sizeof(*ffi)) return {};
    char v[48];
    std::snprintf(v, sizeof(v), "%u.%u.%u.%u",
                  (unsigned)HIWORD(ffi->dwFileVersionMS), (unsigned)LOWORD(ffi->dwFileVersionMS),
                  (unsigned)HIWORD(ffi->dwFileVersionLS), (unsigned)LOWORD(ffi->dwFileVersionLS));
    return v;
}

// Operation numbers are reshuffled between game builds, so a table from another build names the
// wrong handlers and every rule would read some other field. Only a table extracted from this build
// is used. The launcher writes the build label ("950.1.0.0", then the exe's time stamp) when an
// extraction that rewrote the table ends: the build when the run succeeded and the game did not
// update during it, else an empty label. It gives a build label the table's write time. A label
// newer than its table was not written for that table (older launchers wrote it when an extraction
// started), so the table also has to be at least as new as its label. The time stamp is not
// required to match: the OpenGL and Vulkan clients of one build share their numbers, and the
// self-naming handlers settle whether this exe's numbers are the table's. Empty when the table can
// be used, else why not.
std::string TableBuildMismatch(const std::wstring& exePath, const std::wstring& opcodesJson, const std::wstring& buildTxt,
                               TableState& ts) {
    std::vector<std::uint8_t> b;
    std::string table;
    if (ReadFile(buildTxt, b)) table.assign(b.begin(), b.end());
    while (!table.empty() && (table.back() == '\r' || table.back() == '\n' || table.back() == ' ')) table.pop_back();
    ts.label = table;
    const std::size_t sp = table.find(' ');
    const std::string version = sp == std::string::npos ? table : table.substr(0, sp);
    const std::string running = ExeBuildImpl(exePath);
    ts.exeVersion = running;
    char line[200];
    if (running.empty() || version != running) {
        std::snprintf(line, sizeof(line), "operation table is for build %s, the client is %s",
                      version.empty() ? "(unknown)" : version.substr(0, 40).c_str(), running.empty() ? "(unknown)" : running.c_str());
        return line;
    }
    WIN32_FILE_ATTRIBUTE_DATA t{}, v{};
    if (!GetFileAttributesExW(opcodesJson.c_str(), GetFileExInfoStandard, &t) ||
        !GetFileAttributesExW(buildTxt.c_str(), GetFileExInfoStandard, &v) ||
        CompareFileTime(&t.ftLastWriteTime, &v.ftLastWriteTime) < 0) {
        std::snprintf(line, sizeof(line), "operation table for build %s was not rewritten by the last extraction", running.c_str());
        return line;
    }
    return {};
}

int ParseHex(const char* pat, std::uint8_t* out, int cap) {
    int n = 0;
    for (const char* p = pat; *p && n < cap; ) {
        while (*p == ' ') ++p;
        if (!*p) break;
        char two[3] = { p[0], p[1], 0 };
        out[n++] = (std::uint8_t)std::strtoul(two, nullptr, 16); p += 2;
    }
    return n;
}

// The instruction of shape `head` + disp32 in the first `span` bytes of the handler, never past
// `endRva` (the next handler), with the displacement in range; `nth` skips earlier matches. 0 when
// absent, or when a later match reads a different displacement (`ambiguous`): a guess would read
// the wrong field with no sign of it, where a rule that finds nothing is reported and keeps the
// compiled constant.
std::uint32_t FindDisp(const Pe& pe, std::uint32_t handlerRva, std::uint32_t endRva, const Rule& r, bool& ambiguous) {
    ambiguous = false;
    std::uint8_t head[8]; const int hn = ParseHex(r.head, head, 8);
    const bool headRex = (head[0] & 0xF0) == 0x40;
    const std::uint8_t* p = pe.base + pe.textRaw + (handlerRva - pe.textRva);
    std::size_t len = pe.textSize - (handlerRva - pe.textRva);
    if (len > r.span) len = r.span;
    if (endRva > handlerRva && endRva - handlerRva < len) len = endRva - handlerRva;
    int seen = 0; bool have = false; std::uint32_t got = 0;
    const std::size_t dn = r.d8 ? 1 : 4;
    for (std::size_t i = 0; i + hn + dn <= len; ++i) {
        if (std::memcmp(p + i, head, hn) != 0) continue;
        // a head without its own REX byte also matches one byte into the REX form, which is
        // another instruction (other register or width)
        if (!headRex && i > 0 && (p[i - 1] & 0xF0) == 0x40) continue;
        std::uint32_t disp = 0;
        if (r.d8) disp = p[i + hn]; else std::memcpy(&disp, p + i + hn, 4);
        if (disp < r.lo || disp >= r.hi) continue;
        if (seen++ < r.nth) continue;
        if (!have) { have = true; got = disp; continue; }
        if (disp != got) { ambiguous = true; return 0; }   // the same field read again is fine
    }
    return have ? got : 0;
}

// Every hit of a pattern (-1 = any byte) in the executable sections, at most `cap`.
std::vector<std::uint32_t> ScanCode(const Pe& pe, const int* pat, std::size_t n, std::size_t cap) {
    std::vector<std::uint32_t> hits;
    for (const auto& s : pe.sections) {
        if (!(s.flags & 0x20000000u)) continue;
        const std::uint8_t* b = pe.base + s.raw;
        const std::size_t len = std::min<std::size_t>(s.rawSize, s.vsize ? s.vsize : s.rawSize);
        for (std::size_t i = 0; i + n <= len; ++i) {
            if (b[i] != (std::uint8_t)pat[0]) continue;
            std::size_t k = 1;
            while (k < n && (pat[k] < 0 || b[i + k] == (std::uint8_t)pat[k])) ++k;
            if (k < n) continue;
            hits.push_back(s.rva + (std::uint32_t)i);
            if (hits.size() >= cap) return hits;
        }
    }
    return hits;
}

// The root global: the MainData constructor (32 bytes before the anchor) publishes it with its
// first mov [rip+disp],rax.
std::uint32_t RootGlobal(const Pe& pe) {
    int anchor[sizeof(rtx::sig::kMainAnchor)];
    for (std::size_t i = 0; i < sizeof(anchor) / sizeof(int); ++i) anchor[i] = rtx::sig::kMainAnchor[i];
    const auto hits = ScanCode(pe, anchor, sizeof(anchor) / sizeof(int), 2);
    if (hits.size() != 1 || hits[0] < 32) return 0;
    const std::uint32_t f0 = hits[0] - 32;
    const std::uint8_t* p = At(pe, f0, 0x200 + 7);
    if (!p) return 0;
    for (std::uint32_t i = 0; i < 0x200; ++i) {
        if (p[i] != 0x48 || p[i + 1] != 0x89 || p[i + 2] != 0x05) continue;
        std::int32_t rel; std::memcpy(&rel, p + i + 3, 4);
        return (std::uint32_t)((std::int64_t)f0 + i + 7 + rel);
    }
    return 0;
}

// A fourth clock read: mov rax,[clock] then add rax,20000 (a connection's ping deadline, 20 s
// ahead), at two sites: the pair sits together in one routine (exe+0xE9CD1 on 950-1) and an epilogue
// reload splits it in the connection constructor (exe+0xE9AFB), so the add is the anchor and the
// nearest mov rax,[rip+disp] before it is the read.
constexpr int kClockAdd[] = { 0x48, 0x05, 0x20, 0x4E, 0x00, 0x00 };
constexpr std::uint32_t kClockAddBack = 24;   // bytes before the add the mov is looked for in

// Where the engine clock sits after the root global, from the routines that read it; 0 when none
// is found or two disagree (`why` says which).
std::uint32_t EngineClock(const Pe& pe, std::string& why) {
    const std::uint32_t g = RootGlobal(pe);
    if (!g) { why = "root global not found"; return 0; }
    std::uint32_t off = 0;
    auto take = [&](const int* pat, std::size_t len) {
        const auto hits = ScanCode(pe, pat, len, 2);
        if (hits.size() != 1) return true;
        const std::uint8_t* p = At(pe, hits[0] + rtx::sig::kClockDispAt, 4);
        if (!p) return true;
        std::int32_t rel; std::memcpy(&rel, p, 4);
        const std::int64_t d = (std::int64_t)hits[0] + rtx::sig::kClockDispAt + 4 + rel - g;
        if (d <= 0 || d >= rtx::sig::kClockMax) return true;
        if (off && off != (std::uint32_t)d) { why = "the clock reads name two places"; return false; }
        off = (std::uint32_t)d;
        return true;
    };
    for (const auto& r : rtx::sig::kClockReads) if (!take(r.pat, r.len)) return 0;
    for (std::uint32_t at : ScanCode(pe, kClockAdd, sizeof(kClockAdd) / sizeof(int), 8)) {
        if (at < kClockAddBack) continue;
        const std::uint8_t* p = At(pe, at - kClockAddBack, kClockAddBack);
        if (!p) continue;
        for (int i = (int)kClockAddBack - 7; i >= 0; --i) {
            if (p[i] != 0x48 || p[i + 1] != 0x8B || p[i + 2] != 0x05) continue;
            std::int32_t rel; std::memcpy(&rel, p + i + 3, 4);
            const std::int64_t d = (std::int64_t)(at - kClockAddBack) + i + 7 + rel - g;
            if (d > 0 && d < rtx::sig::kClockMax) {
                if (off && off != (std::uint32_t)d) { why = "the clock reads name two places"; return 0; }
                off = (std::uint32_t)d;
            }
            break;
        }
    }
    if (!off) why = "no clock read of a known shape";
    return off;
}

// A component setter handler that is a wrapper: the callback it hands its dispatcher, else 0.
std::uint32_t WrapperCallback(const Pe& pe, std::uint32_t h) {
    using namespace rtx::sig;
    const std::uint8_t* p = At(pe, h, 0x40);
    if (!p) return 0;
    if (p[0] == 0x40) { ++h; ++p; }
    if (std::memcmp(p, kCcWrapHead, sizeof(kCcWrapHead)) || std::memcmp(p + kCcWrapMidAt, kCcWrapMid, sizeof(kCcWrapMid)) ||
        std::memcmp(p + kCcWrapTailAt, kCcWrapTail, sizeof(kCcWrapTail))) return 0;
    std::int32_t rel; std::memcpy(&rel, p + 25, 4);
    const std::int64_t cb = (std::int64_t)h + 29 + rel;
    if (cb < pe.textRva || cb >= (std::int64_t)pe.textRva + pe.textSize) return 0;
    return (std::uint32_t)cb;
}

// The functions the exe's unwind data names, start -> end. A call whose target starts one of them
// is a call; a byte that merely looks like one inside another instruction is passed over. The end
// bounds a rule's scan of the routine: what sits past it is another function, found only by where
// the linker put it. The compiler splits some functions into parts whose unwind data chains to a
// part before (UNW_FLAG_CHAININFO with the parent's entry after the unwind codes, or an entry that
// points at its parent's entry); a part that follows the function directly and chains to it is
// the same function, so its end is the function's end.
std::uint32_t ChainParent(const Pe& pe, std::uint32_t info) {
    if (info & 1) { const std::uint8_t* e = At(pe, info & ~1u, 4); std::uint32_t b = 0; if (e) std::memcpy(&b, e, 4); return b; }
    const std::uint8_t* u = At(pe, info, 4);
    if (!u || !((u[0] >> 3) & 4)) return 0;
    const std::uint32_t at = info + 4 + 2u * ((u[2] + 1u) & ~1u);   // past the unwind codes, kept to an even count
    const std::uint8_t* e = At(pe, at, 4);
    std::uint32_t b = 0; if (e) std::memcpy(&b, e, 4);
    return b;
}
std::map<std::uint32_t, std::uint32_t> FunctionRanges(const Pe& pe) {
    std::map<std::uint32_t, std::uint32_t> out;
    const std::uint8_t* p = pe.pdataSize ? At(pe, pe.pdataRva, pe.pdataSize) : nullptr;
    if (!p) return out;
    std::uint32_t lastBegin = 0, lastEnd = 0;
    std::vector<std::uint32_t> parts;   // where each part of the function being extended begins
    for (std::uint32_t i = 0; i + 12 <= pe.pdataSize; i += 12) {
        std::uint32_t begin, stop, info;
        std::memcpy(&begin, p + i, 4); std::memcpy(&stop, p + i + 4, 4); std::memcpy(&info, p + i + 8, 4);
        if (begin < pe.textRva || begin >= pe.textRva + pe.textSize || stop <= begin) continue;
        const std::uint32_t parent = ChainParent(pe, info);
        if (parent && lastEnd && begin == lastEnd && std::find(parts.begin(), parts.end(), parent) != parts.end()) {
            out[lastBegin] = stop; lastEnd = stop; parts.push_back(begin);
            continue;
        }
        out.emplace(begin, stop);
        lastBegin = begin; lastEnd = stop;
        parts.assign(1, begin);
    }
    return out;
}

// Where a routine ends, at most `limit` bytes from its start: its unwind data's end when it has
// one, else the first ret the linker's int3 padding follows. A small callback that saves no
// register has no unwind data, and the callback linked after it is not its code.
std::uint32_t RoutineEnd(const Pe& pe, std::uint32_t at, std::uint32_t limit, const std::map<std::uint32_t, std::uint32_t>& functions) {
    const std::uint32_t end = at + limit;
    if (const auto fe = functions.find(at); fe != functions.end()) return fe->second < end ? fe->second : end;
    const std::uint8_t* p = At(pe, at, limit);
    if (!p) return end;
    for (std::uint32_t i = 0; i + 1 < limit; ++i)
        if (p[i] == 0xC3 && p[i + 1] == 0xCC) return at + i + 1;
    return end;
}

// The routine the handler's `nth` direct call names (0-based, in address order, within the first
// 0x400 bytes of the handler and never past `end`), 0 when there is none.
std::uint32_t CallTarget(const Pe& pe, std::uint32_t at, std::uint32_t end, int nth, const std::map<std::uint32_t, std::uint32_t>& functions) {
    std::size_t len = 0x400;
    if (end > at && end - at < len) len = end - at;
    const std::uint8_t* p = At(pe, at, len);
    if (!p) return 0;
    int seen = 0;
    for (std::size_t i = 0; i + 5 <= len; ++i) {
        if (p[i] != 0xE8) continue;
        std::int32_t rel; std::memcpy(&rel, p + i + 1, 4);
        const std::int64_t t = (std::int64_t)at + (std::int64_t)i + 5 + rel;
        if (t < pe.textRva || t >= (std::int64_t)pe.textRva + pe.textSize || !functions.count((std::uint32_t)t)) continue;
        if (seen++ == nth) return (std::uint32_t)t;
    }
    return 0;
}

}  // namespace

bool ReadFile(const std::wstring& path, std::vector<std::uint8_t>& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > (256ll << 20)) { CloseHandle(h); return false; }
    out.resize((size_t)sz.QuadPart);
    DWORD got = 0; std::size_t at = 0;
    while (at < out.size()) {
        if (!::ReadFile(h, out.data() + at, (DWORD)(out.size() - at), &got, nullptr) || !got) { CloseHandle(h); return false; }
        at += got;
    }
    CloseHandle(h);
    return true;
}

std::wstring FileKey(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a)) return L"";
    wchar_t buf[64];
    _snwprintf_s(buf, _TRUNCATE, L"|%u|%u|%u", a.nFileSizeLow, a.ftLastWriteTime.dwHighDateTime, a.ftLastWriteTime.dwLowDateTime);
    return path + buf;
}

bool ParsePe(const std::vector<std::uint8_t>& f, Pe& pe) {
    if (f.size() < 0x200 || f[0] != 'M' || f[1] != 'Z') return false;
    std::uint32_t e_lfanew; std::memcpy(&e_lfanew, f.data() + 0x3C, 4);
    if ((std::uint64_t)e_lfanew + 0x108 > f.size() || std::memcmp(f.data() + e_lfanew, "PE\0\0", 4) != 0) return false;
    const std::uint8_t* nt = f.data() + e_lfanew;
    std::uint16_t nsec; std::memcpy(&nsec, nt + 6, 2);
    std::memcpy(&pe.stamp, nt + 8, 4);
    std::uint16_t optSize; std::memcpy(&optSize, nt + 20, 2);
    const std::uint8_t* opt = nt + 24;
    std::uint16_t magic; std::memcpy(&magic, opt, 2);
    if (magic != 0x20B) return false;
    std::memcpy(&pe.imageBase, opt + 24, 8);
    std::memcpy(&pe.sizeOfImage, opt + 56, 4);
    std::memcpy(&pe.importRva, opt + 112 + 8, 4); std::memcpy(&pe.importSize, opt + 112 + 12, 4);
    std::memcpy(&pe.pdataRva, opt + 112 + 24, 4); std::memcpy(&pe.pdataSize, opt + 112 + 28, 4);
    const std::uint8_t* sec = nt + 24 + optSize;
    bool text = false;
    pe.sections.clear();
    for (unsigned i = 0; i < nsec; ++i, sec += 40) {
        if ((const std::uint8_t*)(sec + 40) > f.data() + f.size()) return false;
        Section s;
        char nm[9] = {}; std::memcpy(nm, sec, 8); s.name = nm;
        std::memcpy(&s.vsize, sec + 8, 4); std::memcpy(&s.rva, sec + 12, 4);
        std::memcpy(&s.rawSize, sec + 16, 4); std::memcpy(&s.raw, sec + 20, 4);
        std::memcpy(&s.flags, sec + 36, 4);
        if ((std::uint64_t)s.raw + s.rawSize > f.size()) s.rawSize = s.raw < f.size() ? (std::uint32_t)(f.size() - s.raw) : 0;
        pe.sections.push_back(s);
        if (!text && s.name == ".text") {
            pe.textRva = s.rva; pe.textRaw = s.raw;
            pe.textSize = s.vsize > s.rawSize ? s.rawSize : s.vsize;
            text = true;
        }
    }
    pe.base = f.data(); pe.size = f.size();
    return text;
}

const Section* SectionOf(const Pe& pe, std::uint32_t rva) {
    for (const auto& s : pe.sections)
        if (rva >= s.rva && rva < s.rva + (s.vsize > s.rawSize ? s.vsize : s.rawSize)) return &s;
    return nullptr;
}

const std::uint8_t* At(const Pe& pe, std::uint32_t rva, std::size_t n) {
    const Section* s = SectionOf(pe, rva);
    if (!s) return nullptr;
    const std::uint32_t off = rva - s->rva;
    if ((std::uint64_t)off + n > s->rawSize) return nullptr;
    return pe.base + s->raw + off;
}

// Every registered handler by scrambled number: mov r8w, imm16 ; lea rdx, [rip+rel32]. The
// registrar is the one routine holding thousands of these; anything else matching the two
// instructions by chance is drowned by the requirement that the numbers form one dense run.
bool Handlers(const Pe& pe, std::map<std::uint32_t, std::uint32_t>& outByOp /* op -> rva */) {
    const std::uint8_t* t = pe.base + pe.textRaw;
    for (std::size_t i = 0; i + 12 <= pe.textSize; ++i) {
        if (t[i] != 0x66 || t[i + 1] != 0x41 || t[i + 2] != 0xB8 || t[i + 5] != 0x48 || t[i + 6] != 0x8D || t[i + 7] != 0x15) continue;
        std::uint16_t op; std::memcpy(&op, t + i + 3, 2);
        std::int32_t rel; std::memcpy(&rel, t + i + 8, 4);
        const std::int64_t rva = (std::int64_t)pe.textRva + (std::int64_t)i + 12 + rel;
        if (rva < pe.textRva || rva >= (std::int64_t)pe.textRva + pe.textSize) continue;
        auto it = outByOp.find(op);
        if (it != outByOp.end() && it->second != (std::uint32_t)rva) return false;   // the same number twice with different handlers: not the registrar
        outByOp[op] = (std::uint32_t)rva;
    }
    if (outByOp.size() < 1500) return false;
    // dense: the numbers run 1..N with few gaps
    std::uint32_t maxOp = outByOp.rbegin()->first;
    return outByOp.size() * 10 >= (std::size_t)maxOp * 9;
}

// The registrar passes each operation's operand width beside its number, in r9b just before:
// mov r9b,1 for a four-byte operand, mov r9b,0 / xor r9d,r9d for one byte.
std::vector<std::uint8_t> ScriptOperandWidths(const Pe& pe) {
    std::vector<std::uint8_t> out;
    const std::uint8_t* t = pe.base + pe.textRaw;
    std::size_t found = 0, open = 0;
    for (std::size_t i = 24; i + 12 <= pe.textSize; ++i) {
        if (t[i] != 0x66 || t[i + 1] != 0x41 || t[i + 2] != 0xB8 || t[i + 5] != 0x48 || t[i + 6] != 0x8D || t[i + 7] != 0x15) continue;
        std::uint16_t op; std::memcpy(&op, t + i + 3, 2);
        int w = 0;
        for (std::size_t j = i - 1; j + 24 > i && !w; --j) {
            if (t[j] == 0x41 && t[j + 1] == 0xB1) w = t[j + 2] ? 4 : 1;
            else if (t[j] == 0x45 && (t[j + 1] == 0x33 || t[j + 1] == 0x32) && t[j + 2] == 0xC9) w = 1;
        }
        if (!w) { ++open; continue; }
        if (op >= out.size()) out.resize((std::size_t)op + 1, 0);
        if (out[op] && out[op] != w) return {};   // one number, two widths: not the registrar
        out[op] = (std::uint8_t)w;
        ++found;
    }
    if (found < 1500 || open * 100 > found) return {};
    return out;
}

std::uint32_t OverheadFromCode(const Pe& pe) {
    const std::uint8_t* t = pe.base + pe.textRaw;
    const std::size_t n = pe.textSize;
    constexpr std::size_t kLookBack = 0x80;
    std::uint32_t found = 0;
    for (std::size_t i = kLookBack; i + 7 <= n; ++i) {
        // imul r64, r64, 0x1B0
        if ((t[i] & 0xFA) != 0x48 || t[i + 1] != 0x69 || (t[i + 2] & 0xC0) != 0xC0) continue;
        std::uint32_t imm; std::memcpy(&imm, t + i + 3, 4);
        if (imm != 0x1B0) continue;
        // mov r64,[r64+disp32] in the stretch before it
        for (std::size_t j = i - kLookBack; j < i; ++j) {
            if ((t[j] & 0xFA) != 0x48 || t[j + 1] != 0x8B || (t[j + 2] & 0xC0) != 0x80 || (t[j + 2] & 7) == 4) continue;
            std::uint32_t disp; std::memcpy(&disp, t + j + 3, 4);
            if (disp < 0x400 || disp > 0x3000) continue;
            if (found && found != disp) return 0;
            found = disp;
        }
    }
    return found;
}

ArrowSite ArrowSiteFromCode(const Pe& pe) {
    ArrowSite a;
    const int* pat = rtx::sig::kArrowFrame;
    const std::size_t len = sizeof(rtx::sig::kArrowFrame) / sizeof(int);
    const std::uint8_t* t = pe.base + pe.textRaw;
    const std::uint8_t* hit = nullptr;
    for (std::size_t i = 0; i + len + 0x20 <= pe.textSize; ++i) {
        std::size_t k = 0;
        for (; k < len; ++k) if (pat[k] >= 0 && t[i + k] != (std::uint8_t)pat[k]) break;
        if (k != len) continue;
        if (hit) return a;   // twice: not the site
        hit = t + i;
    }
    if (!hit) return a;
    std::memcpy(&a.arrow, hit + rtx::sig::kArrowFrameArrow, 4);
    std::memcpy(&a.trail, hit + rtx::sig::kArrowFrameTrail, 4);
    a.slot = rtx::sig::ArrowSlotSize(hit);
    a.ok = a.arrow >= 0x18000 && a.arrow < 0x60000 && a.trail >= 0x18000 && a.trail < 0x60000;
    // the message routine: its signature sits a little into it, the records are filled further on
    const int* mp = rtx::sig::kArrowMessage;
    const std::size_t ml = sizeof(rtx::sig::kArrowMessage) / sizeof(int);
    const std::uint8_t* msg = nullptr;
    for (std::size_t i = 0x40; i + ml + 0x600 <= pe.textSize; ++i) {
        std::size_t k = 0;
        for (; k < ml; ++k) if (mp[k] >= 0 && t[i + k] != (std::uint8_t)mp[k]) break;
        if (k != ml) continue;
        if (msg) { msg = nullptr; break; }   // twice: not the routine
        msg = t + i;
    }
    if (msg && !rtx::sig::ArrowRequests(msg - 0x40, 0x640, a.requests, a.requestSize)) a.requests = a.requestSize = 0;
    return a;
}

// The exporter's placeholders (unk11190, UNKNOWN_COMMAND_500, CHATCAT_UNK11187): numbered in sequence
// per export, so one operation more or less renumbers every one after it, and the same label names
// another handler in the next export.
static bool Placeholder(const std::string& n) {
    const auto digits_from = [&](std::size_t at) {
        if (at >= n.size()) return false;
        for (std::size_t i = at; i < n.size(); ++i) if (n[i] < '0' || n[i] > '9') return false;
        return true;
    };
    if (n.rfind("unk", 0) == 0 && digits_from(3)) return true;
    if (n.rfind("UNKNOWN_COMMAND_", 0) == 0 && digits_from(16)) return true;
    const std::size_t u = n.rfind("UNK");
    return u != std::string::npos && digits_from(u + 3);
}

// name -> scrambled number, from the launcher's export: [{"op":N,"id":..,"name":"X"},...]
bool OpTable(const std::wstring& path, std::map<std::string, std::uint32_t>& out) {
    std::vector<std::uint8_t> f;
    if (!ReadFile(path, f)) return false;
    std::string s(f.begin(), f.end());
    std::size_t at = 0;
    while ((at = s.find("\"op\":", at)) != std::string::npos) {
        at += 5;
        const std::uint32_t op = (std::uint32_t)std::strtoul(s.c_str() + at, nullptr, 10);
        const std::size_t end = s.find('}', at);
        if (end == std::string::npos) break;
        const std::size_t nm = s.find("\"name\":\"", at);
        if (nm != std::string::npos && nm < end) {
            const std::size_t q = s.find('"', nm + 8);
            if (q != std::string::npos && q < end) out[s.substr(nm + 8, q - nm - 8)] = op;
        }
        at = end;
    }
    return out.size() > 500;
}

std::string ExeBuild(const std::wstring& path) { return ExeBuildImpl(path); }

std::vector<std::uint32_t> HandlersNaming(const Pe& pe, const std::map<std::uint32_t, std::uint32_t>& byOp, const char* text) {
    std::vector<std::uint32_t> out;
    const std::size_t tl = std::strlen(text);
    std::uint32_t str = 0;
    for (const auto& s : pe.sections) {
        if (s.name != ".rdata") continue;
        const std::uint8_t* b = pe.base + s.raw;
        for (std::size_t i = 0; i + tl + 1 <= s.rawSize; ++i) {
            if (b[i] != (std::uint8_t)text[0] || std::memcmp(b + i, text, tl) != 0 || b[i + tl] != 0) continue;
            std::size_t st = i;
            while (st > 0 && b[st - 1] != 0) --st;                 // the whole string ends in the text
            if (str) return out;                                   // more than one: not checkable
            str = s.rva + (std::uint32_t)st;
        }
    }
    if (!str) return out;
    std::map<std::uint32_t, std::uint32_t> seen;
    for (const auto& kv : byOp) {
        const std::uint8_t* h = At(pe, kv.second, 0x400);
        if (!h) continue;
        for (std::size_t i = 0; i + 7 <= 0x400; ++i) {
            if ((h[i] == 0x48 || h[i] == 0x4C) && h[i + 1] == 0x8D && (h[i + 2] & 0xC7) == 0x05) {
                std::int32_t rel; std::memcpy(&rel, h + i + 3, 4);
                if ((std::int64_t)kv.second + (std::int64_t)i + 7 + rel == (std::int64_t)str) { out.push_back(kv.first); break; }
            }
        }
    }
    return out;
}

static const std::vector<Found>& RunLocked(const std::wstring& exePath, const std::wstring& opcodesJson, bool needLabel) {
    // the build label the launcher keeps beside the table
    const std::size_t slash = opcodesJson.find_last_of(L"\\/");
    const std::wstring buildTxt = (slash == std::wstring::npos ? std::wstring() : opcodesJson.substr(0, slash + 1)) + L"client_version.txt";
    const std::wstring key = FileKey(exePath) + L"#" + FileKey(opcodesJson) + L"#" + FileKey(buildTxt) + (needLabel ? L"" : L"#nolabel");
    if (!g_cache.key.empty() && g_cache.key == key) return g_cache.found;
    g_cache = Cache{}; g_cache.key = key;
    std::ostringstream rep;
    std::vector<std::uint8_t> f; Pe pe{};
    std::map<std::uint32_t, std::uint32_t> handlers; std::map<std::string, std::uint32_t> ops;
    std::string clockLine;
    auto give_up = [&](const std::string& why) -> const std::vector<Found>& {
        g_cache.why = why;
        g_cache.report = "calibrate: " + why + "\n" + clockLine;
        return g_cache.found;
    };
    if (!ReadFile(exePath, f) || !ParsePe(f, pe)) return give_up("client exe not readable");
    g_cache.table.exeStamp = pe.stamp;
    {   // the engine clock comes from code, not from the operation table
        std::string why;
        Found fd{ "kEngineClock", 0, EngineClock(pe, why), "engine clock reads", Outcome::NotFound };
        fd.status = fd.found ? Outcome::Found : Outcome::NotFound;
        char line[160];
        if (fd.found) std::snprintf(line, sizeof(line), "  kEngineClock: root global + 0x%X (from engine clock reads)\n", fd.found);
        else std::snprintf(line, sizeof(line), "  kEngineClock: not found (%s)\n", why.c_str());
        clockLine = line;
        g_cache.found.push_back(fd);
    }
    if (!Handlers(pe, handlers)) return give_up("operation registrar not recognised");
    g_cache.table.handlers = (int)handlers.size();
    // the names the handlers earn by their own code
    rtx::opprints::Named byCode;
    {
        rtx::opprints::Image im;
        if (rtx::opprints::Open(f.data(), f.size(), false, im)) {
            rtx::opprints::Prints pr;
            rtx::opprints::Take(im, handlers, pr);
            rtx::opprints::Resolve(pr, rtx::opprints::kRefs, rtx::opprints::kRefCount, byCode);
        }
    }
    g_cache.table.fromCode = (int)byCode.byName.size();
    // The handlers that name themselves, against a set of names: the first disagreement, or empty.
    auto spot_fail = [&](const std::map<std::string, std::uint32_t>& names, std::vector<SpotCheck>* keep) {
        std::string fail;
        for (const Spot& sp : kSpots) {
            SpotCheck sc; sc.text = sp.text; sc.op = sp.op;
            auto o = names.find(sp.op);
            const std::vector<std::uint32_t> owners = HandlersNaming(pe, handlers, sp.text);
            for (std::size_t i = 0; i < owners.size(); ++i) sc.owners += (i ? "," : "") + std::to_string(owners[i]);
            if (o != names.end()) sc.number = (int)o->second;
            if (o == names.end() || owners.empty()) sc.ok = -1;
            else sc.ok = std::find(owners.begin(), owners.end(), o->second) != owners.end() ? 1 : 0;
            if (sc.ok == 0 && fail.empty())
                fail = std::string(sp.op) + " is named operation " + std::to_string(sc.number) +
                       ", the handler naming " + sp.text + " is op " + sc.owners;
            if (keep) keep->push_back(sc);
        }
        return fail;
    };
    // An export of this build's scripts, where the launcher keeps one, adds the names the code did
    // not settle. Where the two differ the code stands: the exporter numbers the ops it cannot name
    // in sequence (unk11010, ...), so one op more or less renumbers every placeholder after it, and
    // the names keyed to a placeholder move with it. It is not needed: without one the names are
    // the code's alone.
    ops = byCode.byName;
    {
        std::map<std::string, std::uint32_t> exported;
        const bool read = OpTable(opcodesJson, exported);
        const std::string mismatch = TableBuildMismatch(exePath, opcodesJson, buildTxt, g_cache.table);
        std::string note = !read ? std::string("none on this PC") : needLabel ? mismatch : std::string();
        if (note.empty()) {
            std::set<std::string> ruled;
            for (const Rule& r : kRules) ruled.insert(r.op);
            // Only real names are compared: a placeholder the code learned from an earlier export names
            // another handler in this one.
            int differ = 0, differRuled = 0, compared = 0;
            std::map<std::uint32_t, std::string> exportedByOp;
            for (const auto& kv : exported) exportedByOp.emplace(kv.second, kv.first);
            std::set<std::uint32_t> codeOps;
            for (const auto& kv : byCode.byName) {
                codeOps.insert(kv.second);
                if (Placeholder(kv.first)) continue;
                ++compared;
                const auto it = exported.find(kv.first);
                if (it != exported.end() && it->second != kv.second) {
                    ++differ;
                    if (ruled.count(kv.first)) ++differRuled;
                    const auto on = exportedByOp.find(kv.second);
                    g_cache.differs.push_back({ kv.first, kv.second, it->second, on == exportedByOp.end() ? std::string() : on->second });
                }
            }
            // the export's real names, less any on a handler the code named: one name per handler. Its
            // placeholders are left out, a rule keyed to one reads the handler the code gave it.
            std::map<std::string, std::uint32_t> merged = byCode.byName;
            for (const auto& kv : exported)
                if (!codeOps.count(kv.second) && !Placeholder(kv.first)) merged.insert(kv);
            // An export of this build names its handlers as their code does (0 differ on 950-1). One
            // whose version resource matches but whose names mostly disagree is another build's: the
            // plugin beta client carries the live version string on other operation numbers.
            if (compared > 0 && differ * 20 > compared) {
                char why[200];
                std::snprintf(why, sizeof(why), "the export names %d of %d handlers differently from their own code: it is another build's (%s)",
                              differ, compared, g_cache.table.label.empty() ? "no label" : g_cache.table.label.c_str());
                note = why;
            } else note = spot_fail(merged, nullptr);
            if (note.empty()) {
                ops.swap(merged);
                g_cache.table.exportUsed = true; g_cache.table.differ = differ; g_cache.table.differRuled = differRuled;
            }
        }
        g_cache.table.exportNote = note;
    }
    const std::string spotFail = spot_fail(ops, &g_cache.spots);
    if (!spotFail.empty()) return give_up(spotFail);
    if (ops.empty()) return give_up("no operation recognised by its code in this exe");
    g_cache.table.names = (int)ops.size();
    g_cache.table.usable = true;
    g_cache.ops = ops;
    rep << "calibrate: " << handlers.size() << " handlers, " << ops.size() << " named operations (" << byCode.byName.size() << " by their own code"
        << (g_cache.table.exportUsed ? ", the rest by the export of this build" : "; script export not used: " + g_cache.table.exportNote) << ")\n" << clockLine;
    // handler entry points in address order: a handler's scan stops where the next one begins
    std::vector<std::uint32_t> starts;
    for (const auto& kv : handlers) starts.push_back(kv.second);
    std::sort(starts.begin(), starts.end());
    const std::map<std::uint32_t, std::uint32_t> functions = FunctionRanges(pe);
    // One rule against this exe: the value it finds (0 for none) and why not.
    struct Try { std::uint32_t value = 0; Outcome status = Outcome::NotFound; };
    auto try_rule = [&](const Rule& rule) {
        Try t;
        // lo and hi from what the rule named by rel found earlier in the table; nothing found
        // there, nothing here
        Rule r = rule;
        if (r.rel) {
            std::uint32_t base = 0;
            for (const Found& f : g_cache.found) if (std::strcmp(f.name, r.rel) == 0) { base = f.found; break; }
            if (!base) return t;
            r.lo += base; r.hi += base;
        }
        // a name the code could not tell from its look-alikes stands for all of them: the rule
        // holds when every one reads the same place
        std::vector<std::uint32_t> cands;
        if (auto o = ops.find(r.op); o != ops.end()) cands.push_back(o->second);
        else if (auto sh = byCode.shared.find(r.op); sh != byCode.shared.end()) cands = sh->second;
        if (cands.empty()) { t.status = Outcome::NoOp; return t; }
        bool first = true;
        for (const std::uint32_t op : cands) {
            auto h = handlers.find(op);
            if (h == handlers.end()) { t.value = 0; t.status = Outcome::NoHandler; return t; }
            auto next = std::upper_bound(starts.begin(), starts.end(), h->second);
            std::uint32_t at = h->second, end = next != starts.end() ? *next : pe.textRva + pe.textSize;
            if (r.wrap) {
                // the shape sits in the callback the wrapper hands on, and nowhere past its end; a
                // handler that is no wrapper is read as is
                if (const std::uint32_t cb = WrapperCallback(pe, at)) { at = cb; end = RoutineEnd(pe, cb, 0x140, functions); }
            }
            if (r.call >= 0) {
                // the shape sits in the routine the handler's call names, and nowhere past its end
                const std::uint32_t cb = CallTarget(pe, at, end, r.call, functions);
                if (!cb) { t.value = 0; t.status = Outcome::NotFound; return t; }
                at = cb; end = RoutineEnd(pe, cb, r.span, functions);
            }
            bool amb = false;
            const std::uint32_t v = FindDisp(pe, at, end, r, amb);
            if (amb || (!first && v != t.value)) { t.value = 0; t.status = Outcome::Ambiguous; return t; }
            t.value = v; first = false;
        }
        t.status = t.value ? Outcome::Found : Outcome::NotFound;
        return t;
    };
    // the rules by name, in table order
    std::vector<const Rule*> order;
    std::map<std::string, std::vector<const Rule*>> byName;
    for (const Rule& r : kRules) { auto& v = byName[r.name]; if (v.empty()) order.push_back(&r); v.push_back(&r); }
    static const char* kWord[] = { "found", "moved", "not found", "more than one candidate", "not recognised", "no handler" };
    for (const Rule* lead : order) {
        const auto& group = byName[lead->name];
        Found fd{ lead->name, lead->compiled, 0, lead->op, Outcome::NotFound };
        fd.ref = lead->compiled != 0;
        fd.what = lead->what;
        Try leadTry; bool conflict = false;
        std::string failed, values;   // the rules that found nothing, and every value found, for the report
        for (std::size_t i = 0; i < group.size(); ++i) {
            const Rule& r = *group[i];
            const Try t = try_rule(r);
            if (i == 0) leadTry = t;
            // two rules of a name on one operation are told apart by their shape
            std::string who = r.op;
            for (const Rule* o : group)
                if (o != &r && std::strcmp(o->op, r.op) == 0) { who += std::string(" [") + r.head + "]"; break; }
            char note[200];
            if (t.value) {
                std::snprintf(note, sizeof(note), "%s 0x%X", who.c_str(), t.value);
                values += std::string(values.empty() ? "" : ", ") + note;
                if (!fd.found) { fd.found = t.value; fd.op = r.op; }
                else if (t.value != fd.found) conflict = true;
            } else {
                std::snprintf(note, sizeof(note), "%s %s", who.c_str(), kWord[(int)t.status]);
                failed += std::string(failed.empty() ? "" : ", ") + note;
            }
        }
        if (conflict) { fd.found = 0; fd.status = Outcome::Ambiguous; }
        else if (fd.found) fd.status = Outcome::Found;
        else fd.status = leadTry.status;
        if (fd.ref && fd.status == Outcome::Found && fd.found != fd.compiled) fd.status = Outcome::Moved;
        char line[400];
        if (conflict) std::snprintf(line, sizeof(line), "  %s: not found, the rules disagree (%s)\n", fd.name, values.c_str());
        else if (!fd.found && fd.ref) std::snprintf(line, sizeof(line), "  %s: not found (%s; compiled 0x%X)\n", fd.name, failed.c_str(), fd.compiled);
        else if (!fd.found) std::snprintf(line, sizeof(line), "  %s: not found (%s)\n", fd.name, failed.c_str());
        else if (fd.ref) std::snprintf(line, sizeof(line), "  %s: 0x%X (from %s; compiled 0x%X%s%s)\n", fd.name, fd.found, fd.op, fd.compiled, failed.empty() ? "" : "; ", failed.c_str());
        else std::snprintf(line, sizeof(line), "  %s: 0x%X (from %s%s%s)\n", fd.name, fd.found, fd.op, failed.empty() ? "" : "; ", failed.c_str());
        rep << line;
        g_cache.found.push_back(fd);
    }
    // A name the code and the export give to different handlers counts against the table only where
    // a rule keyed to it found nothing: a rule that found its field in the handler the code names has
    // settled which of the two is that operation.
    if (g_cache.table.differRuled) {
        int open = 0;
        for (const auto& d : g_cache.differs) {
            bool ruled = false, missing = false;
            for (const Rule& r : kRules) {
                if (d.name != r.op) continue;
                ruled = true;
                for (const Found& f : g_cache.found) if (std::strcmp(f.name, r.name) == 0 && !f.found) missing = true;
            }
            if (ruled && missing) ++open;
        }
        g_cache.table.differRuled = open;
    }
    {   // the varp hash map, derived from the varp manager the QUEST_FINISHED handler names
        static char from[64];
        std::snprintf(from, sizeof(from), "QUEST_FINISHED (varp manager + 0x%X)", kVarpHashFromMgr);
        Found fd{ "kOffVarpHash", 0, 0, from, Outcome::NotFound };
        for (const auto& f : g_cache.found)
            if (std::strcmp(f.name, "md::kVarpMgr") == 0 && f.found) { fd.found = f.found + kVarpHashFromMgr; fd.status = Outcome::Found; }
        char line[160];
        if (fd.found) std::snprintf(line, sizeof(line), "  kOffVarpHash: 0x%X (varp manager + 0x%X)\n", fd.found, kVarpHashFromMgr);
        else std::snprintf(line, sizeof(line), "  kOffVarpHash: not found (the varp manager was not)\n");
        rep << line;
        g_cache.found.push_back(fd);
    }
    g_cache.report = rep.str();
    return g_cache.found;
}

const std::vector<Found>& Run(const std::wstring& exePath, const std::wstring& opcodesJson) {
    std::lock_guard<std::mutex> lk(g_mu);
    return RunLocked(exePath, opcodesJson, true);
}

std::uint32_t Use(const char* name, std::uint32_t compiled) {
    std::lock_guard<std::mutex> lk(g_mu);
    for (auto& fd : g_cache.found) {
        if (std::strcmp(fd.name, name) != 0) continue;
        fd.compiled = compiled;
        fd.applied = true;
        if (fd.status == Outcome::Found && fd.found != compiled) fd.status = Outcome::Moved;
        else if (fd.status == Outcome::Moved && fd.found == compiled) fd.status = Outcome::Found;
        return fd.found ? fd.found : compiled;
    }
    return compiled;
}

std::string Report() {
    std::lock_guard<std::mutex> lk(g_mu);
    std::string r = g_cache.report;
    for (const auto& fd : g_cache.found)
        if (fd.found && fd.compiled && fd.found != fd.compiled) {
            char line[160];
            std::snprintf(line, sizeof(line), "  %s MOVED: compiled 0x%X, client 0x%X, %s\n", fd.name, fd.compiled, fd.found,
                          fd.ref && !fd.applied ? "the compiled copy still reads the old one" : "using the client's");
            r += line;
        }
    return r;
}

std::vector<Found> Results() {
    std::lock_guard<std::mutex> lk(g_mu);
    std::vector<Found> out;
    for (const auto& fd : g_cache.found) if (!fd.ref || fd.applied) out.push_back(fd);
    return out;
}

std::vector<Found> References() {
    std::lock_guard<std::mutex> lk(g_mu);
    std::vector<Found> out;
    for (const auto& fd : g_cache.found) if (fd.ref && !fd.applied) out.push_back(fd);
    return out;
}

std::string CheckText(const std::wstring& exePath, const std::wstring& opcodesJson) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_cache = Cache{};
    RunLocked(exePath, opcodesJson, false);
    std::ostringstream o;
    o << "exe stamp " << std::hex << g_cache.table.exeStamp << std::dec << ", version " << g_cache.table.exeVersion
      << ", table label " << (g_cache.table.label.empty() ? "(none)" : g_cache.table.label) << "\n";
    o << "names " << (g_cache.table.usable ? "usable" : "NOT usable: " + g_cache.why) << ", " << g_cache.table.handlers
      << " handlers, " << g_cache.table.names << " named operations, " << g_cache.table.fromCode << " by their own code, export "
      << (g_cache.table.exportUsed ? "used (" + std::to_string(g_cache.table.differ) + " differ)" : "not used: " + g_cache.table.exportNote) << "\n";
    for (const auto& s : g_cache.spots)
        o << "spot " << s.op << " = " << s.number << ", handlers naming " << s.text << ": " << s.owners
          << (s.ok == 1 ? " (agrees)" : s.ok == 0 ? " (DISAGREES)" : " (not checkable)") << "\n";
    static const char* kStatus[] = { "found", "MOVED", "not found", "ambiguous", "no op", "no handler" };
    for (const auto& fd : g_cache.found) {
        char line[220];
        std::snprintf(line, sizeof(line), "%-18s %-34s %-10s found 0x%X", fd.name, fd.op, kStatus[(int)fd.status], fd.found);
        o << line;
        if (fd.ref) { std::snprintf(line, sizeof(line), " compiled 0x%X", fd.compiled); o << line; }
        if (fd.what) o << " (" << fd.what << ")";
        o << "\n";
    }
    if (!g_cache.differs.empty()) {
        std::set<std::string> ruled;
        for (const Rule& r : kRules) ruled.insert(r.op);
        int usedByRule = 0;
        for (const auto& d : g_cache.differs) if (ruled.count(d.name)) ++usedByRule;
        o << "\nnamed differently by the code and the export: " << g_cache.differs.size() << " (" << usedByRule
          << " read by a rule above, marked *)\n";
        o << "  name                                      code   export  export's name for the code's number\n";
        for (const auto& d : g_cache.differs) {
            char line[260];
            std::snprintf(line, sizeof(line), "%c %-42s %5u  %5u  %s\n", ruled.count(d.name) ? '*' : ' ', d.name.c_str(), d.code, d.exported,
                          d.exportOnCode.empty() ? "(none)" : d.exportOnCode.c_str());
            o << line;
        }
    }
    return o.str();
}

std::string Why() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_cache.why;
}

TableState Table() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_cache.table;
}

// The rows of a table header TableText wrote, as `    { "NAME", depth, unique, 0x...ull },` lines,
// and its "Made from" builds. Rows of one name sit together (the header is written sorted).
struct TableRow { std::string name; int depth = 0, unique = 0; std::uint64_t print = 0; };
static bool ReadTable(const std::wstring& path, std::vector<TableRow>& rows, std::string& from) {
    std::vector<std::uint8_t> f;
    if (!ReadFile(path, f)) return false;
    std::string s(f.begin(), f.end());
    std::size_t at = 0;
    while (at < s.size()) {
        std::size_t nl = s.find('\n', at);
        if (nl == std::string::npos) nl = s.size();
        const std::string line = s.substr(at, nl - at);
        at = nl + 1;
        if (const std::size_t m = line.find("Made from: "); m != std::string::npos) {
            from = line.substr(m + 11);
            while (!from.empty() && (from.back() == '\r' || from.back() == ' ')) from.pop_back();
            continue;
        }
        char name[128]; int depth = 0, unique = 0; unsigned long long print = 0;
        if (std::sscanf(line.c_str(), " { \"%127[^\"]\", %d, %d, 0x%llx", name, &depth, &unique, &print) == 4)
            rows.push_back({ name, depth, unique, print });
    }
    return !rows.empty();
}

std::string OpPrintsTable(const std::vector<std::pair<std::wstring, std::wstring>>& builds, const std::wstring& basePath,
                          const std::vector<std::pair<std::string, std::uint32_t>>& labels, std::string& log) {
    std::set<std::string> rows;
    std::string from;
    std::vector<TableRow> base;
    std::vector<rtx::opprints::Ref> baseRefs;
    if (!basePath.empty()) {
        if (!ReadTable(basePath, base, from)) { log += "the table to build on is not readable\n"; return {}; }
        std::stable_sort(base.begin(), base.end(), [](const TableRow& a, const TableRow& b) { return a.name < b.name; });
        char buf[64];
        for (const TableRow& r : base) {
            baseRefs.push_back({ r.name.c_str(), (std::uint8_t)r.depth, (std::uint8_t)r.unique, r.print });
            std::snprintf(buf, sizeof(buf), "\t%d\t%d\t%016llx", r.depth, r.unique, (unsigned long long)r.print);
            rows.insert(r.name + buf);
        }
        char line[200];
        std::snprintf(line, sizeof(line), "building on %zu rows (made from %s)\n", rows.size(), from.c_str());
        log += line;
    }
    for (const auto& b : builds) {
        std::vector<std::uint8_t> f; Pe pe{};
        std::map<std::uint32_t, std::uint32_t> handlers; std::map<std::string, std::uint32_t> names;
        rtx::opprints::Image im;
        char line[200];
        if (!ReadFile(b.first, f) || !ParsePe(f, pe) || !Handlers(pe, handlers) || !rtx::opprints::Open(f.data(), f.size(), false, im)) { log += "exe not readable, or no registrar in it\n"; return {}; }
        if (!OpTable(b.second, names)) { log += "script export's operation names not readable\n"; return {}; }
        // the export has to be this exe's: every handler that names itself sits under its number
        int checked = 0;
        for (const Spot& sp : kSpots) {
            const auto o = names.find(sp.op);
            const std::vector<std::uint32_t> owners = HandlersNaming(pe, handlers, sp.text);
            if (o == names.end() || owners.empty()) continue;
            ++checked;
            if (std::find(owners.begin(), owners.end(), o->second) == owners.end()) {
                std::snprintf(line, sizeof(line), "%08x: the export names %s %u, another handler names itself that: not this exe's export\n", pe.stamp, sp.op, o->second);
                log += line; return {};
            }
        }
        if (checked < 3) { log += "fewer than 3 self-naming handlers to hold the export against\n"; return {}; }
        rtx::opprints::Prints pr;
        rtx::opprints::Take(im, handlers, pr);
        int placeholders = 0, kept = 0;
        if (!base.empty()) {
            // The exporter labels operations by what their scripts do with them, and a label moves to
            // another handler between exports. A shipped name keeps its handler as long as that
            // handler's code is still found in this build: the client reads by code, and the rules
            // were proved on it. The export names what the shipped rows no longer find. Placeholders
            // are per-export labels and none of this build's go in beside the shipped ones.
            rtx::opprints::Named shipped;
            rtx::opprints::Resolve(pr, baseRefs.data(), baseRefs.size(), shipped);
            for (auto it = names.begin(); it != names.end(); ) {
                const auto s = shipped.byName.find(it->first);
                if (Placeholder(it->first)) { ++placeholders; it = names.erase(it); }
                else if (s != shipped.byName.end() && s->second != it->second) {
                    ++kept;
                    std::snprintf(line, sizeof(line), "  %s: the shipped rows name operation %u, the export %u; the shipped handler stands\n", it->first.c_str(), s->second, it->second);
                    log += line;
                    it = names.erase(it);
                } else ++it;
            }
        }
        const std::size_t before = rows.size();
        rtx::opprints::RowsOf(pr, names, rows);
        // a shipped label whose handler changed code: this build's rows for it, under the shipped label
        for (const auto& l : labels) {
            if (handlers.find(l.second) == handlers.end()) { std::snprintf(line, sizeof(line), "  %s: operation %u is not a handler of this build\n", l.first.c_str(), l.second); log += line; return {}; }
            std::map<std::string, std::uint32_t> one{ { l.first, l.second } };
            rtx::opprints::RowsOf(pr, one, rows);
            std::snprintf(line, sizeof(line), "  %s: the rows of operation %u added under the shipped label\n", l.first.c_str(), l.second);
            log += line;
        }
        int alone = 0;
        for (const auto& kv : names) { const auto p = pr.of[rtx::opprints::kDeepest].find(kv.second); if (p != pr.of[rtx::opprints::kDeepest].end() && pr.at[rtx::opprints::kDeepest].at(p->second).size() == 1) ++alone; }
        const std::string ver = ExeBuildImpl(b.first);
        std::snprintf(line, sizeof(line), "%s %08x: %zu handlers, %zu names, %d alone in their code, %zu rows added", ver.c_str(), pe.stamp, handlers.size(), names.size(), alone, rows.size() - before);
        log += line;
        if (!base.empty()) { std::snprintf(line, sizeof(line), "; %d placeholders left out, %d names kept as shipped", placeholders, kept); log += line; }
        log += "\n";
        std::snprintf(line, sizeof(line), "%s%s %08x", from.empty() ? "" : ", ", ver.c_str(), pe.stamp);
        from += line;
    }
    return rtx::opprints::TableText(rows, from);
}

std::map<std::string, std::uint32_t> OpNames() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_cache.ops;
}

std::vector<SpotCheck> SpotChecks() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_cache.spots;
}

std::map<std::uint32_t, std::uint32_t> ExeHandlers(const std::wstring& exePath) {
    std::map<std::uint32_t, std::uint32_t> out;
    std::vector<std::uint8_t> f; Pe pe{};
    if (!ReadFile(exePath, f) || !ParsePe(f, pe) || !Handlers(pe, out)) out.clear();
    return out;
}

}  // namespace rtx::calib
