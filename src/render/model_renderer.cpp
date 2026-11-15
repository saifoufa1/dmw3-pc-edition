#include "render/model_renderer.h"

#include <windows.h>
#include <GL/gl.h>

#include <algorithm>
#include <cmath>

namespace dw3 {

void uploadModel(const Model& m, ModelGpu& gpu) {
    destroyModel(gpu);
    for (size_t c = 0; c < m.cluts.size(); ++c) gpu.tex.push_back(gfx::upload(m.textureFor(c)));
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
    // Pass 1: shaded + textured. Pass 2 (flash only): additive white.
    for (int pass = 0; pass < (v.flash > 0.f ? 2 : 1); ++pass) {
        if (pass == 1) {
            glDisable(GL_TEXTURE_2D);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            glDepthMask(GL_FALSE);
        }
        int boundTex = -1;
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
                if (pass == 0 && !v.silhouette && int(pr.clut) != boundTex && pr.clut < gpu.tex.size()) {
                    glBindTexture(GL_TEXTURE_2D, gpu.tex[pr.clut].id);
                    boundTex = pr.clut;
                }
                static const int kTri[2][3] = {{0, 1, 2}, {1, 3, 2}};
                glBegin(GL_TRIANGLES);
                for (int t = 0; t < (pr.count == 4 ? 2 : 1); ++t)
                    for (int c = 0; c < 3; ++c) {
                        const int k = kTri[t][c];
                        if (pass == 1) {
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
                            glColor4f(std::min(1.f, cr * tr), std::min(1.f, cg * tg), std::min(1.f, cb * tb), ta);
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
    std::filesystem::path file;
    std::error_code ec;
    for (auto& e : std::filesystem::directory_iterator(root_ / "DAT" / "MODEL", ec))
        if (e.path().filename().string().rfind(id, 0) == 0) { file = e.path(); break; }
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
    uploadModel(gm->model, gm->gpu);
    cache_[id] = gm;
    return gm;
}

}  // namespace dw3
