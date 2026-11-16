// PRO/FIELDSTG.PRO (the field engine overlay, loaded at 0x80082448): the tables it uses to create
// event actors and to find stages. We read data only, never run the code. See docs/FORMATS.md.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/bytes.h"

namespace dw3 {

constexpr uint32_t kFieldStgBase = 0x80082448;

// Character table entry (FIELDSTG 0x80099134, u32 per character id; read by the callback at stage
// descriptor +0x74 = 0x80091398 and stored at actor +0x9C). The high half is the FILE ID of the
// character's sprite pack (DAT/FIELD/SPRT/P###XXXX.BIN), the low half is passed on with it (open).
struct FieldChar {
    uint16_t fileId = 0;  // 0 = no sprite (invisible actor, e.g. chars 112/113 = counter blockers)
    uint16_t lo = 0;
    uint8_t size = 0;     // byte table 0x80099758 (descriptor +0x78 callback), halved into actor +0x80 (open)
};

struct FieldStgTables {
    bool ok = false;
    std::vector<FieldChar> chars;             // index = character id (WSTAG event actor +0x08)
    std::map<uint16_t, uint16_t> stageFiles;  // stage key (0x200..) -> WSTAG overlay file id
};

bool loadFieldStg(ByteView fieldstg, FieldStgTables& out, std::string* err = nullptr);

// File ids are indexes into the main executable's LBA table (SLUS_014.36, file offset 0x35378), which
// we do not ship or read at runtime. `data/file_ids.cfg` (made once by `dw3check --map-file-ids DISC`)
// maps the ids to the names of the extracted files: one "0xID NAME" line per file (NAME without .BIN/.PRO).
bool loadFileIds(const std::string& path, std::map<uint16_t, std::string>& out);

}  // namespace dw3
