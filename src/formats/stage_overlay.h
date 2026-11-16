// PRO/WSTAG###.PRO per-stage overlays (MIPS code + data, loaded at 0x800A4CA0). We do not run the
// code: a tiny register tracker finds the stage-init function's stores into the field engine's
// stage descriptor (0x800990B4) and reads the data tables it points at. See docs/FORMATS.md.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/bytes.h"

namespace dw3 {

constexpr uint32_t kWstagBase = 0x800A4CA0;  // code entry (file offset 4)
// Data pointers inside the overlay map to file offsets as (addr - kWstagData): verified by the actor
// table, condition lists and talk tables (every pointer lands on a record start).
constexpr uint32_t kWstagData = 0x800A4CA4;
constexpr uint32_t kStageDesc = 0x800990B4;

// Scene object (descriptor +0x10 table, 18-byte records): draws a TMPK piece at (x, y).
struct OverlayObject {
    uint8_t type = 0, link = 0;   // link 0x64.. = tied to an event actor slot (open)
    uint16_t flags = 0;           // e.g. 0x0240 / 0x0640 / 0x0440 (door) / 0x0A40 (open)
    uint8_t frame = 0, mode = 0;  // frame id; mode 0 static, 1 cycle frames a..b, 2 cycle sub-counter
    uint8_t loopFrom = 0, loopTo = 0, delay = 0, counter = 0;
    int16_t x = 0, y = 0;         // map pixels, top-left of the piece
    uint16_t depthY = 0;          // door objects: y of the sort line (else 0)
    uint16_t unk = 0;
};

// Story flag test or assignment: {u16 flag, u16 value} lists ended by flag 0xFFFF.
struct FlagCond {
    uint16_t flag = 0;
    uint16_t value = 0;
};

// One talk table entry: if every `cond` holds, show TALK message `msg` and apply `set`.
struct TalkEntry {
    std::vector<FlagCond> cond, set;
    uint32_t msg = 0;  // index into the area's TALK bank (DAT/COUNTRY/USA/USTALK##.BIN)
};

// Event actor (descriptor +0x4C pointer list, 20-byte records): a placed NPC for one story state.
// FIELDSTG 0x8008A7CC: the actor is created only when its `show` list holds (0x80048288 callback).
struct OverlayActor {
    uint32_t showPtr = 0;   // +0x00 condition list for this variant to appear (0 = always)
    uint32_t talkPtr = 0;   // +0x04 talk table: {condPtr, setPtr, msgId} triples, ended by 3 zeros
    uint16_t charId = 0;    // +0x08 character id (NOT the P### sprite number; mapping is a FIELDSTG callback, open)
    uint16_t slot = 0;      // +0x0A actor slot; several records share a slot (one per story state)
    int16_t x = 0, y = 0;   // +0x0C/+0x0E map pixels (feet)
    int16_t dir = 0;        // +0x10 facing 0..7 (original order; see FORMATS.md)
    std::vector<FlagCond> show;
    std::vector<TalkEntry> talk;
};

// Area link (descriptor +0x14, 24-byte records ended by an all-zero kind): one per trigger index.
// The trigger map value of a zone is (type << 5) | index; `index` selects the record of this table.
// FIELDSTG reads: u16 flag, value (condition 1; flag 0xFFFF = none), u16 flag, value (condition 2),
// u16 kind, u16 arg, i16 x, y, u16 dir, u16 link, u16 pad[2].
//   kind 1 = warp: arg = target stage KEY (0x200 + index in FIELDSTG's stage table), x/y/dir = arrival,
//            link = the door scene object of this stage (scene objects with the same `link`, 0x64..) or 0
//   kind 8 = event zone: arg = event id (handler table at descriptor +0x24), e.g. "I need my partner" blocks
//   kind 7 = seen on S202 (arg 0x40/0x20/0xFFE0, x = small signed value): open
struct OverlayLink {
    std::vector<FlagCond> cond;
    uint16_t kind = 0, arg = 0;
    int16_t x = 0, y = 0;
    uint16_t dir = 0, link = 0;
};

struct StageOverlay {
    bool ok = false;
    uint32_t desc[32] = {};        // descriptor words stored by the init function (index = offset / 4)
    bool has[32] = {};
    int spawnX = -1, spawnY = -1;  // +0x2C / +0x30 (8.8 fixed point) = player start
    std::vector<OverlayObject> objects;
    std::vector<OverlayActor> actors;
    std::vector<OverlayLink> links;  // index = trigger index (zone value & 31)
};

bool loadStageOverlay(ByteView pro, StageOverlay& out, std::string* err = nullptr);

}  // namespace dw3
