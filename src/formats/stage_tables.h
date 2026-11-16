// Plain-text tables extracted from the original stage overlays by `dw3check --extract-npcs`:
//   data/npcs/S###.npc   the event actors (NPCs) of a stage
//   data/doors/S###.door its area links (doors / exits / event zones)
// One record per line, so they can be imported into a database later. No game text in them: talk
// lines are message ids into the player's own USTALK##.BIN. See docs/FORMATS.md.
#pragma once
#include <string>
#include <vector>

#include "formats/stage_overlay.h"

namespace dw3 {

// npc SLOT CHAR SPRITE X Y DIR [show F=V,...] [talk COND/SET/MSG]...
//   SPRITE "-" = no sprite (invisible blocker); COND/SET = "F=V,F=V" or "-"; MSG hex id.
struct NpcRecord {
    int slot = 0, charId = 0;
    std::string sprite;
    int x = 0, y = 0, dir = 0;
    std::vector<FlagCond> show;
    std::vector<TalkEntry> talk;
};

// door INDEX warp STAGE X Y DIR [link 0xNN] [if F=V,...]
// door INDEX event 0xARG [if ...]          door INDEX kind K 0xARG X Y DIR [if ...]
struct DoorRecord {
    int index = 0;      // trigger index (zone value & 31)
    int kind = 0;       // 1 warp, 8 event, other = unknown
    std::string target; // warp: stage id (S225), or "key0x20b" when the stage key has no known file
    int arg = 0;        // event id / raw arg
    int x = 0, y = 0, dir = 0;
    int link = 0;       // door scene object link id (0x64..), 0 = none
    std::vector<FlagCond> cond;
};

std::string flagListText(const std::vector<FlagCond>& l);  // "6002=1,1a00=0" or "-"
bool parseFlagList(const std::string& s, std::vector<FlagCond>& out);

bool loadNpcTable(const std::string& path, std::vector<NpcRecord>& out, std::string* err = nullptr);
bool saveNpcTable(const std::string& path, const std::string& header, const std::vector<NpcRecord>& in);
bool loadDoorTable(const std::string& path, std::vector<DoorRecord>& out, std::string* err = nullptr);
bool saveDoorTable(const std::string& path, const std::string& header, const std::vector<DoorRecord>& in);

}  // namespace dw3
