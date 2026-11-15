// Pre-rendered field background: DAT/Z_STAGE/S###PACK.BIN. See docs/FORMATS.md.
#pragma once
#include <string>

#include "core/bytes.h"
#include "formats/image.h"

namespace dw3 {

constexpr int kStageCell = 128;  // each chunk covers a 128x128 pixel cell

struct StageMap {
    int gridW = 0, gridH = 0;
    int chunksDecoded = 0, chunksEmpty = 0, chunksFailed = 0;
    Image image;  // gridW*128 x gridH*128, transparent where nothing is drawn
};

bool loadStagePack(ByteView file, StageMap& out, std::string* err = nullptr);

}  // namespace dw3
