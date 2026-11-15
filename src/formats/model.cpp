#include "formats/model.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include "formats/rlen.h"

namespace dw3 {

namespace {

struct Sections {
    std::vector<ByteView> s;
};

bool splitSections(ByteView f, Sections& out, std::string& err) {
    uint32_t first;
    if (!f.u32(0, first) || first < 8 || first % 4 || first > f.size) { err = "bad model table"; return false; }
    const size_t n = first / 4;
    std::vector<uint32_t> offs(n);
    for (size_t i = 0; i < n; ++i) f.u32(i * 4, offs[i]);
    out.s.resize(n);
    for (size_t i = 0; i < n; ++i) {
        // A section runs to the next larger offset (or the end of file).
        uint32_t end = uint32_t(f.size);
        for (size_t j = 0; j < n; ++j)
            if (offs[j] > offs[i] && offs[j] < end) end = offs[j];
        if (offs[i] == 0 || offs[i] > f.size) continue;
        out.s[i] = f.sub(offs[i], end - offs[i]);
    }
    return true;
}

int16_t s16(ByteView v, size_t off) {
    uint16_t u = 0;
    v.u16(off, u);
    return int16_t(u);
}

// Texture section: u32 headerSize, u32 pageOffsets..., 0; each page = RLEN -> TIM.
bool isTexture(ByteView s) {
    uint32_t a = 0;
    return s.u32(0, a) && (a == 8 || a == 12 || a == 16) && s.has(a, 4) && isRlen(s, a);
}

bool loadPages(ByteView s, std::vector<std::vector<uint16_t>>& pages, std::string& err) {
    uint32_t h = 0;
    s.u32(0, h);
    std::vector<uint32_t> offs{h};
    for (uint32_t o = 4; o + 4 <= h; o += 4) {
        uint32_t v = 0;
        s.u32(o, v);
        if (v) offs.push_back(v);
    }
    for (uint32_t off : offs) {
        Bytes tim;
        if (!isRlen(s, off) || !decodeRlen(s, off, tim)) { err = "bad texture RLEN"; return false; }
        ByteView tv(tim);
        uint32_t magic, flags, blen;
        uint16_t bw, bh;
        if (!tv.u32(0, magic) || magic != 0x10 || !tv.u32(4, flags) || !tv.u32(8, blen) || !tv.u16(16, bw) ||
            !tv.u16(18, bh) || bw != 64 || bh != 256 || !tv.has(20, size_t(bw) * bh * 2)) {
            err = "unexpected texture layout";
            return false;
        }
        std::vector<uint16_t> page(size_t(bw) * bh);
        for (size_t i = 0; i < page.size(); ++i) tv.u16(20 + i * 2, page[i]);
        pages.push_back(std::move(page));
    }
    return true;
}

bool readKeys(ByteView s, uint32_t a, uint32_t b, std::vector<ModelKey>& out) {
    if (a > b || b > s.size || (b - a) % 8) return false;
    for (uint32_t o = a; o < b; o += 8) out.push_back({s16(s, o), s16(s, o + 2), s16(s, o + 4), s16(s, o + 6)});
    return true;
}

bool parseTrack(ByteView s, ModelTrack& t, std::string& err) {
    uint32_t p, r, sc, e;
    if (!s.u32(0, p) || !s.u32(4, r) || !s.u32(8, sc) || !s.u32(12, e) || p != 16) { err = "bad track header"; return false; }
    if (!readKeys(s, p, r, t.pos) || !readKeys(s, r, sc, t.rot) || !readKeys(s, sc, e, t.scl) || t.pos.empty() ||
        t.rot.empty()) {
        err = "bad track keys";
        return false;
    }
    return true;
}

bool parseMesh(ByteView s, ModelMesh& m, Model& model, std::string& err) {
    uint32_t vo, no, po;
    if (!s.u32(0, vo) || !s.u32(4, no) || !s.u32(8, po)) { err = "bad mesh header"; return false; }
    uint32_t nv = 0, nn = 0;
    if (!s.u32(vo, nv) || !s.u32(no, nn) || !s.has(vo + 6, size_t(nv) * 6) || !s.has(no + 6, size_t(nn) * 6)) {
        err = "bad mesh lists";
        return false;
    }
    for (uint32_t i = 0; i < nv; ++i) m.verts.push_back({s16(s, vo + 6 + i * 6), s16(s, vo + 8 + i * 6), s16(s, vo + 10 + i * 6)});
    for (uint32_t i = 0; i < nn; ++i) m.norms.push_back({s16(s, no + 6 + i * 6), s16(s, no + 8 + i * 6), s16(s, no + 10 + i * 6)});

    // Primitive stream: bytes >= 0x80 set render state (high nibble = register, low = value),
    // 0x02 + rgb sets the flat colour, 0x00 / 0x01 (+ 7-byte texture header) emit one primitive.
    uint8_t st[16] = {};
    uint8_t ub = 0, vb = 0, clut = 0, r = 128, g = 128, b = 128;
    bool haveHdr = false;
    size_t p = po;
    for (;;) {
        if (!s.has(p, 1)) { err = "mesh stream overrun"; return false; }
        const uint8_t c = s.data[p++];
        if (c == 0xff) break;
        if (c & 0x80) { st[c >> 4] = c & 15; continue; }
        if (c == 2) {
            if (!s.has(p, 3)) { err = "mesh colour overrun"; return false; }
            r = s.data[p]; g = s.data[p + 1]; b = s.data[p + 2];
            p += 3;
            continue;
        }
        if (c > 1) { err = "unknown mesh command " + std::to_string(c); return false; }
        if (c == 1) {
            if (!s.has(p, 7)) { err = "mesh header overrun"; return false; }
            ub = s.data[p];
            vb = s.data[p + 2];
            std::array<int, 3> key{s.data[p + 1], s.data[p + 3], s.data[p + 4]};
            auto it = std::find(model.cluts.begin(), model.cluts.end(), key);
            if (it == model.cluts.end()) { model.cluts.push_back(key); it = model.cluts.end() - 1; }
            clut = uint8_t(it - model.cluts.begin());
            haveHdr = true;
            p += 7;
        }
        if (!haveHdr) { err = "primitive before texture header"; return false; }
        ModelPrim pr;
        pr.count = st[8] ? 4 : 3;
        pr.lit = st[11] == 0;
        pr.blend = st[14];
        pr.clut = clut;
        pr.r = r; pr.g = g; pr.b = b;
        const size_t k = pr.count, nk = pr.lit ? k : 0;
        if (!s.has(p, 3 * k + nk)) { err = "primitive overrun"; return false; }
        for (size_t i = 0; i < k; ++i) {
            pr.v[i] = s.data[p + i];
            if (pr.v[i] >= nv) { err = "vertex index out of range"; return false; }
        }
        for (size_t i = 0; i < nk; ++i) {
            pr.n[i] = s.data[p + k + i];
            if (pr.n[i] >= nn) { err = "normal index out of range"; return false; }
        }
        for (size_t i = 0; i < k; ++i) {
            pr.u[i] = uint8_t(s.data[p + k + nk + 2 * i] + ub);
            pr.t[i] = uint8_t(s.data[p + k + nk + 2 * i + 1] + vb);
        }
        p += 3 * k + nk;
        m.prims.push_back(pr);
    }
    return true;
}

bool parseClips(ByteView s, std::vector<ModelClip>& clips) {
    uint32_t first;
    if (!s.u32(0, first) || first % 4 || first > s.size) return false;
    clips.resize(first / 4);
    for (size_t i = 0; i < clips.size(); ++i) {
        uint32_t o = 0;
        s.u32(i * 4, o);
        if (!o) continue;
        ModelClip c;
        for (int guard = 0; guard < 64 && s.has(o, 8); ++guard, o += 8) {
            uint16_t a, l, f, t;
            s.u16(o, a); s.u16(o + 2, l); s.u16(o + 4, f); s.u16(o + 6, t);
            if (l == 0) {
                c.length = a;
                c.loop = f == 0x8000;
                break;
            }
            ClipSeg seg{a, l, f, t, f == 0 && t == 0};
            c.segs.push_back(seg);
        }
        if (c.length > 0) clips[i] = c;
    }
    return true;
}

}  // namespace

Image Model::textureFor(size_t clutIndex) const {
    Image img;
    img.resize(kTexW, kTexH, 0);
    if (clutIndex >= cluts.size()) return img;
    const size_t page = size_t(cluts[clutIndex][0]);
    if (page >= pages.size()) {  // no own texture: white, so effect shapes still show
        std::fill(img.px.begin(), img.px.end(), rgba(255, 255, 255));
        return img;
    }
    const auto& vram = pages[page];
    const int cx = cluts[clutIndex][1], cy = cluts[clutIndex][2];
    uint32_t pal[16];
    for (int i = 0; i < 16; ++i) {
        size_t idx = size_t(cy & 255) * 64 + size_t(cx + i);
        uint16_t w = idx < vram.size() ? vram[idx] : 0;
        uint8_t r = uint8_t((w & 31) << 3), g = uint8_t(((w >> 5) & 31) << 3), b = uint8_t(((w >> 10) & 31) << 3);
        pal[i] = w == 0 ? 0 : rgba(uint8_t(r | r >> 5), uint8_t(g | g >> 5), uint8_t(b | b >> 5));
    }
    for (int y = 0; y < kTexH; ++y)
        for (int x = 0; x < kTexW; ++x) img.px[size_t(y) * kTexW + x] = pal[texel(page, x, y)];
    return img;
}

bool loadModel(ByteView f, Model& out, std::string& err) {
    out = {};
    Sections S;
    if (!splitSections(f, S, err)) return false;
    const size_t n = S.s.size();

    // Hierarchy: [textureSection, count] + count * (parent, meshSection, trackSection).
    int hier = -1;
    uint32_t texSec = 0, count = 0;
    for (size_t i = 0; i < n && hier < 0; ++i) {
        ByteView s = S.s[i];
        uint32_t t, c;
        if (!s.u32(0, t) || !s.u32(4, c) || t >= n || c == 0 || c > 128) continue;
        if (s.size != 8 + size_t(c) * 12 && s.size != 8 + size_t(c) * 12 - 4) continue;
        if (t != 0 && !isTexture(S.s[t])) continue;  // 0 = no own texture (effects)
        bool rowsOk = true;
        for (uint32_t k = 0; k < c && rowsOk; ++k) {
            uint32_t par = 0, ms = 0, ts = 0;
            s.u32(8 + k * 12, par); s.u32(12 + k * 12, ms);
            if (!s.u32(16 + k * 12, ts)) ts = ms;
            rowsOk = par <= c && ms < n && ts < n && ms != i && ts != i;
        }
        if (!rowsOk) continue;
        hier = int(i);
        texSec = t;
        count = c;
    }
    if (hier < 0) { err = "no hierarchy section (unsupported model layout)"; return false; }

    if (texSec != 0 && !loadPages(S.s[texSec], out.pages, err)) return false;

    ByteView hs = S.s[size_t(hier)];
    std::vector<std::array<uint32_t, 3>> rows(count);
    for (uint32_t k = 0; k < count; ++k)
        for (int j = 0; j < 3; ++j) {
            uint32_t v = 0;
            hs.u32(8 + k * 12 + size_t(j) * 4, v);
            rows[k][size_t(j)] = v;
        }
    // The very last triple can be cut short (its track index is implied).
    if (hs.size == 8 + size_t(count) * 12 - 4) rows[count - 1][2] = rows[count - 1][1] - (rows[0][1] - rows[0][2]);

    for (uint32_t k = 0; k < count; ++k) {
        const uint32_t parent = rows[k][0], meshSec = rows[k][1], trackSec = rows[k][2];
        if (parent > count || meshSec >= n || trackSec >= n) { err = "bad hierarchy entry"; return false; }
        ModelNode node;
        node.parent = int(parent) - 1;
        ModelTrack tr;
        if (!parseTrack(S.s[trackSec], tr, err)) { err = "track " + std::to_string(trackSec) + ": " + err; return false; }
        node.track = int(out.tracks.size());
        out.tracks.push_back(std::move(tr));
        if (S.s[meshSec].size) {
            ModelMesh mesh;
            if (!parseMesh(S.s[meshSec], mesh, out, err)) { err = "mesh " + std::to_string(meshSec) + ": " + err; return false; }
            node.mesh = int(out.meshes.size());
            out.meshes.push_back(std::move(mesh));
        }
        out.nodes.push_back(node);
    }
    if (!parseClips(S.s[0], out.clips)) { err = "bad clip table"; return false; }

    // Height of the idle pose.
    Pose pose;
    int f0 = 0;
    if (!out.clips.empty() && out.clips[0].valid())
        for (const auto& sg : out.clips[0].segs)
            if (!sg.blend) { f0 = sg.from; break; }
    samplePose(out, float(f0), pose);
    std::vector<Mat34> mats;
    poseMatrices(out, pose, mats);
    float minY = 0;
    for (size_t i = 0; i < out.nodes.size(); ++i) {
        if (out.nodes[i].mesh < 0) continue;
        for (const auto& v : out.meshes[size_t(out.nodes[i].mesh)].verts) {
            float x, y, z;
            xform(mats[i], v[0], v[1], v[2], x, y, z);
            minY = std::min(minY, y);
        }
    }
    out.height = -minY;
    return true;
}

// ---------------------------------------------------------------------------------------------
namespace {
void sampleKeys(const std::vector<ModelKey>& k, float t, float& x, float& y, float& z, float def) {
    if (k.empty()) { x = y = z = def; return; }
    if (t <= k.front().t) { x = k.front().x; y = k.front().y; z = k.front().z; return; }
    for (size_t i = 1; i < k.size(); ++i) {
        if (t <= k[i].t) {
            const ModelKey& a = k[i - 1];
            const ModelKey& b = k[i];
            float f = b.t == a.t ? 1.f : (t - a.t) / float(b.t - a.t);
            x = a.x + (b.x - a.x) * f;
            y = a.y + (b.y - a.y) * f;
            z = a.z + (b.z - a.z) * f;
            return;
        }
    }
    x = k.back().x; y = k.back().y; z = k.back().z;
}
}  // namespace

void samplePose(const Model& m, float frame, Pose& out) {
    out.resize(m.nodes.size());
    for (size_t i = 0; i < m.nodes.size(); ++i) {
        const ModelTrack& t = m.tracks[size_t(m.nodes[i].track)];
        NodePose& p = out[i];
        sampleKeys(t.pos, frame, p.px, p.py, p.pz, 0);
        sampleKeys(t.rot, frame, p.rx, p.ry, p.rz, 0);
        sampleKeys(t.scl, frame, p.sx, p.sy, p.sz, 4096);
    }
}

void lerpPose(const Pose& a, const Pose& b, float f, Pose& out) {
    out.resize(a.size());
    auto ang = [f](float x, float y) {
        float d = std::fmod(y - x, 4096.f);
        if (d > 2048) d -= 4096;
        if (d < -2048) d += 4096;
        return x + d * f;
    };
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
        const NodePose &p = a[i], &q = b[i];
        out[i] = {p.px + (q.px - p.px) * f, p.py + (q.py - p.py) * f, p.pz + (q.pz - p.pz) * f,
                  ang(p.rx, q.rx), ang(p.ry, q.ry), ang(p.rz, q.rz),
                  p.sx + (q.sx - p.sx) * f, p.sy + (q.sy - p.sy) * f, p.sz + (q.sz - p.sz) * f};
    }
}

void poseMatrices(const Model& m, const Pose& pose, std::vector<Mat34>& out) {
    out.assign(m.nodes.size(), Mat34{});
    std::vector<char> done(m.nodes.size(), 0);
    constexpr float k = 6.28318530718f / 4096.f;
    std::function<void(size_t)> build = [&](size_t i) {
        if (done[i]) return;
        done[i] = 1;
        const NodePose& p = pose[i];
        const float cx = std::cos(p.rx * k), sx = std::sin(p.rx * k);
        const float cy = std::cos(p.ry * k), sy = std::sin(p.ry * k);
        const float cz = std::cos(p.rz * k), sz = std::sin(p.rz * k);
        // R = Rx * Ry * Rz
        float R[3][3] = {
            {cy * cz, -cy * sz, sy},
            {sx * sy * cz + cx * sz, -sx * sy * sz + cx * cz, -sx * cy},
            {-cx * sy * cz + sx * sz, cx * sy * sz + sx * cz, cx * cy},
        };
        const float S[3] = {p.sx / 4096.f, p.sy / 4096.f, p.sz / 4096.f};
        Mat34 L;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) L.m[r][c] = R[r][c] * S[c];
        L.t[0] = p.px; L.t[1] = p.py; L.t[2] = p.pz;
        const int par = m.nodes[i].parent;
        if (par < 0 || size_t(par) >= m.nodes.size() || size_t(par) == i) {
            out[i] = L;
            return;
        }
        build(size_t(par));
        const Mat34& P = out[size_t(par)];
        Mat34 W;
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) W.m[r][c] = P.m[r][0] * L.m[0][c] + P.m[r][1] * L.m[1][c] + P.m[r][2] * L.m[2][c];
            W.t[r] = P.m[r][0] * L.t[0] + P.m[r][1] * L.t[1] + P.m[r][2] * L.t[2] + P.t[r];
        }
        out[i] = W;
    };
    for (size_t i = 0; i < m.nodes.size(); ++i) build(i);
}

// ---------------------------------------------------------------------------------------------
static int firstFrame(const ModelClip& c) {
    for (const auto& s : c.segs)
        if (!s.blend) return s.from;
    return 0;
}

void ClipPlayer::play(const Model& m, int clip) {
    if (clip < 0 || size_t(clip) >= m.clips.size() || !m.clips[size_t(clip)].valid()) clip = 0;
    if (m.clips.empty() || !m.clips[size_t(clip)].valid()) return;
    if (cur_.size() != m.nodes.size()) samplePose(m, float(firstFrame(m.clips[size_t(clip)])), cur_);
    clip_ = clip;
    t_ = 0;
    seg_ = -1;
    done_ = false;
    evaluate(m);
}

void ClipPlayer::update(const Model& m, float frames) {
    if (clip_ < 0 || done_) return;
    const ModelClip& c = m.clips[size_t(clip_)];
    t_ += frames;
    if (t_ >= float(c.length)) {
        if (c.loop && c.length > 0) {
            t_ = std::fmod(t_, float(c.length));
            seg_ = -1;
        } else {
            t_ = float(c.length);
            done_ = true;
        }
    }
    evaluate(m);
}

void ClipPlayer::evaluate(const Model& m) {
    const ModelClip& c = m.clips[size_t(clip_)];
    int s = int(c.segs.size()) - 1;
    for (size_t i = 0; i < c.segs.size(); ++i)
        if (t_ < float(c.segs[i].start + c.segs[i].len)) { s = int(i); break; }
    const ClipSeg& sg = c.segs[size_t(s)];
    if (s != seg_) {
        seg_ = s;
        if (sg.blend) {
            from_ = cur_;
            int next = -1;
            for (size_t j = size_t(s) + 1; j < c.segs.size(); ++j)
                if (!c.segs[j].blend) { next = c.segs[j].from; break; }
            if (next < 0) next = c.loop ? firstFrame(c) : (m.clips[0].valid() ? firstFrame(m.clips[0]) : 0);
            samplePose(m, float(next), target_);
        }
    }
    const float local = std::clamp(t_ - float(sg.start), 0.f, float(sg.len));
    if (sg.blend) {
        srcFrame_ = -1;
        lerpPose(from_, target_, sg.len > 0 ? local / float(sg.len) : 1.f, cur_);
    } else {
        float fr = std::min(float(sg.from) + local, float(sg.to));
        srcFrame_ = int(fr);
        samplePose(m, fr, cur_);
    }
}

}  // namespace dw3
