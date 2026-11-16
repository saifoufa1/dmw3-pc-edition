#include "formats/dw3_text.h"

#include <cstdio>

namespace dw3 {

namespace {
// 0x01 xx symbol table (USA banks). Letters after 0x01 are rare typos in the data: shown as the letter.
char symbol(uint8_t c) {
    switch (c) {
        case 0x01: return ' ';
        case 0x04: return ',';
        case 0x05: return '.';
        case 0x07: return ':';
        case 0x0f: return '/';
        case 0x13: return '\'';
        case 0x14: return '"';
        case 0x15: return '(';
        case 0x16: return ')';
        case 0x1a: return '-';
        case 0xe6: return '?';
        case 0xe7: return '!';
        default: break;
    }
    if (c >= 0x0e && c <= 0x27) return char('A' + (c - 0x0e));
    if (c >= 0x28 && c <= 0x41) return char('a' + (c - 0x28));
    return 0;
}

void rtrimLines(std::string& s) {
    std::string out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t nl = s.find('\n', start);
        std::string line = s.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        while (!line.empty() && line.back() == ' ') line.pop_back();
        size_t lead = 0;
        while (lead < line.size() && line[lead] == ' ') ++lead;
        out += line.substr(lead);
        if (nl == std::string::npos) break;
        out += '\n';
        start = nl + 1;
    }
    while (!out.empty() && out.back() == '\n') out.pop_back();
    s = out;
}
}  // namespace

Message decodeMessage(ByteView v, size_t off, size_t end, const std::string& playerName, int* unknown) {
    Message m;
    int unk = 0;
    std::string cur;
    bool inName = false;
    bool terminated = false;
    auto push = [&] {
        rtrimLines(cur);
        if (!cur.empty()) m.pages.push_back(cur);
        cur.clear();
    };
    auto emit = [&](const std::string& s) {
        if (inName) m.speaker += s;
        else cur += s;
    };
    if (end > v.size) end = v.size;
    size_t i = off;
    while (i < end) {
        const uint8_t c = v.data[i];
        if (c == 0x00) { terminated = true; break; }
        if (c >= 0x0e && c <= 0x27) { emit(std::string(1, char('A' + (c - 0x0e)))); ++i; continue; }
        if (c >= 0x28 && c <= 0x41) { emit(std::string(1, char('a' + (c - 0x28)))); ++i; continue; }
        if (c >= 0x04 && c <= 0x0d) { emit(std::string(1, char('0' + (c - 0x04)))); ++i; continue; }
        if (c == 0xe6 || c == 0xe7) { emit(std::string(1, symbol(c))); ++i; continue; }
        if (c == 0x03) { push(); ++i; continue; }  // clear box / new page
        if (i + 1 >= end) { ++unk; break; }
        const uint8_t d = v.data[i + 1];
        if (c == 0x01) {
            const char s = symbol(d);
            if (s) emit(std::string(1, s));
            else if (d != 0x02) { char buf[8]; std::snprintf(buf, sizeof buf, "{1:%02x}", d); emit(buf); ++unk; }
            i += 2;
            continue;
        }
        if (c == 0x02) {
            if (d == 0x00) { terminated = true; break; }
            if (d == 0x01) { emit("\n"); i += 2; continue; }
            if (d == 0x02) { if (!inName) push(); i += 2; continue; }  // wait for button
            if (d == 0x07) { inName = !inName; i += 2; continue; }      // speaker name field
            if (d == 0x09) { emit(playerName); i += 2; continue; }      // player name
            if (d == 0x08) { emit(playerName); i += 6; continue; }      // player name, 4 arg bytes (01 02 05 01)
            char buf[8]; std::snprintf(buf, sizeof buf, "{2:%02x}", d); emit(buf); ++unk;
            i += 2;
            continue;
        }
        char buf[8]; std::snprintf(buf, sizeof buf, "{%02x}", c); emit(buf); ++unk;
        ++i;
    }
    push();
    if (unknown) *unknown = unk;
    m.ok = terminated && !inName;  // a message may be empty (e.g. a bare wait)
    return m;
}

bool loadEventBank(ByteView v, EventBank& out, std::string* err) {
    out = {};
    auto fail = [&](const char* s) { if (err) *err = s; return false; };
    uint32_t first;
    if (!v.u32(0, first) || first < 4 || (first & 3) || first > v.size) return fail("bad block table");
    const size_t n = first / 4;
    std::vector<uint32_t> offs(n);
    for (size_t i = 0; i < n; ++i) {
        v.u32(i * 4, offs[i]);
        if (offs[i] < first || offs[i] > v.size || (i && offs[i] < offs[i - 1])) return fail("bad block offset");
    }
    for (size_t b = 0; b < n; ++b) {
        const uint32_t s = offs[b], e = b + 1 < n ? offs[b + 1] : uint32_t(v.size);
        uint32_t cnt;
        if (!v.u32(s, cnt) || cnt == 0 || cnt > 512 || 4 + cnt * 4 > e - s) return fail("bad block header");
        std::vector<std::pair<uint32_t, uint32_t>> msgs;
        for (uint32_t k = 0; k < cnt; ++k) {
            uint32_t o;
            v.u32(s + 4 + k * 4, o);
            if (o < 4 + cnt * 4 || s + o > e) return fail("bad message offset");
            msgs.push_back({s + o, e});
        }
        out.blocks.push_back(std::move(msgs));
    }
    out.ok = true;
    return true;
}

bool loadTalkBank(ByteView v, TalkBank& out, std::string* err) {
    out = {};
    auto fail = [&](const char* s) { if (err) *err = s; return false; };
    uint32_t n;
    if (!v.u32(0, n) || n == 0 || n > 8192 || 4 + size_t(n) * 4 > v.size) return fail("bad talk table");
    std::vector<uint32_t> offs(n);
    for (uint32_t i = 0; i < n; ++i) {
        v.u32(4 + i * 4, offs[i]);
        if (offs[i] < 4 + n * 4 || offs[i] > v.size || (i && offs[i] < offs[i - 1])) return fail("bad talk offset");
    }
    for (uint32_t i = 0; i < n; ++i) out.msgs.push_back({offs[i], i + 1 < n ? offs[i + 1] : uint32_t(v.size)});
    out.ok = true;
    return true;
}

int checkMessages(ByteView v, const std::vector<std::pair<uint32_t, uint32_t>>& msgs, int* total) {
    int bad = 0, n = 0;
    for (const auto& [s, e] : msgs) {
        uint32_t w;
        if (e - s <= 4 && (!v.u32(s, w) || w == 0)) continue;  // empty slot
        if (v.data[s] == 0) continue;
        int unk = 0;
        Message m = decodeMessage(v, s, e, "Junpei", &unk);
        ++n;
        if (!m.ok || unk) ++bad;
    }
    if (total) *total = n;
    return bad;
}

}  // namespace dw3
