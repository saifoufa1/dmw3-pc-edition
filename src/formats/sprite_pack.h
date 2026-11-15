// Field sprite pack: DAT/FIELD/SPRT/P###XXXX.BIN. See docs/FORMATS.md.
#pragma once
#include <string>

#include "core/bytes.h"
#include "formats/image.h"

namespace dw3 {

struct AnimKey {
    int duration;  // ticks @ 60 Hz
    int frame;     // index into SpritePack::frames
    int ox, oy;    // draw offset relative to the actor's feet
};

struct Anim {
    std::vector<AnimKey> keys;
    bool loop = true;
};

struct SpritePack {
    std::vector<Image> frames;
    std::vector<Anim> anims;  // anims[0..4]=idle S,SE,E,NE,N; [5..9]=walk; [10..14]=run
};

bool loadSpritePack(ByteView file, SpritePack& out, std::string* err = nullptr);

}  // namespace dw3
