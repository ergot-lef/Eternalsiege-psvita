// Eternal Siege Vita - rendu (vitaGL, pipeline fixe)
#include "game.h"
#include <vitaGL.h>

unsigned g_tex[TEX_COUNT];
Batch g_batches[BA_COUNT];
Camera g_cam;

static const int s_batchTex[BA_COUNT] = {
    TEX_STONE, TEX_ROCK, TEX_BARK, TEX_WOOD, TEX_PLANKS, TEX_METAL, TEX_CLOTH, TEX_LEATHER,
    TEX_SKIN, TEX_STRAW, TEX_WHITE, TEX_LEAVES, TEX_GRASSBLADE
};

void gfxInit() {
    vglInitExtended(0, 960, 544, 24 * 1024 * 1024, SCE_GXM_MULTISAMPLE_2X);
    glViewport(0, 0, 960, 544);
    glClearColor(0.5f, 0.6f, 0.7f, 1);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glShadeModel(GL_SMOOTH);
    for (int i = 0; i < BA_COUNT; i++) {
        int blend = BM_OPAQUE;
        if (i == BA_LEAVES || i == BA_GRASS) blend = BM_ALPHATEST;
        g_batches[i].init(s_batchTex[i], blend, true);
    }
    logf("gfx ok");
}

void gfxBeginFrame() {
    glDepthMask(GL_TRUE);
    glClearColor(g_weather.fogR, g_weather.fogG, g_weather.fogB, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void gfxEndFrame() {
    vglSwapBuffers(GL_FALSE);
}

unsigned texUpload(int w, int h, const uint8_t* rgba, bool mip, bool repeat) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (mip) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glGenerateMipmap(GL_TEXTURE_2D);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    return t;
}

static void setBlend(int blend) {
    switch (blend) {
    case BM_OPAQUE:
        glDisable(GL_BLEND); glDisable(GL_ALPHA_TEST); glDepthMask(GL_TRUE); break;
    case BM_ALPHATEST:
        glDisable(GL_BLEND); glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_GREATER, 0.45f); glDepthMask(GL_TRUE); break;
    case BM_ALPHA:
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_ALPHA_TEST); glDepthMask(GL_FALSE); break;
    case BM_ADD:
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDisable(GL_ALPHA_TEST); glDepthMask(GL_FALSE); break;
    }
}

static void drawArrays(const Vtx* v, int n, int tex, int blend, bool lit) {
    if (n <= 0) return;
    setBlend(blend);
    if (lit) glEnable(GL_LIGHTING); else glDisable(GL_LIGHTING);
    glBindTexture(GL_TEXTURE_2D, g_tex[tex]);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    if (lit) glEnableClientState(GL_NORMAL_ARRAY); else glDisableClientState(GL_NORMAL_ARRAY);
    glVertexPointer(3, GL_FLOAT, sizeof(Vtx), &v->x);
    glTexCoordPointer(2, GL_FLOAT, sizeof(Vtx), &v->u);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vtx), &v->r);
    if (lit) glNormalPointer(GL_FLOAT, sizeof(Vtx), &v->nx);
    glDrawArrays(GL_TRIANGLES, 0, n);
}

void batchFlush(Batch& b) {
    // découpe en paquets raisonnables
    const int CH = 30000;
    int n = (int)b.v.size();
    for (int s = 0; s < n; s += CH) {
        int c = n - s < CH ? n - s : CH;
        drawArrays(&b.v[s], c, b.tex, b.blend, b.lit);
    }
    b.clear();
}

void batchesFlushOpaque() {
    for (int i = 0; i < BA_COUNT; i++)
        if (g_batches[i].blend == BM_OPAQUE) batchFlush(g_batches[i]);
    glDisable(GL_CULL_FACE);
    for (int i = 0; i < BA_COUNT; i++)
        if (g_batches[i].blend == BM_ALPHATEST) batchFlush(g_batches[i]);
    glEnable(GL_CULL_FACE);
    setBlend(BM_OPAQUE);
}

void batchesFlushAlpha() {
    for (int i = 0; i < BA_COUNT; i++)
        if (g_batches[i].blend >= BM_ALPHA) batchFlush(g_batches[i]);
    setBlend(BM_OPAQUE);
}

// ---------------------------------------------------------------- Émission
static inline Vtx mkv(const vec3& p, float u, float v, const vec3& n, Color c) {
    Vtx o;
    o.x = p.x; o.y = p.y; o.z = p.z; o.u = u; o.v = v;
    o.nx = n.x; o.ny = n.y; o.nz = n.z;
    o.r = c.r; o.g = c.g; o.b = c.b; o.a = c.a;
    return o;
}

void emitTri(Batch& b, const Vtx& a, const Vtx& c, const Vtx& d) {
    b.v.push_back(a); b.v.push_back(c); b.v.push_back(d);
}

void emitQuad(Batch& b, const vec3& p0, const vec3& p1, const vec3& p2, const vec3& p3,
              const vec3& n, Color col, float u0, float v0, float u1, float v1) {
    Vtx a = mkv(p0, u0, v0, n, col), c = mkv(p1, u1, v0, n, col);
    Vtx d = mkv(p2, u1, v1, n, col), e = mkv(p3, u0, v1, n, col);
    b.v.push_back(a); b.v.push_back(c); b.v.push_back(d);
    b.v.push_back(a); b.v.push_back(d); b.v.push_back(e);
}

void emitBox(Batch& b, const Mat4& m, const vec3& h, Color col, float uvs) {
    vec3 c[8];
    for (int i = 0; i < 8; i++)
        c[i] = m.point(vec3((i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z));
    vec3 X = normalize(m.dir(vec3(1, 0, 0))), Y = normalize(m.dir(vec3(0, 1, 0))), Z = normalize(m.dir(vec3(0, 0, 1)));
    float sx = h.x * 2 * uvs, sy = h.y * 2 * uvs, sz = h.z * 2 * uvs;
    // +X, -X, +Y, -Y, +Z, -Z (ordre CCW vu de l'extérieur)
    emitQuad(b, c[3], c[7], c[5], c[1], X, col, 0, 0, sz, sy);
    emitQuad(b, c[6], c[2], c[0], c[4], -X, col, 0, 0, sz, sy);
    emitQuad(b, c[6], c[7], c[3], c[2], Y, col, 0, 0, sx, sz);
    emitQuad(b, c[0], c[1], c[5], c[4], -Y, scaleC(col, 0.8f), 0, 0, sx, sz);
    emitQuad(b, c[7], c[6], c[4], c[5], Z, col, 0, 0, sx, sy);
    emitQuad(b, c[2], c[3], c[1], c[0], -Z, col, 0, 0, sx, sy);
}

void emitCylinder(Batch& b, const Mat4& m, float r, float h, int seg, Color col, bool caps, float us, float vs) {
    // axe local Y, base à y=0
    vec3 prevB, prevT, prevN;
    for (int i = 0; i <= seg; i++) {
        float a = (float)i / seg * 2 * PI;
        vec3 ln(cosf(a), 0, sinf(a));
        vec3 pb = m.point(vec3(ln.x * r, 0, ln.z * r));
        vec3 pt = m.point(vec3(ln.x * r, h, ln.z * r));
        vec3 n = normalize(m.dir(ln));
        if (i > 0) {
            float u0 = (float)(i - 1) / seg * us, u1 = (float)i / seg * us;
            Vtx a0 = mkv(prevB, u0, h * vs, prevN, col), a1 = mkv(pb, u1, h * vs, n, col);
            Vtx a2 = mkv(pt, u1, 0, n, col), a3 = mkv(prevT, u0, 0, prevN, col);
            b.v.push_back(a0); b.v.push_back(a3); b.v.push_back(a2);
            b.v.push_back(a0); b.v.push_back(a2); b.v.push_back(a1);
            if (caps) {
                vec3 up = normalize(m.dir(vec3(0, 1, 0)));
                vec3 ct = m.point(vec3(0, h, 0)), cb = m.point(vec3(0, 0, 0));
                Color cc = scaleC(col, 0.9f);
                b.v.push_back(mkv(ct, 0.5f, 0.5f, up, cc));
                b.v.push_back(mkv(pt, 0.5f + ln.x * 0.5f, 0.5f + ln.z * 0.5f, up, cc));
                b.v.push_back(mkv(prevT, 0.5f, 0, up, cc));
                b.v.push_back(mkv(cb, 0.5f, 0.5f, -up, cc));
                b.v.push_back(mkv(prevB, 0.5f, 0, -up, cc));
                b.v.push_back(mkv(pb, 0.5f + ln.x * 0.5f, 0.5f + ln.z * 0.5f, -up, cc));
            }
        }
        prevB = pb; prevT = pt; prevN = n;
    }
}

void emitCone(Batch& b, const Mat4& m, float r, float h, int seg, Color col) {
    vec3 tip = m.point(vec3(0, h, 0));
    for (int i = 0; i < seg; i++) {
        float a0 = (float)i / seg * 2 * PI, a1 = (float)(i + 1) / seg * 2 * PI;
        vec3 l0(cosf(a0), 0, sinf(a0)), l1(cosf(a1), 0, sinf(a1));
        vec3 p0 = m.point(l0 * r), p1 = m.point(l1 * r);
        vec3 n0 = normalize(m.dir(vec3(l0.x, r / h, l0.z))), n1 = normalize(m.dir(vec3(l1.x, r / h, l1.z)));
        b.v.push_back(mkv(p0, (float)i / seg * 3, 1, n0, col));
        b.v.push_back(mkv(tip, ((float)i + 0.5f) / seg * 3, 0, normalize(n0 + n1), col));
        b.v.push_back(mkv(p1, (float)(i + 1) / seg * 3, 1, n1, col));
    }
}

void emitBillboard(Batch& b, const vec3& p, float s, float rot, Color col, const vec3& right, const vec3& up) {
    float c = cosf(rot) * s, sn = sinf(rot) * s;
    vec3 R = right * c + up * sn, U = up * c - right * sn;
    vec3 n(0, 1, 0);
    emitQuad(b, p - R - U, p + R - U, p + R + U, p - R + U, n, col, 0, 1, 1, 0);
}

// ---------------------------------------------------------------- Mesh statique
void StaticMesh::upload() {
    count = (int)cpu.size();
    if (!count) return;
    if (!vbo) glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, count * sizeof(Vtx), cpu.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void StaticMesh::draw(int tex, int blend, bool lit) {
    if (!count || !vbo) return;
    setBlend(blend);
    if (lit) glEnable(GL_LIGHTING); else glDisable(GL_LIGHTING);
    glBindTexture(GL_TEXTURE_2D, g_tex[tex]);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    if (lit) glEnableClientState(GL_NORMAL_ARRAY); else glDisableClientState(GL_NORMAL_ARRAY);
    glVertexPointer(3, GL_FLOAT, sizeof(Vtx), (void*)0);
    glTexCoordPointer(2, GL_FLOAT, sizeof(Vtx), (void*)(3 * sizeof(float)));
    if (lit) glNormalPointer(GL_FLOAT, sizeof(Vtx), (void*)(5 * sizeof(float)));
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vtx), (void*)(8 * sizeof(float)));
    glDrawArrays(GL_TRIANGLES, 0, count);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void StaticMesh::release() {
    if (vbo) { GLuint b = vbo; glDeleteBuffers(1, &b); vbo = 0; }
    count = 0;
}

// ---------------------------------------------------------------- Caméra
static float s_frustum[6][4];

void cameraApply() {
    Camera& c = g_cam;
    c.proj = Mat4::perspective(c.fov, 960.0f / 544.0f, 0.08f, 260.0f);
    c.view = Mat4::viewFPS(c.pos, c.yaw, c.pitch, c.roll);
    Mat4 inv = Mat4::rotY(c.yaw) * Mat4::rotX(c.pitch) * Mat4::rotZ(c.roll);
    c.fwd = inv.dir(vec3(0, 0, -1));
    c.right = inv.dir(vec3(1, 0, 0));
    c.up = inv.dir(vec3(0, 1, 0));
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(c.proj.m);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(c.view.m);
    // plans du frustum
    Mat4 vp = c.proj * c.view;
    const float* m = vp.m;
    float rows[4][4];
    for (int r = 0; r < 4; r++)
        for (int k = 0; k < 4; k++) rows[r][k] = m[k * 4 + r];
    for (int k = 0; k < 4; k++) {
        s_frustum[0][k] = rows[3][k] + rows[0][k];
        s_frustum[1][k] = rows[3][k] - rows[0][k];
        s_frustum[2][k] = rows[3][k] + rows[1][k];
        s_frustum[3][k] = rows[3][k] - rows[1][k];
        s_frustum[4][k] = rows[3][k] + rows[2][k];
        s_frustum[5][k] = rows[3][k] - rows[2][k];
    }
    for (int p = 0; p < 6; p++) {
        float l = sqrtf(s_frustum[p][0] * s_frustum[p][0] + s_frustum[p][1] * s_frustum[p][1] + s_frustum[p][2] * s_frustum[p][2]);
        for (int k = 0; k < 4; k++) s_frustum[p][k] /= l;
    }
}

bool sphereVisible(const vec3& c, float r) {
    for (int p = 0; p < 6; p++)
        if (s_frustum[p][0] * c.x + s_frustum[p][1] * c.y + s_frustum[p][2] * c.z + s_frustum[p][3] < -r) return false;
    return true;
}

// ---------------------------------------------------------------- Lumières
struct LightCand { vec3 p; float r, g, b, radius, prio; };
static LightCand s_lights[64];
static int s_nLights = 0;

void lightsReset() { s_nLights = 0; }

void lightAdd(const vec3& p, float r, float g, float b, float radius, float prio) {
    float d = length(p - g_cam.pos);
    if (d > 70) return;
    float score = prio * radius / (1 + d);
    if (s_nLights < 64) {
        LightCand& L = s_lights[s_nLights++];
        L.p = p; L.r = r; L.g = g; L.b = b; L.radius = radius; L.prio = score;
    } else {
        int w = 0;
        for (int i = 1; i < 64; i++) if (s_lights[i].prio < s_lights[w].prio) w = i;
        if (s_lights[w].prio < score) {
            LightCand& L = s_lights[w];
            L.p = p; L.r = r; L.g = g; L.b = b; L.radius = radius; L.prio = score;
        }
    }
}

void lightsApply() {
    // tri partiel : 5 meilleures lumières ponctuelles (GL_LIGHT1..5)
    for (int i = 0; i < s_nLights; i++)
        for (int j = i + 1; j < s_nLights; j++)
            if (s_lights[j].prio > s_lights[i].prio) { LightCand t = s_lights[i]; s_lights[i] = s_lights[j]; s_lights[j] = t; }
    for (int i = 0; i < 5; i++) {
        GLenum L = GL_LIGHT1 + i;
        if (i < s_nLights) {
            LightCand& c = s_lights[i];
            float pos[4] = { c.p.x, c.p.y, c.p.z, 1 };
            float dif[4] = { c.r, c.g, c.b, 1 };
            float zero[4] = { 0, 0, 0, 1 };
            glEnable(L);
            glLightfv(L, GL_POSITION, pos);
            glLightfv(L, GL_DIFFUSE, dif);
            glLightfv(L, GL_AMBIENT, zero);
            glLightf(L, GL_CONSTANT_ATTENUATION, 1.0f);
            glLightf(L, GL_LINEAR_ATTENUATION, 0.0f);
            glLightf(L, GL_QUADRATIC_ATTENUATION, 1.0f / (c.radius * c.radius * 0.35f));
        } else {
            glDisable(L);
        }
    }
}

// ---------------------------------------------------------------- 2D
static Batch s_2d;
static Batch s_text;
static bool s_2dInit = false;

void begin2D() {
    if (!s_2dInit) {
        s_2d.init(TEX_WHITE, BM_ALPHA, false);
        s_text.init(TEX_FONT, BM_ALPHA, false);
        s_2dInit = true;
    }
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_FOG);
    glDisable(GL_LIGHTING);
    glMatrixMode(GL_PROJECTION);
    Mat4 o = Mat4::ortho(0, 960, 544, 0, -1, 1);
    glLoadMatrixf(o.m);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void end2D() {
    batchFlush(s_2d);
    batchFlush(s_text);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
}

void draw2DRect(float x, float y, float w, float h, Color c) {
    if (!s_text.v.empty()) { batchFlush(s_2d); batchFlush(s_text); }
    vec3 n(0, 0, 1);
    emitQuad(s_2d, vec3(x, y, 0), vec3(x + w, y, 0), vec3(x + w, y + h, 0), vec3(x, y + h, 0), n, c, 0, 0, 1, 1);
}

// Police : voir textures.cpp (glyphes 6x10 dans une grille 16x8 de 8x12 px)
int fontGlyph(uint32_t cp);

static uint32_t utf8Next(const char*& s) {
    uint8_t c = (uint8_t)*s++;
    if (c < 0x80) return c;
    if ((c & 0xE0) == 0xC0) { uint32_t r = ((c & 0x1F) << 6) | ((uint8_t)*s & 0x3F); if (*s) s++; return r; }
    if ((c & 0xF0) == 0xE0) {
        uint32_t r = (c & 0x0F) << 12;
        if (*s) r |= ((uint8_t)*s++ & 0x3F) << 6;
        if (*s) r |= ((uint8_t)*s++ & 0x3F);
        return r;
    }
    while ((*s & 0xC0) == 0x80) s++;
    return '?';
}

float textWidth(const char* s, float scale) {
    float w = 0;
    while (*s) { utf8Next(s); w += 6 * scale; }
    return w;
}

void drawText(float x, float y, const char* s, float scale, Color c, int align) {
    if (align == 1) x -= textWidth(s, scale) * 0.5f;
    else if (align == 2) x -= textWidth(s, scale);
    vec3 n(0, 0, 1);
    float cw = 8 * scale, chh = 12 * scale;
    // ombre portée
    for (int pass = 0; pass < 2; pass++) {
        const char* p = s;
        float cx = x + (pass == 0 ? scale : 0), cy = y + (pass == 0 ? scale : 0);
        Color col = pass == 0 ? rgba(0, 0, 0, c.a * 3 / 4) : c;
        while (*p) {
            uint32_t cp = utf8Next(p);
            int g = fontGlyph(cp);
            if (g > 0) {
                float u0 = (g % 16) * 8 / 128.0f, v0 = (g / 16) * 12 / 128.0f;
                float u1 = u0 + 8 / 128.0f, v1 = v0 + 12 / 128.0f;
                emitQuad(s_text, vec3(cx, cy, 0), vec3(cx + cw, cy, 0), vec3(cx + cw, cy + chh, 0), vec3(cx, cy + chh, 0), n, col, u0, v0, u1, v1);
            }
            cx += 6 * scale;
        }
    }
}
