// PS1 TIM texture decoder (4/8/16 bpp). See docs/FORMATS.md.
#pragma once
#include "core/bytes.h"
#include "formats/image.h"

namespace dw3 {

// True if a plausible TIM header (magic 0x10 + known flags) sits at `off`.
bool looksLikeTim(ByteView v, size_t off);

// Decodes the TIM at `off` into RGBA. PS1 colour 0x0000 is transparent.
// `clutRow` picks which palette row to use when the CLUT holds several.
// On success `*end` (optional) receives the offset just past the TIM.
bool decodeTim(ByteView v, size_t off, Image& out, size_t* end = nullptr, int clutRow = 0);

}  // namespace dw3
