#include "render/model_renderer.h"

#include "render/camera3d.h"

#include <windows.h>
#include <GL/gl.h>

#include <algorithm>
#include <cmath>

namespace dw3 {

namespace {
void toHsv(float r, float g, float b, float& h, float& s, float& v) {
    const float mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b)), d = mx - mn;
    v = mx;
    s = mx > 0 ? d / mx : 0;
    if (d <= 1e-5f) { h = 0; return; }
    if (mx == r) h = 60.f * std::fmod((g - b) / d + 6.f, 6.f);
    else if (mx == g) h = 60.f * ((b - r) / d + 2.f);
    else h = 60.f * ((r - g) / d + 4.f);
}
void fromHsv(float h, float s, float v, float& r, float& g, float& b) {
    const float c = v * s, x = c * (1 - std::abs(std::fmod(h / 60.f, 2.f) - 1)), m = v - c;
    const int i = int(h / 60.f) % 6;
    const float rr[6] = {c, x, 0, 0, x, c}, gg[6] = {x, c, c, x, 0, 0}, bb[6] = {0, 0, x, c, c, x};
    r = rr[i] + m;
    g = gg[i] + m;
    b = bb[i] + m;
}
}  // namespace

void recolorImage(Image& img, Recolor rc) {
    if (rc == Recolor::None) return;
    for (uint32_t& p : img.px) {
        if ((p >> 24) == 0) continue;
        float r = float(p & 255) / 255.f, g = float((p >> 8) & 255) / 255.f, b = float((p >> 16) & 255) / 255.f;
        float h, s, v;
        toHsv(r, g, b, h, s, v);
        const bool skin = s > 0.25f && h >= 15.f && h <= 72.f;   // Agumon's yellow / orange hide
        const bool eye = s > 0.25f && h > 80.f && h < 170.f;     // green eyes
        if (rc == Recolor::Black) {
            if (skin) {  // charcoal grey, keeping the original shading
                r = g = 0.10f + 0.42f * v;
                b = 0.13f + 0.45f * v;
            } else if (eye) {
                fromHsv(42.f, std::max(0.6f, s), std::max(0.7f, v), r, g, b);  // yellow eyes
            }
        } else if (rc == Recolor::Blue) {
            if (skin) fromHsv(222.f, std::min(1.f, s * 0.8f + 0.12f), std::min(1.f, v * 0.92f), r, g, b);
            else if (eye) fromHsv(0.f, std::max(0.6f, s), std::max(0.7f, v), r, g, b);  // red eyes
        }
        p = rgba(uint8_t(std::clamp(r, 0.f, 1.f) * 255), uint8_t(std::clamp(g, 0.f, 1.f) * 255),
                 uint8_t(std::clamp(b, 0.f, 1.f) * 255), uint8_t(p >> 24));
    }
}

void uploadModel(const Model& m, ModelGpu& gpu, Recolor recolor) {
    destroyModel(gpu);
    for (size_t c = 0; c < m.cluts.size(); ++c) {
        Image img = m.textureFor(c);
        recolorImage(img, recolor);
        gpu.tex.push_back(gfx::upload(img));
    }
}

void destroyModel(ModelGpu& gpu) {
    for (auto& t : gpu.tex) gfx::destroy(t);
    gpu.tex.clear();
}

void drawModel(const Model& m, const ModelGpu& gpu, const Pose& pose, const ModelView& v) {
    if (pose.size() != m.nodes.size()) return;
    std::vector<Mat34> mats;
    poseMatrices(m, pose, mats);

    const float cyaw = std::cos(v.yaw), syaw = std::sin(v.yaw);
    const float cp = std::cos(v.pitch), sp = std::sin(v.pitch);
    const float s = v.pxPerUnit;
    // Light from the upper left, a little toward the camera (view space, y down, -z = toward camera).
    float lx = -0.45f, ly = -0.75f, lz = -0.5f;
    const float ll = std::sqrt(lx * lx + ly * ly + lz * lz);
    lx /= ll; ly /= ll; lz /= ll;
    const float ta = float(v.tint >> 24) / 255.f;
    const float tr = float(v.tint & 255) / 255.f, tg = float((v.tint >> 8) & 255) / 255.f, tb = float((v.tint >> 16) & 255) / 255.f;

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, kScreenW, kScreenH, 0, -100000, 100000);
    glMatrixMode(GL_MODELVIEW);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5f);
    if (v.silhouette) glDisable(GL_TEXTURE_2D);
    else glEnable(GL_TEXTURE_2D);

    struct V { float x, y, z; };
    std::vector<V> pv;
    std::vector<V> nv;
    // Pass 0: opaque prims. Pass 1: PS1 semi-transparent prims (additive fire etc.), no depth
    // writes. Pass 2 (flash only): additive white over everything.
    for (int pass = 0; pass < (v.flash > 0.f ? 3 : 2); ++pass) {
        if (pass == 1) {
            glDepthMask(GL_FALSE);
            glDisable(GL_ALPHA_TEST);
        }
        if (pass == 2) {
            glDisable(GL_TEXTURE_2D);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            glEnable(GL_ALPHA_TEST);
        }
        int boundTex = -1, boundBlend = -1;
        for (size_t i = 0; i < m.nodes.size(); ++i) {
            if (m.nodes[i].mesh < 0) continue;
            const ModelMesh& mesh = m.meshes[size_t(m.nodes[i].mesh)];
            const Mat34& M = mats[i];
            pv.resize(mesh.verts.size());
            for (size_t k = 0; k < mesh.verts.size(); ++k) {
                float x, y, z;
                xform(M, mesh.verts[k][0], mesh.verts[k][1], mesh.verts[k][2], x, y, z);
                const float rx = x * cyaw + z * syaw, rz = -x * syaw + z * cyaw;
                const float py = y * cp - rz * sp, pz = y * sp + rz * cp;
                pv[k] = {v.x + rx * s, v.y + py * s, pz * s};
            }
            nv.resize(mesh.norms.size());
            for (size_t k = 0; k < mesh.norms.size(); ++k) {
                const auto& n = mesh.norms[k];
                float x = M.m[0][0] * n[0] + M.m[0][1] * n[1] + M.m[0][2] * n[2];
                float y = M.m[1][0] * n[0] + M.m[1][1] * n[1] + M.m[1][2] * n[2];
                float z = M.m[2][0] * n[0] + M.m[2][1] * n[1] + M.m[2][2] * n[2];
                const float rx = x * cyaw + z * syaw, rz = -x * syaw + z * cyaw;
                const float py = y * cp - rz * sp, pz = y * sp + rz * cp;
                const float l = std::max(1e-3f, std::sqrt(rx * rx + py * py + pz * pz));
                nv[k] = {rx / l, py / l, pz / l};
            }
            for (const ModelPrim& pr : mesh.prims) {
                const bool semi = pr.blend != 0 && !v.silhouette;
                if ((pass == 0 && semi) || (pass == 1 && !semi)) continue;
                if (pass == 1 && pr.blend != boundBlend) {
                    boundBlend = pr.blend;
                    if (pr.blend == 1) glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);       // average
                    else if (pr.blend == 3) glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_COLOR);       // subtract (approx.)
                    else glBlendFunc(GL_SRC_ALPHA, GL_ONE);                                      // add / add 1/4
                }
                if (pass < 2 && !v.silhouette && int(pr.clut) != boundTex && pr.clut < gpu.tex.size()) {
                    glBindTexture(GL_TEXTURE_2D, gpu.tex[pr.clut].id);
                    boundTex = pr.clut;
                }
                static const int kTri[2][3] = {{0, 1, 2}, {1, 3, 2}};
                glBegin(GL_TRIANGLES);
                for (int t = 0; t < (pr.count == 4 ? 2 : 1); ++t)
                    for (int c = 0; c < 3; ++c) {
                        const int k = kTri[t][c];
                        if (pass == 2) {
                            glColor4f(1, 1, 1, v.flash * ta);
                        } else if (v.silhouette) {
                            glColor4f(tr, tg, tb, ta);
                        } else {
                            float li;
                            if (pr.lit) {
                                const V& n = nv[pr.n[k]];
                                li = 0.62f + 0.55f * std::max(0.f, -(n.x * lx + n.y * ly + n.z * lz));
                            } else {
                                li = 1.f;
                            }
                            float cr = li * (pr.lit ? 1.f : pr.r / 128.f), cg = li * (pr.lit ? 1.f : pr.g / 128.f),
                                  cb = li * (pr.lit ? 1.f : pr.b / 128.f);
                            float a = ta;
                            if (pass == 1) a *= pr.blend == 1 ? 0.5f : pr.blend == 4 ? 0.25f : 1.f;
                            glColor4f(std::min(1.f, cr * tr), std::min(1.f, cg * tg), std::min(1.f, cb * tb), a);
                            glTexCoord2f((pr.u[k] + 0.25f) / 256.f, (pr.t[k] + 0.25f) / 256.f);
                        }
                        const V& p = pv[pr.v[k]];
                        glVertex3f(p.x, p.y, -p.z);
                    }
                glEnd();
            }
        }
    }
    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
    glColor4f(1, 1, 1, 1);
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

void drawModelWorld(const Model& m, const ModelGpu& gpu, const Pose& pose, const Camera3D& cam, const WorldPlace& p) {
    if (pose.size() != m.nodes.size()) return;
    std::vector<Mat34> mats;
    poseMatrices(m, pose, mats);
    const float cyaw = std::cos(p.yaw), syaw = std::sin(p.yaw), s = p.scale;
    // Same light as the 2D-era view: upper left, toward the camera, in view space (y down).
    float lx = -0.45f, ly = -0.75f, lz = -0.5f;
    const float ll = std::sqrt(lx * lx + ly * ly + lz * lz);
    lx /= ll; ly /= ll; lz /= ll;
    const float ta = float(p.tint >> 24) / 255.f;
    const float tr = float(p.tint & 255) / 255.f, tg = float((p.tint >> 8) & 255) / 255.f, tb = float((p.tint >> 16) & 255) / 255.f;

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5f);
    if (p.silhouette) glDisable(GL_TEXTURE_2D);
    else glEnable(GL_TEXTURE_2D);

    struct V { float x, y, z; };
    std::vector<V> pv, nv;
    const int lastPass = p.flash > 0.f ? 2 : 1;
    for (int pass = 0; pass <= lastPass; ++pass) {
        if (pass < 2 && !(p.passes & (1 << pass))) continue;
        if (pass == 0) { glDepthMask(GL_TRUE); glEnable(GL_ALPHA_TEST); }
        if (pass == 1) { glDepthMask(GL_FALSE); glDisable(GL_ALPHA_TEST); }
        if (pass == 2) {
            glDepthMask(GL_FALSE);
            glDisable(GL_TEXTURE_2D);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            glEnable(GL_ALPHA_TEST);
        }
        int boundTex = -1, boundBlend = -1;
        for (size_t i = 0; i < m.nodes.size(); ++i) {
            if (m.nodes[i].mesh < 0) continue;
            const ModelMesh& mesh = m.meshes[size_t(m.nodes[i].mesh)];
            const Mat34& M = mats[i];
            pv.resize(mesh.verts.size());
            for (size_t k = 0; k < mesh.verts.size(); ++k) {
                float x, y, z;
                xform(M, mesh.verts[k][0], mesh.verts[k][1], mesh.verts[k][2], x, y, z);
                pv[k] = {p.x + (x * cyaw + z * syaw) * s, p.y + y * s, p.z + (-x * syaw + z * cyaw) * s};
            }
            nv.resize(mesh.norms.size());
            if (p.shade)
                for (size_t k = 0; k < mesh.norms.size(); ++k) {
                    const auto& n = mesh.norms[k];
                    const float x = M.m[0][0] * n[0] + M.m[0][1] * n[1] + M.m[0][2] * n[2];
                    const float y = M.m[1][0] * n[0] + M.m[1][1] * n[1] + M.m[1][2] * n[2];
                    const float z = M.m[2][0] * n[0] + M.m[2][1] * n[1] + M.m[2][2] * n[2];
                    const float wx = x * cyaw + z * syaw, wz = -x * syaw + z * cyaw;
                    // world -> view space (right, down, forward)
                    const float vx = wx * cam.right[0] + y * cam.right[1] + wz * cam.right[2];
                    const float vy = wx * cam.down[0] + y * cam.down[1] + wz * cam.down[2];
                    const float vz = wx * cam.fwd[0] + y * cam.fwd[1] + wz * cam.fwd[2];
                    const float l = std::max(1e-3f, std::sqrt(vx * vx + vy * vy + vz * vz));
                    nv[k] = {vx / l, vy / l, vz / l};
                }
            for (const ModelPrim& pr : mesh.prims) {
                const bool semi = pr.blend != 0 && !p.silhouette;
                if ((pass == 0 && semi) || (pass == 1 && !semi)) continue;
                if (pass == 1 && pr.blend != boundBlend) {
                    boundBlend = pr.blend;
                    if (pr.blend == 1) glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);  // average
                    else if (pr.blend == 3) glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_COLOR);  // subtract (approx.)
                    else glBlendFunc(GL_SRC_ALPHA, GL_ONE);                                 // add / add 1/4
                }
                if (pass < 2 && !p.silhouette && int(pr.clut) != boundTex && pr.clut < gpu.tex.size()) {
                    glBindTexture(GL_TEXTURE_2D, gpu.tex[pr.clut].id);
                    boundTex = pr.clut;
                }
                static const int kTri[2][3] = {{0, 1, 2}, {1, 3, 2}};
                glBegin(GL_TRIANGLES);
                for (int t = 0; t < (pr.count == 4 ? 2 : 1); ++t)
                    for (int c = 0; c < 3; ++c) {
                        const int k = kTri[t][c];
                        if (pass == 2) {
                            glColor4f(1, 1, 1, p.flash * ta);
                        } else if (p.silhouette) {
                            glColor4f(tr, tg, tb, ta);
                        } else {
                            float li = 1.f;
                            if (pr.lit && p.shade && pr.n[k] < nv.size()) {
                                const V& n = nv[pr.n[k]];
                                li = 0.62f + 0.55f * std::max(0.f, -(n.x * lx + n.y * ly + n.z * lz));
                            }
                            const float cr = li * (pr.lit ? 1.f : pr.r / 128.f), cg = li * (pr.lit ? 1.f : pr.g / 128.f),
                                        cb = li * (pr.lit ? 1.f : pr.b / 128.f);
                            float a = ta;
                            if (pass == 1) a *= pr.blend == 1 ? 0.5f : pr.blend == 4 ? 0.25f : 1.f;
                            glColor4f(std::min(1.f, cr * tr), std::min(1.f, cg * tg), std::min(1.f, cb * tb), a);
                            glTexCoord2f((pr.u[k] + 0.25f) / 256.f, (pr.t[k] + 0.25f) / 256.f);
                        }
                        const V& v = pv[pr.v[k]];
                        glVertex3f(v.x, v.y, v.z);
                    }
                glEnd();
            }
        }
    }
    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_ALPHA_TEST);
    glEnable(GL_TEXTURE_2D);
    glColor4f(1, 1, 1, 1);
}

void drawGroundShadow3D(float x, float y, float z, float radius, float alpha) {
    glDisable(GL_TEXTURE_2D);
    glDepthMask(GL_FALSE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-2.f, -4.f);
    glBegin(GL_TRIANGLE_FAN);
    glColor4f(0, 0, 0, alpha);
    glVertex3f(x, y - 4.f, z);
    glColor4f(0, 0, 0, 0);
    for (int i = 0; i <= 24; ++i) {
        const float a = float(i) / 24.f * 6.2831853f;
        glVertex3f(x + std::cos(a) * radius, y - 4.f, z + std::sin(a) * radius);
    }
    glEnd();
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDepthMask(GL_TRUE);
    glEnable(GL_TEXTURE_2D);
    glColor4f(1, 1, 1, 1);
}

void drawGroundShadow(float cx, float cy, float rx, float ry, float alpha) {
    glDisable(GL_TEXTURE_2D);
    glBegin(GL_TRIANGLE_FAN);
    glColor4f(0, 0, 0, alpha);
    glVertex2f(cx, cy);
    glColor4f(0, 0, 0, 0);
    for (int i = 0; i <= 24; ++i) {
        float a = float(i) / 24.f * 6.2831853f;
        glVertex2f(cx + std::cos(a) * rx, cy + std::sin(a) * ry);
    }
    glEnd();
    glEnable(GL_TEXTURE_2D);
    glColor4f(1, 1, 1, 1);
}

std::shared_ptr<GameModel> ModelCache::get(const std::string& id, std::string* err) {
    auto it = cache_.find(id);
    if (it != cache_.end()) return it->second;
    // "M005GREY" -> DAT/MODEL, "EFFECT/MEFT0331" -> DAT/EFFECT.
    std::filesystem::path dir = root_ / "DAT" / "MODEL";
    std::string prefix = id;
    Recolor recolor = Recolor::None;
    if (auto colon = prefix.find(':'); colon != std::string::npos) {
        const std::string rc = prefix.substr(colon + 1);
        recolor = rc == "black" ? Recolor::Black : rc == "blue" ? Recolor::Blue : Recolor::None;
        prefix = prefix.substr(0, colon);
    }
    if (auto slash = prefix.find('/'); slash != std::string::npos) {
        dir = root_ / "DAT" / prefix.substr(0, slash);
        prefix = prefix.substr(slash + 1);
    }
    std::filesystem::path file;
    std::error_code ec;
    for (auto& e : std::filesystem::directory_iterator(dir, ec))
        if (e.path().filename().string().rfind(prefix, 0) == 0) { file = e.path(); break; }
    Bytes b;
    if (file.empty() || !readFile(file, b)) {
        if (err) *err = "model not found: " + id;
        cache_[id] = nullptr;
        return nullptr;
    }
    auto gm = std::make_shared<GameModel>();
    std::string e;
    if (!loadModel(b, gm->model, e)) {
        if (err) *err = id + ": " + e;
        cache_[id] = nullptr;
        return nullptr;
    }
    gm->file = file.filename().string();
    uploadModel(gm->model, gm->gpu, recolor);
    cache_[id] = gm;
    return gm;
}

}  // namespace dw3
