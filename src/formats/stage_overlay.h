// PRO/WSTAG###.PRO per-stage overlays (MIPS code + data, loaded at 0x800A4CA0). We do not run the
// code: a tiny register tracker finds the stage-init function's stores into the field engine's
// stage descriptor (0x800990B4) and reads the data tables it points at. See docs/FORMATS.md.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/bytes.h"

namespace dw3 {

constexpr uint32_t kWstagBase = 0x800A4CA0;
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

// Event actor (descriptor +0x4C pointer list, 20-byte records): a placed NPC for one story state.
struct OverlayActor {
    uint32_t condPtr = 0;   // condition/script pointer (RAM address; open)
    uint16_t charId = 0;    // character id: (charId & 0xFF) = FIELD/SPRT P### number (bit 8 meaning open)
    uint16_t slot = 0;      // actor slot; several records share a slot (one per story state)
    int16_t x = 0, y = 0;   // map pixels (feet)
    int16_t dir = 0;        // facing 0..7 (original order; see FORMATS.md)
    uint32_t nextPtr = 0;
};

struct StageOverlay {
    bool ok = false;
    uint32_t desc[32] = {};        // descriptor words stored by the init function (index = offset / 4)
    bool has[32] = {};
    int spawnX = -1, spawnY = -1;  // +0x2C / +0x30 (8.8 fixed point) = player start
    std::vector<OverlayObject> objects;
    std::vector<OverlayActor> actors;
};

bool loadStageOverlay(ByteView pro, StageOverlay& out, std::string* err = nullptr);

}  // namespace dw3
