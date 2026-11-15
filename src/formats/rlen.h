// "RLEN" byte run-length compression used by DW3 stage/mask packs.
//   "RLEN" u32 decompressedSize, then a stream of:
//     c & 0x80 -> repeat next byte (c & 0x7f) times
//     else     -> copy the next c bytes literally
#pragma once
#include "core/bytes.h"

namespace dw3 {

bool isRlen(ByteView v, size_t off);
bool decodeRlen(ByteView v, size_t off, Bytes& out, size_t* end = nullptr);

}  // namespace dw3
