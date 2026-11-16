#include "game/story_text.h"

#include <cctype>
#include <cstdlib>

namespace dw3 {

void TextDb::setRoot(std::filesystem::path root, std::string country) {
    root_ = std::move(root);
    country_ = std::move(country);
    prefix_ = country_.substr(0, 2);
    banks_.clear();
}

TextDb::Bank& TextDb::bank(const std::string& name) {
    Bank& b = banks_[name];
    if (!b.tried) {
        b.tried = true;
        if (readFile(root_ / "DAT" / "COUNTRY" / country_ / (prefix_ + name + ".BIN"), b.data)) {
            if (name.rfind("DMG", 0) == 0) b.ok = loadEventBank(b.data, b.ev);
            else b.ok = loadTalkBank(b.data, b.talk);
        }
    }
    return b;
}

bool TextDb::isRef(const std::string& s) {
    const size_t c = s.find(':');
    if (c == std::string::npos || c < 4) return false;
    const std::string head = s.substr(0, c);
    return (head.rfind("dmg", 0) == 0 || head.rfind("talk", 0) == 0) && std::isdigit((unsigned char)head.back());
}

bool TextDb::get(const std::string& ref, Message& out) {
    out = {};
    if (!isRef(ref)) return false;
    const size_t c = ref.find(':');
    std::string head = ref.substr(0, c), rest = ref.substr(c + 1);
    for (auto& ch : head) ch = char(std::toupper((unsigned char)ch));
    Bank& b = bank(head);
    if (!b.ok) return false;
    std::pair<uint32_t, uint32_t> span;
    if (head.rfind("DMG", 0) == 0) {
        const size_t c2 = rest.find(':');
        if (c2 == std::string::npos) return false;
        const long blk = std::strtol(rest.substr(0, c2).c_str(), nullptr, 0);
        const long msg = std::strtol(rest.substr(c2 + 1).c_str(), nullptr, 0);
        if (blk < 0 || size_t(blk) >= b.ev.blocks.size() || msg < 0 || size_t(msg) >= b.ev.blocks[size_t(blk)].size()) return false;
        span = b.ev.blocks[size_t(blk)][size_t(msg)];
    } else {
        const long id = std::strtol(rest.c_str(), nullptr, 0);
        if (id < 0 || size_t(id) >= b.talk.msgs.size()) return false;
        span = b.talk.msgs[size_t(id)];
    }
    out = decodeMessage(b.data, span.first, span.second, player_);
    return out.ok && !out.pages.empty();
}

}  // namespace dw3
