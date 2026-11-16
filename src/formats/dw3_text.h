// DW3 message banks (DAT/COUNTRY/USA/US*.BIN): custom 1/2-byte text encoding, decoded at runtime.
// See docs/FORMATS.md "Message text". Pure parser, no platform code.
//   USDMG###.BIN : event ("drama") banks, one per area: blocks of messages, one block per event.
//   USTALK##.BIN : NPC talk banks: one flat indexed list (ids used by WSTAG talk tables).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/bytes.h"

namespace dw3 {

// One decoded message: optional speaker (the 02 07 ... 02 07 field) and pages of '\n'-separated lines.
struct Message {
    std::string speaker;             // "" = none; "{PLAYER}" is replaced by the player name
    std::vector<std::string> pages;  // at least one page when ok
    bool ok = false;
};

// Decodes one message starting at `off` (stops at 02 00, a bare 00, or `end`).
// Unknown codes are kept as "{xx}" so nothing is silently dropped; `unknown` counts them.
Message decodeMessage(ByteView v, size_t off, size_t end, const std::string& playerName, int* unknown = nullptr);

// Event bank: u32 blockOffsets[] (first offset = table size); block = u32 count, count x u32 offsets
// relative to the block (entry 0 is always an empty message).
struct EventBank {
    std::vector<std::vector<std::pair<uint32_t, uint32_t>>> blocks;  // [block][msg] = absolute {start, end}
    bool ok = false;
};
bool loadEventBank(ByteView v, EventBank& out, std::string* err = nullptr);

// Talk bank: u32 count, count x u32 absolute offsets. Empty slots are 4-byte zero entries.
struct TalkBank {
    std::vector<std::pair<uint32_t, uint32_t>> msgs;  // {start, end}
    bool ok = false;
};
bool loadTalkBank(ByteView v, TalkBank& out, std::string* err = nullptr);

// Validates every message of a bank: returns the number of messages that did not terminate cleanly
// or contained unknown codes (0 = fully decoded).
int checkMessages(ByteView v, const std::vector<std::pair<uint32_t, uint32_t>>& msgs, int* total = nullptr);

}  // namespace dw3
